package org.pamguard.port.reference;

import PamUtils.PamUtils;
import PamUtils.complex.ComplexArray;
import PamguardMVC.RawDataTransforms;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

public final class ClickBasicClassifierFixtureExporter {
    private static final int ENABLE_ENERGYBAND = 0x1;
    private static final int ENABLE_PEAKFREQWIDTH = 0x2;
    private static final int ENABLE_PEAKFREQPOS = 0x4;
    private static final int ENABLE_MEANFREQUENCY = 0x8;
    private static final int ENABLE_CLICKLENGTH = 0x10;
    private static final double SAMPLE_RATE = 48000.0;

    private static final class ClickType {
        int speciesCode;
        boolean discard;
        int whichSelections;
        double[] band1Freq = new double[2];
        double[] band2Freq = new double[2];
        double[] band1Energy = new double[2];
        double[] band2Energy = new double[2];
        double bandEnergyDifference;
        double[] peakFrequencySearch = new double[2];
        double[] peakFrequencyRange = new double[2];
        double[] peakWidth = new double[2];
        double widthEnergyFraction;
        double[] meanSumRange = new double[2];
        double[] meanSelRange = new double[2];
        double[] clickLength = new double[2];
        double lengthEnergyFraction;
    }

    private static final class ClickId {
        int clickType;
        boolean discard;
    }

    private ClickBasicClassifierFixtureExporter() {
    }

    private static final class ClassifierCase {
        String name;
        ClickType[] clickTypes;

        ClassifierCase(String name, ClickType... clickTypes) {
            this.name = name;
            this.clickTypes = clickTypes;
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: ClickBasicClassifierFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        double[][] waveform = syntheticWaveform();

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,clickType,discard");
            for (ClassifierCase classifierCase : caseCatalogue()) {
                ClickId clickId = identify(waveform, classifierCase.clickTypes);
                writer.printf(Locale.ROOT, "%s,%d,%s%n",
                        classifierCase.name, clickId.clickType, Boolean.toString(clickId.discard));
            }
        }
    }

    /**
     * Case catalogue shared by name with the C++ fixture check
     * (cpp-engine/tools/click_basic_classifier_fixture_check.cpp).
     * Both sides must build identical type lists per case.
     */
    private static ClassifierCase[] caseCatalogue() {
        return new ClassifierCase[]{
                new ClassifierCase("all-pass", failingType(), passingType()),
                new ClassifierCase("band1-range-fail", failingType()),
                new ClassifierCase("band-diff-fail", bandDiffFailType()),
                new ClassifierCase("peak-pos-only-pass", selectionOnlyType(11, ENABLE_PEAKFREQPOS)),
                new ClassifierCase("peak-pos-only-fail", peakPosFailType()),
                new ClassifierCase("peak-width-only-fail", peakWidthFailType()),
                new ClassifierCase("mean-freq-only-pass", selectionOnlyType(12, ENABLE_MEANFREQUENCY)),
                new ClassifierCase("length-only-fail", lengthFailType()),
                new ClassifierCase("length-zero-max-pass", lengthZeroMaxType()),
                new ClassifierCase("order-first-wins",
                        selectionOnlyType(21, ENABLE_MEANFREQUENCY),
                        selectionOnlyType(22, ENABLE_PEAKFREQPOS)),
                new ClassifierCase("no-selections-pass", selectionOnlyType(30, 0)),
        };
    }

    private static ClickType selectionOnlyType(int speciesCode, int whichSelections) {
        ClickType type = passingType();
        type.speciesCode = speciesCode;
        type.discard = false;
        type.whichSelections = whichSelections;
        return type;
    }

    private static ClickType bandDiffFailType() {
        ClickType type = selectionOnlyType(8, ENABLE_ENERGYBAND);
        type.bandEnergyDifference = 15.0;
        return type;
    }

    private static ClickType peakPosFailType() {
        ClickType type = selectionOnlyType(11, ENABLE_PEAKFREQPOS);
        type.peakFrequencyRange = new double[]{11000.0, 12000.0};
        return type;
    }

    private static ClickType peakWidthFailType() {
        ClickType type = selectionOnlyType(14, ENABLE_PEAKFREQWIDTH);
        type.peakWidth = new double[]{100.0, 500.0};
        return type;
    }

    private static ClickType lengthFailType() {
        ClickType type = selectionOnlyType(15, ENABLE_CLICKLENGTH);
        type.clickLength = new double[]{0.05, 0.10};
        return type;
    }

    private static ClickType lengthZeroMaxType() {
        ClickType type = selectionOnlyType(13, ENABLE_CLICKLENGTH);
        type.clickLength = new double[]{0.05, 0.0};
        return type;
    }

    private static ClickId identify(double[][] waveform, ClickType[] clickTypes) {
        for (ClickType clickType : clickTypes) {
            if (isThisType(waveform, clickType)) {
                ClickId id = new ClickId();
                id.clickType = clickType.speciesCode;
                id.discard = clickType.discard;
                return id;
            }
        }
        return new ClickId();
    }

    private static boolean isThisType(double[][] waveform, ClickType type) {
        int fftLength = PamUtils.getMinFftLength(waveform[0].length);
        double[][] spectra = powerSpectra(waveform, fftLength);
        double[] totalPower = totalPower(spectra, fftLength);

        if ((type.whichSelections & ENABLE_ENERGYBAND) != 0) {
            double eTest1 = inBandEnergy(spectra, type.band1Freq, fftLength);
            if (eTest1 < type.band1Energy[0] || eTest1 > type.band1Energy[1]) {
                return false;
            }
            double eTest2 = inBandEnergy(spectra, type.band2Freq, fftLength);
            if (eTest2 < type.band2Energy[0] || eTest2 > type.band2Energy[1]) {
                return false;
            }
            if (eTest1 - eTest2 < type.bandEnergyDifference) {
                return false;
            }
        }

        double peakFreq = 0.0;
        if ((type.whichSelections & ENABLE_PEAKFREQPOS) != 0 || (type.whichSelections & ENABLE_PEAKFREQWIDTH) != 0) {
            peakFreq = peakFrequency(totalPower, type.peakFrequencySearch, fftLength);
            if ((type.whichSelections & ENABLE_PEAKFREQPOS) != 0) {
                if (peakFreq < type.peakFrequencyRange[0] || peakFreq > type.peakFrequencyRange[1]) {
                    return false;
                }
            }
            if ((type.whichSelections & ENABLE_PEAKFREQWIDTH) != 0) {
                double peakWidth = peakFrequencyWidth(totalPower, peakFreq, type.widthEnergyFraction, fftLength);
                if (peakWidth < type.peakWidth[0] || peakWidth > type.peakWidth[1]) {
                    return false;
                }
            }
        }

        if ((type.whichSelections & ENABLE_MEANFREQUENCY) != 0) {
            double meanFreq = meanFrequency(totalPower, type.meanSumRange, fftLength);
            if (meanFreq < type.meanSelRange[0] || meanFreq > type.meanSelRange[1]) {
                return false;
            }
        }

        if ((type.whichSelections & ENABLE_CLICKLENGTH) != 0 && type.clickLength[1] > 0.0) {
            double clickLengthMs = clickLength(waveform, type.lengthEnergyFraction) * 1000.0;
            if (clickLengthMs < type.clickLength[0] || clickLengthMs > type.clickLength[1]) {
                return false;
            }
        }

        return true;
    }

    private static ClickType failingType() {
        ClickType type = passingType();
        type.speciesCode = 7;
        type.discard = false;
        type.band1Energy = new double[]{210.0, 220.0};
        return type;
    }

    private static ClickType passingType() {
        ClickType type = new ClickType();
        type.speciesCode = 42;
        type.discard = true;
        type.whichSelections = ENABLE_ENERGYBAND | ENABLE_PEAKFREQWIDTH | ENABLE_PEAKFREQPOS | ENABLE_MEANFREQUENCY | ENABLE_CLICKLENGTH;
        type.band1Freq = new double[]{6000.0, 12000.0};
        type.band2Freq = new double[]{12000.0, 18000.0};
        type.band1Energy = new double[]{198.0, 201.0};
        type.band2Energy = new double[]{190.0, 193.0};
        type.bandEnergyDifference = 5.0;
        type.peakFrequencySearch = new double[]{3000.0, 20000.0};
        type.peakFrequencyRange = new double[]{8500.0, 9500.0};
        type.peakWidth = new double[]{2500.0, 3500.0};
        type.widthEnergyFraction = 80.0;
        type.meanSumRange = new double[]{3000.0, 20000.0};
        type.meanSelRange = new double[]{9500.0, 10000.0};
        type.clickLength = new double[]{0.30, 0.38};
        type.lengthEnergyFraction = 90.0;
        return type;
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

    private static double[][] powerSpectra(double[][] waveform, int fftLength) {
        double[][] spectra = new double[waveform.length][];
        for (int channel = 0; channel < waveform.length; channel++) {
            ComplexArray spectrum = RawDataTransforms.getComplexSpectrumHann(waveform[channel], fftLength);
            spectra[channel] = spectrum.magsq();
        }
        return spectra;
    }

    private static double[] totalPower(double[][] spectra, int fftLength) {
        double[] totalPower = new double[fftLength / 2];
        for (double[] spectrum : spectra) {
            for (int i = 0; i < totalPower.length; i++) {
                totalPower[i] += spectrum[i];
            }
        }
        return totalPower;
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

    private static double clickLength(double[][] waveform, double percent) {
        double sum = 0.0;
        for (double[] channelWaveform : waveform) {
            sum += clickLength(channelWaveform, percent);
        }
        return sum / waveform.length;
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
        int bin2 = (int) Math.min(fftLength / 2 - 1, (int) Math.ceil(searchRange[1] * fftLength / SAMPLE_RATE));
        double top = 0.0;
        double bottom = 0.0;
        for (int i = bin1; i <= bin2; i++) {
            top += i * totalPower[i];
            bottom += totalPower[i];
        }
        return top / bottom * SAMPLE_RATE / fftLength;
    }
}
