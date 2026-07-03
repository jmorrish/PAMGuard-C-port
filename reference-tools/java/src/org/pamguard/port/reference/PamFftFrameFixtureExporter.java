package org.pamguard.port.reference;

import PamUtils.complex.ComplexArray;
import Spectrogram.WindowFunction;
import fftManager.FastFFT;

/**
 * Exports frame-level behaviour mirrored from PamFFTProcess.
 *
 * This intentionally captures PAMGuard's frame sample/time calculations as used
 * in src/fftManager/PamFFTProcess.java, while keeping the fixture independent
 * from the full PamController runtime.
 */
public final class PamFftFrameFixtureExporter {

    private PamFftFrameFixtureExporter() {
    }

    public static void main(String[] args) {
        if (args.length < 5) {
            System.err.println("Usage: PamFftFrameFixtureExporter <windowType> <fftLength> <fftHop> <sampleRate> <chunkLength>");
            System.exit(2);
        }

        int windowType = Integer.parseInt(args[0]);
        int fftLength = Integer.parseInt(args[1]);
        int fftHop = Integer.parseInt(args[2]);
        double sampleRate = Double.parseDouble(args[3]);
        int chunkLength = Integer.parseInt(args[4]);

        double[] rawData = new double[chunkLength];
        for (int i = 0; i < chunkLength; i++) {
            rawData[i] = syntheticSample(i);
        }

        exportFrames(windowType, fftLength, fftHop, sampleRate, rawData);
    }

    private static void exportFrames(int windowType, int fftLength, int fftHop, double sampleRate, double[] rawData) {
        int fftOverlap = fftLength - fftHop;
        int dataPointer = 0;
        int fftSlice = 0;
        long rawStartSample = 0;
        long rawTimeMs = 1000;
        double[] windowedData = new double[fftLength];
        double[] window = WindowFunction.getWindowFunc(windowType, fftLength);
        double[] fftRealBlock = new double[fftLength];
        FastFFT fft = new FastFFT();

        System.out.println("frame,channel,fftSlice,startSample,timeMs,bin,real,imag,magsq");

        for (int i = 0; i < rawData.length; i++) {
            if (dataPointer >= 0) {
                windowedData[dataPointer] = rawData[i];
            }
            dataPointer++;
            if (dataPointer == fftLength) {
                for (int w = 0; w < fftLength; w++) {
                    fftRealBlock[w] = windowedData[w] * window[w];
                }

                long startSample = rawStartSample + i - fftLength;
                long timeMs = rawTimeMs + relSamplesToMilliseconds(i - fftLength, sampleRate);
                ComplexArray spectrum = fft.rfft(fftRealBlock, fftLength);

                for (int bin = 0; bin < spectrum.length(); bin++) {
                    double real = spectrum.getReal(bin);
                    double imag = spectrum.getImag(bin);
                    double magsq = spectrum.magsq(bin);
                    System.out.printf("%d,%d,%d,%d,%d,%d,%.17g,%.17g,%.17g%n",
                            fftSlice, 0, fftSlice, startSample, timeMs, bin, real, imag, magsq);
                }

                fftSlice++;
                dataPointer = fftOverlap;
                if (dataPointer > 0) {
                    int copyFrom = fftHop;
                    for (int j = 0; j < fftOverlap && copyFrom < fftLength; j++) {
                        windowedData[j] = windowedData[copyFrom++];
                    }
                }
            }
        }
    }

    private static long relSamplesToMilliseconds(long samples, double sampleRate) {
        return (long) (samples * 1000 / sampleRate);
    }

    private static double syntheticSample(int index) {
        return Math.sin(index * 0.2) + 0.25 * Math.cos(index * 0.7);
    }
}

