package org.pamguard.port.reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public final class WhistlePeakFixtureExporter {
    private static final int WARMUP_SLICES = 100;

    private enum PeakStatus {
        PEAK_ON, PEAK_OFF
    }

    private static final class Peak {
        int minFreq;
        int peakFreq;
        int maxFreq;
        double maxAmp;
        double signal;
        double noise;
        boolean ok;
    }

    private WhistlePeakFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 4) {
            System.err.println("Usage: WhistlePeakFixtureExporter <fftLength> <fftHop> <sampleRate> <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        int fftLength = Integer.parseInt(args[0]);
        int fftHop = Integer.parseInt(args[1]);
        double sampleRate = Double.parseDouble(args[2]);
        File output = new File(args[3]);

        int half = fftLength / 2;
        double[] spectrumAverage = new double[half];
        boolean[] overThreshold = new boolean[half];
        double[] magSquareData = new double[half];
        double[] localAverage = new double[half];
        int searchBin0 = 1;
        int searchBin1 = half - 2;
        double detectionThreshold = Math.pow(10.0, 6.0 / 10.0);
        double bgndUpdate0 = fftHop / sampleRate / 10.0;
        double bgndUpdate0_1 = 1.0 - bgndUpdate0;
        double bgndUpdate1 = fftHop / sampleRate / 100.0;
        double bgndUpdate1_1 = 1.0 - bgndUpdate1;
        int minPeakWidth = 3;
        int maxPeakWidth = 20;
        double maxPercentOverThreshold = 50.0;
        int slicesAnalysed = 0;
        List<Peak> peaks = new ArrayList<>();

        for (int slice = 0; slice <= WARMUP_SLICES; slice++) {
            slicesAnalysed++;
            double[] magsq = syntheticSpectrum(half, slice == WARMUP_SLICES);
            if (slicesAnalysed <= WARMUP_SLICES) {
                for (int i = 0; i < half; i++) {
                    spectrumAverage[i] += magsq[i] / WARMUP_SLICES;
                }
                continue;
            }

            PeakStatus peakOn = PeakStatus.PEAK_OFF;
            Peak newPeak = new Peak();
            int nOver = 0;
            for (int i = searchBin0; i <= searchBin1; i++) {
                magSquareData[i] = magsq[i];
                double newval = magSquareData[i] / spectrumAverage[i];
                if ((overThreshold[i] = newval > detectionThreshold)) {
                    spectrumAverage[i] *= bgndUpdate1_1;
                    spectrumAverage[i] += magSquareData[i] * bgndUpdate1;
                    nOver++;
                }
                else {
                    spectrumAverage[i] *= bgndUpdate0_1;
                    spectrumAverage[i] += magSquareData[i] * bgndUpdate0;
                }
                localAverage[i] = 0.0;
            }

            if (nOver * 100.0 / overThreshold.length > maxPercentOverThreshold) {
                continue;
            }

            int localAverageLen = 5;
            int localAverageGap = 5;
            int lao = localAverageLen / 2;
            int k = half;
            for (int i = 0; i < localAverageLen - lao; i++) {
                k--;
                int l = half;
                for (int j = 0; j < localAverageLen; j++) {
                    l--;
                    localAverage[i] += magSquareData[j];
                    localAverage[k] += magSquareData[l];
                }
                localAverage[i] /= localAverageLen;
                localAverage[k] /= localAverageLen;
            }
            for (int i = localAverageLen - lao; i < half - lao; i++) {
                localAverage[i] += (magSquareData[i + lao] - magSquareData[i - lao]) / localAverageLen;
            }

            int sao = (localAverageLen / 2) + (localAverageGap / 2);
            for (int i = 0; i < sao; i++) {
                magSquareData[i] = 0.0;
                magSquareData[half - 1 - i] = 0.0;
            }
            for (int i = sao + searchBin0; i < (searchBin1 - sao); i++) {
                magSquareData[i] -= (localAverage[i - sao] + localAverage[i + sao]) / 2.0;
                overThreshold[i] = ((magSquareData[i] + spectrumAverage[i]) / spectrumAverage[i] > detectionThreshold);
                if (peakOn == PeakStatus.PEAK_OFF) {
                    if (overThreshold[i]) {
                        newPeak.minFreq = newPeak.maxFreq = newPeak.peakFreq = i;
                        newPeak.signal = newPeak.maxAmp = spectrumAverage[i];
                        newPeak.noise = spectrumAverage[i];
                        newPeak.ok = overThreshold[i];
                        peakOn = PeakStatus.PEAK_ON;
                    }
                }
                else if (peakOn == PeakStatus.PEAK_ON) {
                    if (!overThreshold[i]) {
                        peakOn = PeakStatus.PEAK_OFF;
                        int peakWidth = newPeak.maxFreq - newPeak.minFreq + 1;
                        if (peakWidth >= minPeakWidth && peakWidth <= maxPeakWidth) {
                            peaks.add(newPeak);
                            newPeak = new Peak();
                        }
                    }
                    else {
                        newPeak.maxFreq = i;
                        magSquareData[i] = magsq[i];
                        if (magSquareData[i] > newPeak.maxAmp) {
                            newPeak.maxAmp = magSquareData[i];
                            newPeak.peakFreq = i;
                        }
                        newPeak.signal += magSquareData[i];
                        newPeak.noise += spectrumAverage[i];
                        newPeak.ok |= overThreshold[i];
                    }
                }
            }
        }

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("index,minFreq,peakFreq,maxFreq,maxAmp,signal,noise,ok");
            for (int i = 0; i < peaks.size(); i++) {
                Peak peak = peaks.get(i);
                writer.printf(Locale.ROOT, "%d,%d,%d,%d,%.17g,%.17g,%.17g,%s%n",
                        i,
                        peak.minFreq,
                        peak.peakFreq,
                        peak.maxFreq,
                        peak.maxAmp,
                        peak.signal,
                        peak.noise,
                        Boolean.toString(peak.ok));
            }
        }
    }

    private static double[] syntheticSpectrum(int half, boolean withPeak) {
        double[] spectrum = new double[half];
        for (int i = 0; i < half; i++) {
            spectrum[i] = 1.0 + 0.01 * Math.sin(i * 0.33);
        }
        if (withPeak) {
            spectrum[20] = 22.0;
            spectrum[21] = 26.0;
            spectrum[22] = 31.0;
            spectrum[23] = 24.0;
            spectrum[24] = 18.0;
        }
        return spectrum;
    }
}
