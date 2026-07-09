package org.pamguard.port.reference;

import Localiser.algorithms.Correlations;
import PamUtils.complex.ComplexArray;
import fftManager.FastFFT;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

/**
 * Exports whistle contour delay fixtures for whistlesAndMoans.WhistleDelays.
 *
 * WhistleDelays itself is coupled to WhistleMoanControl/FFT data blocks, so
 * its inner DelayMeasure accumulation (cross-spectrum over contour bins) is
 * transcribed verbatim while the numerically significant reference pieces —
 * FastFFT.realInverse (JTransforms real packing) and
 * Correlations.getInterpolatedPeak — are the real PAMGuard classes.
 *
 * Synthetic per-slice complex spectra put a pure spectral delay on channel 2:
 * ch2[k] = gain * ch1[k] * exp(-2*pi*i*k*tau/fftLength) plus a small
 * deterministic phase jitter. Contour bins never include bin zero, matching
 * real whistle contours (DelayMeasure stores bin-zero imaginary parts in the
 * JTransforms slot reserved for the Nyquist real value).
 *
 * Case parameters are shared by name with the C++ fixture check
 * (cpp-engine/tools/whistle_delay_fixture_check.cpp).
 */
public final class WhistleDelayFixtureExporter {

    private static final int FFT_LENGTH = 256;

    private static final class DelayCase {
        String name;
        double delaySamples;
        double gain;
        int sliceCount;
        int firstBin;
        int binsPerSlice;
        int binRisePerSlice;
        double maxDelay;

        DelayCase(String name, double delaySamples, double gain, int sliceCount,
                  int firstBin, int binsPerSlice, int binRisePerSlice, double maxDelay) {
            this.name = name;
            this.delaySamples = delaySamples;
            this.gain = gain;
            this.sliceCount = sliceCount;
            this.firstBin = firstBin;
            this.binsPerSlice = binsPerSlice;
            this.binRisePerSlice = binRisePerSlice;
            this.maxDelay = maxDelay;
        }
    }

    private WhistleDelayFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: WhistleDelayFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,delaySamples,delayScore");
            for (DelayCase delayCase : caseCatalogue()) {
                double[] peak = runCase(delayCase);
                writer.printf(Locale.ROOT, "%s,%.17g,%.17g%n", delayCase.name, peak[0], peak[1]);
            }
        }
    }

    private static DelayCase[] caseCatalogue() {
        return new DelayCase[]{
                new DelayCase("zero-delay", 0.0, 0.9, 6, 30, 5, 0, 40.0),
                new DelayCase("fractional-positive", 3.5, 0.9, 6, 30, 5, 0, 40.0),
                new DelayCase("fractional-negative", -2.25, 0.85, 6, 30, 5, 0, 40.0),
                // True delay outside the +-maxDelay search window: the
                // narrowband contour's periodic correlation makes a
                // near-zero sidelobe win instead (PAMGuard behaviour worth
                // pinning - narrowband whistle delays are ambiguous).
                new DelayCase("beyond-window-ambiguity", 8.0, 0.9, 6, 30, 5, 0, 5.0),
                new DelayCase("rising-contour", 1.75, 0.8, 8, 28, 3, 2, 40.0),
        };
    }

    private static double[] runCase(DelayCase delayCase) {
        // DelayMeasure state, transcribed: cross spectrum plus per-channel
        // magnitude-squared scales accumulated over contour bins.
        ComplexArray complexData = new ComplexArray(FFT_LENGTH / 2);
        double scale1 = 0.0;
        double scale2 = 0.0;

        for (int slice = 0; slice < delayCase.sliceCount; slice++) {
            ComplexArray ch1 = new ComplexArray(FFT_LENGTH / 2);
            ComplexArray ch2 = new ComplexArray(FFT_LENGTH / 2);
            int startBin = delayCase.firstBin + slice * delayCase.binRisePerSlice;
            for (int b = 0; b < delayCase.binsPerSlice; b++) {
                int bin = startBin + b;
                fillBin(ch1, ch2, bin, slice, delayCase);
            }

            for (int b = 0; b < delayCase.binsPerSlice; b++) {
                int iFreq = startBin + b;
                scale1 += ch1.magsq(iFreq);
                scale2 += ch2.magsq(iFreq);
                double[] d = complexData.getData();
                double ch1r = ch1.getReal(iFreq);
                double ch1i = ch1.getImag(iFreq);
                double ch2r = ch2.getReal(iFreq);
                double ch2i = ch2.getImag(iFreq);
                d[iFreq * 2] += (ch1r * ch2r + ch1i * ch2i);
                d[iFreq * 2 + 1] += (ch1i * ch2r - ch2i * ch1r);
            }
        }

        double scale = Math.sqrt(scale1 * scale2) * 2 / FFT_LENGTH;
        FastFFT fft = new FastFFT();
        double[] xCorr = fft.realInverse(complexData);
        Correlations correlations = new Correlations();
        return correlations.getInterpolatedPeak(xCorr, scale, delayCase.maxDelay);
    }

    private static void fillBin(ComplexArray ch1, ComplexArray ch2, int bin, int slice, DelayCase delayCase) {
        double amplitude = 1.0 + 0.1 * Math.sin(bin * 0.7 + slice * 0.5);
        double phase = 0.31 * slice + 0.117 * bin;
        double ch1r = amplitude * Math.cos(phase);
        double ch1i = amplitude * Math.sin(phase);
        ch1.set(bin, ch1r, ch1i);

        double jitter = 0.02 * Math.sin(slice + bin);
        double rotation = -2.0 * Math.PI * bin * delayCase.delaySamples / FFT_LENGTH + jitter;
        double cosR = Math.cos(rotation);
        double sinR = Math.sin(rotation);
        double ch2r = delayCase.gain * (ch1r * cosR - ch1i * sinR);
        double ch2i = delayCase.gain * (ch1r * sinR + ch1i * cosR);
        ch2.set(bin, ch2r, ch2i);
    }
}
