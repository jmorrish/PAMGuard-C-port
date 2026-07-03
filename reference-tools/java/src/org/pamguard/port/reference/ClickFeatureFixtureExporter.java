package org.pamguard.port.reference;

import PamUtils.PamUtils;
import PamUtils.complex.ComplexArray;
import PamguardMVC.RawDataTransforms;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

public final class ClickFeatureFixtureExporter {
    private static final double SAMPLE_RATE = 48000.0;
    private static final double LENGTH_ENERGY_FRACTION = 90.0;
    private static final double WIDTH_ENERGY_FRACTION = 80.0;
    private static final double[][] ENERGY_BANDS = {
            {6000.0, 12000.0},
            {12000.0, 18000.0},
    };
    private static final double[] SEARCH_RANGE = {3000.0, 20000.0};
    private static final int SPECTRUM_EXPORT_BINS = 6;

    private ClickFeatureFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: ClickFeatureFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        double[][] waveform = syntheticWaveform();
        int fftLength = PamUtils.getMinFftLength(waveform[0].length);
        double[][] spectra = new double[waveform.length][];
        double[] totalPower = new double[fftLength / 2];
        for (int channel = 0; channel < waveform.length; channel++) {
            ComplexArray spectrum = RawDataTransforms.getComplexSpectrumHann(waveform[channel], fftLength);
            spectra[channel] = spectrum.magsq();
            for (int i = 0; i < totalPower.length; i++) {
                totalPower[i] += spectra[channel][i];
            }
        }

        double[] bandEnergy = new double[ENERGY_BANDS.length];
        for (int i = 0; i < ENERGY_BANDS.length; i++) {
            bandEnergy[i] = inBandEnergy(spectra, ENERGY_BANDS[i], fftLength);
        }
        double channel0Length = clickLength(waveform[0], LENGTH_ENERGY_FRACTION);
        double channel1Length = clickLength(waveform[1], LENGTH_ENERGY_FRACTION);
        double clickLength = (channel0Length + channel1Length) / 2.0;
        double peakFrequency = peakFrequency(totalPower, SEARCH_RANGE, fftLength);
        double peakWidth = peakFrequencyWidth(totalPower, peakFrequency, WIDTH_ENERGY_FRACTION, fftLength);
        double meanFrequency = meanFrequency(totalPower, SEARCH_RANGE, fftLength);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.print("fftLength,band0Db,band1Db,peakFrequencyHz,peakWidthHz,meanFrequencyHz,clickLengthSeconds,ch0LengthSeconds,ch1LengthSeconds");
            for (int i = 0; i < SPECTRUM_EXPORT_BINS; i++) {
                writer.print(",totalPower" + i);
            }
            writer.println();
            writer.printf(Locale.ROOT,
                    "%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g",
                    fftLength,
                    bandEnergy[0],
                    bandEnergy[1],
                    peakFrequency,
                    peakWidth,
                    meanFrequency,
                    clickLength,
                    channel0Length,
                    channel1Length);
            for (int i = 0; i < SPECTRUM_EXPORT_BINS; i++) {
                writer.printf(Locale.ROOT, ",%.17g", totalPower[i]);
            }
            writer.println();
        }
    }

    private static double[][] syntheticWaveform() {
        int length = 96;
        double[][] waveform = new double[2][length];
        for (int i = 0; i < length; i++) {
            double env = Math.exp(-0.5 * Math.pow((i - 42.0) / 7.0, 2.0));
            waveform[0][i] = 0.03 * Math.sin(i * 0.19)
                    + env * (Math.sin(2.0 * Math.PI * 9000.0 * i / SAMPLE_RATE)
                    + 0.45 * Math.sin(2.0 * Math.PI * 14000.0 * i / SAMPLE_RATE));
            waveform[1][i] = 0.02 * Math.cos(i * 0.11 + 0.2)
                    + 0.82 * env * (Math.sin(2.0 * Math.PI * 9200.0 * i / SAMPLE_RATE + 0.4)
                    + 0.25 * Math.sin(2.0 * Math.PI * 15000.0 * i / SAMPLE_RATE));
        }
        return waveform;
    }

    private static double inBandEnergy(double[][] spectra, double[] freqs, int fftLength) {
        double energy = 0.0;
        int f1 = Math.max(0, (int) Math.floor(freqs[0] * fftLength / SAMPLE_RATE));
        int f2 = Math.min((fftLength / 2) - 1, (int) Math.ceil(freqs[1] * fftLength / SAMPLE_RATE));
        for (double[] spectrum : spectra) {
            for (int f = f1; f <= f2; f++) {
                energy += spectrum[f];
            }
        }
        if (energy > 0.0) {
            return 10.0 * Math.log10(energy) + 172.0;
        }
        return -100.0;
    }

    private static double clickLength(double[] waveform, double percent) {
        int nAverage = 3;
        double[] smoothData = new double[waveform.length];
        double dataMaximum = 0.0;
        int maxPosition = 0;
        for (int i = 0; i < smoothData.length; i++) {
            smoothData[i] = Math.pow(waveform[i], 2.0);
        }
        for (int i = 0; i < smoothData.length - nAverage; i++) {
            for (int j = 1; j < nAverage; j++) {
                smoothData[i] += smoothData[i + j];
            }
            if (smoothData[i] > dataMaximum) {
                dataMaximum = smoothData[i];
                maxPosition = i;
            }
        }
        int length = getSpikeWidth(smoothData, maxPosition, percent);
        return length / SAMPLE_RATE;
    }

    private static int getSpikeWidth(double[] data, int peakPos, double percent) {
        int width = 1;
        int len = data.length;
        double targetEnergy = 0.0;
        if (percent >= 100.0) {
            return len;
        }
        for (double value : data) {
            targetEnergy += value;
        }
        targetEnergy *= percent / 100.0;
        double foundEnergy = data[peakPos];
        int inext = peakPos + 1;
        int iprev = peakPos - 1;
        while (foundEnergy < targetEnergy) {
            double next = 0.0;
            double prev = 0.0;
            if (inext < len) {
                next = data[inext];
            }
            if (iprev >= 0) {
                prev = data[iprev];
            }
            if (next > prev) {
                foundEnergy += next;
                inext++;
                width++;
            }
            else if (next < prev) {
                foundEnergy += prev;
                iprev--;
                width++;
            }
            else {
                foundEnergy += next + prev;
                inext++;
                iprev--;
                width += 2;
            }
            if (iprev < 0 && inext >= len) {
                break;
            }
        }
        return width;
    }

    private static double peakFrequency(double[] totalPower, double[] searchRange, int fftLength) {
        int bin1 = (int) Math.max(0, Math.floor(searchRange[0] * fftLength / SAMPLE_RATE));
        int bin2 = (int) Math.min(fftLength / 2 - 1, Math.ceil(searchRange[1] * fftLength / SAMPLE_RATE));
        int peakPos = 0;
        double peakEnergy = 0.0;
        for (int i = bin1; i <= bin2; i++) {
            if (totalPower[i] > peakEnergy) {
                peakEnergy = totalPower[i];
                peakPos = i;
            }
        }
        return peakPos * SAMPLE_RATE / fftLength;
    }

    private static double peakFrequencyWidth(double[] totalPower, double peakFrequency, double percent, int fftLength) {
        int peakPos = (int) (peakFrequency * fftLength / SAMPLE_RATE);
        int width = getSpikeWidth(totalPower, peakPos, percent);
        return width * SAMPLE_RATE / fftLength;
    }

    private static double meanFrequency(double[] totalPower, double[] searchRange, int fftLength) {
        int bin1 = (int) Math.max(0, Math.floor(searchRange[0] * fftLength / SAMPLE_RATE));
        int bin2 = (int) Math.min(fftLength / 2 - 1, Math.ceil(searchRange[1] * fftLength / SAMPLE_RATE));
        double top = 0.0;
        double bottom = 0.0;
        for (int i = bin1; i <= bin2; i++) {
            top += i * totalPower[i];
            bottom += totalPower[i];
        }
        return top / bottom * SAMPLE_RATE / fftLength;
    }
}
