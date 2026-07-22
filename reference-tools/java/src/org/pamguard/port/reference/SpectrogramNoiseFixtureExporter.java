package org.pamguard.port.reference;

import PamUtils.complex.ComplexArray;
import fftManager.FFTDataUnit;
import spectrogramNoiseReduction.SpecNoiseMethod;
import spectrogramNoiseReduction.averageSubtraction.AverageSubtraction;
import spectrogramNoiseReduction.averageSubtraction.AverageSubtractionParameters;
import spectrogramNoiseReduction.kernelSmoothing.KernelSmoothing;
import spectrogramNoiseReduction.medianFilter.SpectrogramMedianFilter;
import spectrogramNoiseReduction.threshold.SpectrogramThreshold;
import spectrogramNoiseReduction.threshold.ThresholdParams;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Exports spectrogram noise reduction fixtures by driving the REAL PAMGuard
 * methods end to end — no transcription at all. All four SpecNoiseMethods
 * (median filter, average subtraction, Gaussian kernel smoothing, threshold)
 * construct headlessly, FFTDataUnit and ComplexArray are plain classes, and
 * the chain semantics (methods in fixed order over a cloned slice, then the
 * OUTPUT_RAW pick-earlier pass) follow SpectrogramNoiseProcess.newData.
 *
 * The synthetic spectrogram is a deterministic complex ridge over background,
 * including zero bins (the average-subtraction skip path) and a low-power
 * region (threshold zeroing).
 *
 * Output: one row per (case, slice, bin) with the resulting complex value.
 * Shared by name with cpp-engine/tools/spectrogram_noise_fixture_check.cpp.
 */
public final class SpectrogramNoiseFixtureExporter {

    private static final int BINS = 32;
    private static final int SLICES = 24;

    private SpectrogramNoiseFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: SpectrogramNoiseFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,slice,bin,re,im");
            // The raw input travels in the fixture so the C++ check replays
            // the same bytes rather than re-deriving them from a duplicated
            // formula, which could hide a shared mistake.
            for (int slice = 0; slice < SLICES; slice++) {
                ComplexArray input = syntheticSlice(slice);
                for (int bin = 0; bin < input.length(); bin++) {
                    writer.printf(Locale.ROOT, "input,%d,%d,%.17g,%.17g%n",
                            slice, bin, input.getReal(bin), input.getImag(bin));
                }
            }
            runCase(writer, "median-only", true, false, false, false, 0.02, 8.0, SpectrogramThreshold.OUTPUT_RAW);
            runCase(writer, "average-only", false, true, false, false, 0.02, 8.0, SpectrogramThreshold.OUTPUT_RAW);
            runCase(writer, "kernel-only", false, false, true, false, 0.02, 8.0, SpectrogramThreshold.OUTPUT_RAW);
            runCase(writer, "threshold-raw", false, false, false, true, 0.02, 8.0, SpectrogramThreshold.OUTPUT_RAW);
            runCase(writer, "threshold-binary", false, false, false, true, 0.02, 8.0, SpectrogramThreshold.OUTPUT_BINARY);
            runCase(writer, "threshold-input", false, false, false, true, 0.02, 8.0, SpectrogramThreshold.OUTPUT_INPUT);
            runCase(writer, "full-chain-raw", true, true, true, true, 0.05, 6.0, SpectrogramThreshold.OUTPUT_RAW);
            runCase(writer, "full-chain-binary", true, true, true, true, 0.05, 6.0, SpectrogramThreshold.OUTPUT_BINARY);
        }
    }

    private static void runCase(PrintWriter writer, String name, boolean median, boolean average, boolean kernel,
                                boolean threshold, double updateConstant, double thresholdDb, int finalOutput)
            throws Exception {
        SpectrogramMedianFilter medianFilter = new SpectrogramMedianFilter();
        AverageSubtraction averageSubtraction = new AverageSubtraction();
        AverageSubtractionParameters averageParams = new AverageSubtractionParameters();
        averageParams.updateConstant = updateConstant;
        averageSubtraction.setParams(averageParams);
        KernelSmoothing kernelSmoothing = new KernelSmoothing();
        SpectrogramThreshold spectrogramThreshold = new SpectrogramThreshold();
        ThresholdParams thresholdParams = new ThresholdParams();
        thresholdParams.thresholdDB = thresholdDb;
        thresholdParams.finalOutput = finalOutput;
        spectrogramThreshold.setParams(thresholdParams);

        List<SpecNoiseMethod> methods = new ArrayList<>();
        List<Boolean> run = new ArrayList<>();
        methods.add(medianFilter);
        run.add(median);
        methods.add(averageSubtraction);
        run.add(average);
        methods.add(kernelSmoothing);
        run.add(kernel);
        methods.add(spectrogramThreshold);
        run.add(threshold);
        for (SpecNoiseMethod method : methods) {
            method.initialise(1);
        }

        for (int slice = 0; slice < SLICES; slice++) {
            ComplexArray input = syntheticSlice(slice);
            // SpectrogramNoiseProcess.newData: methods run over a CLONE of the
            // input, then OUTPUT_RAW copies surviving bins back from the
            // (un-noise-reduced) input of the same slice.
            ComplexArray working = input.clone();
            FFTDataUnit unit = new FFTDataUnit(slice * 10L, 1, slice * 128L, 128L, working, slice);
            unit.setSequenceBitmap(null);
            for (int i = 0; i < methods.size(); i++) {
                if (run.get(i)) {
                    methods.get(i).runNoiseReduction(unit);
                }
            }
            ComplexArray result = unit.getFftData();
            if (threshold && finalOutput == SpectrogramThreshold.OUTPUT_RAW) {
                spectrogramThreshold.pickEarlierData(input, result);
            }
            for (int bin = 0; bin < result.length(); bin++) {
                writer.printf(Locale.ROOT, "%s,%d,%d,%.17g,%.17g%n",
                        name, slice, bin, result.getReal(bin), result.getImag(bin));
            }
        }
    }

    /**
     * Deterministic synthetic slice: broadband background, a tonal ridge that
     * drifts upward, a strong transient at slice 12, and exact-zero bins so
     * the average subtraction's zero-skip path runs.
     */
    private static ComplexArray syntheticSlice(int slice) {
        double[] data = new double[BINS * 2];
        for (int bin = 0; bin < BINS; bin++) {
            double phase = 0.37 * bin + 0.11 * slice;
            double magnitude = 0.6 + 0.25 * Math.sin(0.5 * bin + 0.3 * slice);
            int ridgeBin = 8 + slice / 4;
            if (bin == ridgeBin) {
                magnitude += 5.0;
            }
            if (slice == 12 && bin >= 20 && bin <= 23) {
                magnitude += 9.0;
            }
            if (bin == 29) {
                magnitude = 0.0; // exact zero: average subtraction skips it
            }
            data[bin * 2] = magnitude * Math.cos(phase);
            data[bin * 2 + 1] = magnitude * Math.sin(phase);
        }
        return new ComplexArray(data);
    }
}
