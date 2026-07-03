package org.pamguard.port.reference;

import PamUtils.complex.ComplexArray;
import Spectrogram.WindowFunction;
import fftManager.FastFFT;

/**
 * Exports PAMGuard FastFFT values for C++ parity tests.
 *
 * Expected classpath includes compiled PAMGuard classes and Maven dependencies.
 */
public final class FftFixtureExporter {

    private FftFixtureExporter() {
    }

    public static void main(String[] args) {
        if (args.length < 2) {
            System.err.println("Usage: FftFixtureExporter <windowType> <fftLength>");
            System.exit(2);
        }

        int windowType = Integer.parseInt(args[0]);
        int fftLength = Integer.parseInt(args[1]);

        double[] window = WindowFunction.getWindowFunc(windowType, fftLength);
        double[] samples = new double[fftLength];
        for (int i = 0; i < fftLength; i++) {
            samples[i] = syntheticSample(i) * window[i];
        }

        FastFFT fft = new FastFFT();
        ComplexArray spectrum = fft.rfft(samples, fftLength);

        System.out.println("index,real,imag,magsq");
        for (int i = 0; i < spectrum.length(); i++) {
            double real = spectrum.getReal(i);
            double imag = spectrum.getImag(i);
            double magsq = spectrum.magsq(i);
            System.out.printf("%d,%.17g,%.17g,%.17g%n", i, real, imag, magsq);
        }
    }

    private static double syntheticSample(int index) {
        return Math.sin(index * 0.2) + 0.25 * Math.cos(index * 0.7);
    }
}

