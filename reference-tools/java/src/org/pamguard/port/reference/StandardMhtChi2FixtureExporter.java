package org.pamguard.port.reference;

import PamguardMVC.PamDataUnit;
import clickTrainDetector.clickTrainAlgorithms.mht.MHTKernel;
import clickTrainDetector.clickTrainAlgorithms.mht.MHTKernelParams;
import clickTrainDetector.clickTrainAlgorithms.mht.StandardMHTChi2Params;
import clickTrainDetector.clickTrainAlgorithms.mht.StandardMHTChi2Provider;
import clickTrainDetector.clickTrainAlgorithms.mht.TrackBitSet;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.IDIManager;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.Locale;

/**
 * Exports end-to-end MHT fixtures: the real MHTKernel driven by the real
 * StandardMHTChi2/StandardMHTChi2Provider with the three ported chi2
 * variables enabled (IDI, amplitude, length; bearing/correlation/time-delay/
 * peak-frequency disabled) and the electrical noise filter off.
 *
 * The provider's private IDIManager is replaced by reflection with a
 * fixed-sample-rate subclass so calcTime works without parent data blocks
 * (same transcription as the chi2 variable exporters). Data units carry
 * fixed nanosecond times, amplitudes, and millisecond durations.
 *
 * Scenario values are shared by name with the C++ fixture check
 * (cpp-engine/tools/standard_mht_chi2_fixture_check.cpp). Kernel parameters
 * are PAMGuard defaults; StandardMHTChi2Params are defaults (coastPenalty 10,
 * newTrackPenalty 50, newTrackN 3, maxICI 0.4, lowICIExponent 0.1,
 * longTrackExponent 0.1).
 */
public final class StandardMhtChi2FixtureExporter {

    private static final double SAMPLE_RATE = 48000.0;

    private static final class FixedDataUnit extends PamDataUnit {
        private final long nanoseconds;
        private final double amplitudeDb;
        private final double durationMs;

        FixedDataUnit(long nanoseconds, double amplitudeDb, double durationMs) {
            super(nanoseconds / 1_000_000L);
            this.nanoseconds = nanoseconds;
            this.amplitudeDb = amplitudeDb;
            this.durationMs = durationMs;
        }

        @Override
        public long getTimeNanoseconds() {
            return nanoseconds;
        }

        @Override
        public double getAmplitudeDB() {
            return amplitudeDb;
        }

        @Override
        public Double getDurationInMilliseconds() {
            return durationMs;
        }
    }

    private static final class FixedRateIdiManager extends IDIManager {
        @Override
        public double calcTime(PamDataUnit prev, PamDataUnit next) {
            double sampleDiff;
            if (next.getTimeNanoseconds() < prev.getTimeNanoseconds()) {
                sampleDiff = ((next.getTimeMilliseconds() - prev.getTimeMilliseconds()) / 1000.) * SAMPLE_RATE;
            }
            else {
                sampleDiff = ((next.getTimeNanoseconds() - prev.getTimeNanoseconds()) / 1E9) * SAMPLE_RATE;
            }
            return sampleDiff / SAMPLE_RATE;
        }
    }

    private static final class StackCase {
        String name;
        double[] timesMs;
        double[] amplitudesDb;
        double[] durationsMs;

        StackCase(String name, double[] timesMs, double[] amplitudesDb, double[] durationsMs) {
            this.name = name;
            this.timesMs = timesMs;
            this.amplitudesDb = amplitudesDb;
            this.durationsMs = durationsMs;
        }
    }

    private StandardMhtChi2FixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: StandardMhtChi2FixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,record,value1,value2,value3");
            for (StackCase stackCase : caseCatalogue()) {
                runCase(stackCase, writer);
            }
        }
    }

    private static StackCase[] caseCatalogue() {
        return new StackCase[]{
                new StackCase("steady-train-then-gap",
                        new double[]{1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700,
                                20000, 40000, 60000, 80000},
                        new double[]{120, 121, 120.5, 121.5, 120.8, 121.2, 120.6, 121.1,
                                100, 101, 100.5, 101.5},
                        new double[]{0.30, 0.32, 0.31, 0.33, 0.30, 0.32, 0.31, 0.30,
                                0.5, 0.52, 0.51, 0.50}),
                new StackCase("two-trains-amplitude-split",
                        new double[]{1000, 1050, 1100, 1150, 1200, 1250, 1300, 1350,
                                1400, 1450, 1500, 1550},
                        new double[]{120, 90, 121, 91, 120.5, 90.5, 121.5, 91.5,
                                120.8, 90.8, 121.2, 91.2},
                        new double[]{0.30, 0.60, 0.32, 0.62, 0.31, 0.61, 0.33, 0.63,
                                0.30, 0.60, 0.32, 0.62}),
        };
    }

    private static void runCase(StackCase stackCase, PrintWriter writer) throws Exception {
        StandardMHTChi2Params chi2Params = new StandardMHTChi2Params();
        // Enable only the ported variables in createChi2Vars order:
        // IDI, Amplitude, BearingDelta, Correlation, TimeDelayDelta, Length, PeakFrequency.
        chi2Params.enable = new boolean[]{true, true, false, false, false, true, false};
        chi2Params.useElectricNoiseFilter = false;
        chi2Params.useCorrelation = false;

        MHTKernelParams kernelParams = new MHTKernelParams();
        StandardMHTChi2Provider provider = new StandardMHTChi2Provider(chi2Params, kernelParams);

        Field idiField = StandardMHTChi2Provider.class.getDeclaredField("iDIManager");
        idiField.setAccessible(true);
        idiField.set(provider, new FixedRateIdiManager());

        MHTKernel<PamDataUnit> kernel = new MHTKernel<>(provider);
        kernel.setMHTParams(kernelParams);

        for (int k = 0; k < stackCase.timesMs.length; k++) {
            kernel.addDetection(new FixedDataUnit((long) (stackCase.timesMs[k] * 1_000_000.0),
                    stackCase.amplitudesDb[k], stackCase.durationsMs[k]));
            writer.printf(Locale.ROOT, "%s,step,%d,%d,%d%n",
                    stackCase.name, kernel.getKCount(), kernel.getPossibilityCount(), kernel.getNConfrimedTracks());
        }

        kernel.confirmRemainingTracks();
        writer.printf(Locale.ROOT, "%s,final,%d,%d,0%n",
                stackCase.name, kernel.getKCount(), kernel.getNConfrimedTracks());
        for (int i = 0; i < kernel.getNConfrimedTracks(); i++) {
            TrackBitSet<?> track = kernel.getConfirmedTrack(i);
            writer.printf(Locale.ROOT, "%s,confirmed,%s,%.17g,%d%n",
                    stackCase.name,
                    MHTKernel.bitSetString(track.trackBitSet, stackCase.timesMs.length),
                    track.getChi2(),
                    track.trackBitSet.cardinality());
        }
    }
}
