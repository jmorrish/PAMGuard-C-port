package org.pamguard.port.reference;

import PamguardMVC.PamDataUnit;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.AmplitudeChi2;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.IDIManager;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.LengthChi2;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.MHTChi2Var;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.BitSet;
import java.util.Locale;

/**
 * Exports MHT length and amplitude chi2 fixtures driving the real PAMGuard
 * LengthChi2 (SimpleChi2Var path; defaults error 0.2, minError 0.002) and
 * AmplitudeChi2 (SimpleChi2VarDelta path; defaults error 30, minError 1,
 * amplitude jump penalty above 10 dB) classes.
 *
 * Data units subclass PamDataUnit with fixed nanosecond times, amplitudes,
 * and millisecond durations; IDIManager.calcTime uses the same fixed-rate
 * subclass as the IDI chi2 exporter.
 *
 * Case parameters are shared by name with the C++ fixture check
 * (cpp-engine/tools/mht_simple_chi2_vars_fixture_check.cpp).
 */
public final class MhtSimpleChi2VarsFixtureExporter {

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

    private static final class VarCase {
        String name;
        String variable;
        boolean update;
        double[] timesMs;
        double[] amplitudesDb;
        double[] durationsMs;

        VarCase(String name, String variable, boolean update, double[] timesMs,
                double[] amplitudesDb, double[] durationsMs) {
            this.name = name;
            this.variable = variable;
            this.update = update;
            this.timesMs = timesMs;
            this.amplitudesDb = amplitudesDb;
            this.durationsMs = durationsMs;
        }
    }

    private MhtSimpleChi2VarsFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: MhtSimpleChi2VarsFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,chi2");
            for (VarCase varCase : caseCatalogue()) {
                double value = run(varCase);
                writer.printf(Locale.ROOT, "%s,%.17g%n", varCase.name, value);
            }
        }
    }

    private static VarCase[] caseCatalogue() {
        double[] steadyTimes = {1000, 1100, 1200, 1300, 1400, 1500};
        double[] steadyAmps = {120, 121, 120.5, 121.5, 120.8, 121.2};
        double[] rampAmps = {110, 114, 118, 122, 126, 130};
        double[] jumpAmps = {120, 121, 120.5, 135, 120.8, 121.2};
        double[] steadyLengths = {0.30, 0.32, 0.31, 0.33, 0.30, 0.32};
        double[] wildLengths = {0.30, 0.90, 0.25, 1.10, 0.20, 0.95};
        return new VarCase[]{
                new VarCase("length-steady-calc", "length", false, steadyTimes, steadyAmps, steadyLengths),
                new VarCase("length-wild-calc", "length", false, steadyTimes, steadyAmps, wildLengths),
                new VarCase("length-steady-update", "length", true, steadyTimes, steadyAmps, steadyLengths),
                new VarCase("length-wild-update", "length", true, steadyTimes, steadyAmps, wildLengths),
                new VarCase("amplitude-steady-update", "amplitude", true, steadyTimes, steadyAmps, steadyLengths),
                new VarCase("amplitude-ramp-update", "amplitude", true, steadyTimes, rampAmps, steadyLengths),
                new VarCase("amplitude-jump-update", "amplitude", true, steadyTimes, jumpAmps, steadyLengths),
                new VarCase("amplitude-steady-calc", "amplitude", false, steadyTimes, steadyAmps, steadyLengths),
        };
    }

    @SuppressWarnings("rawtypes")
    private static double run(VarCase varCase) {
        MHTChi2Var<PamDataUnit> chi2Var = varCase.variable.equals("length") ? new LengthChi2() : new AmplitudeChi2();
        FixedRateIdiManager idiManager = new FixedRateIdiManager();

        ArrayList<PamDataUnit> units = new ArrayList<>();
        for (int i = 0; i < varCase.timesMs.length; i++) {
            units.add(new FixedDataUnit((long) (varCase.timesMs[i] * 1_000_000.0),
                    varCase.amplitudesDb[i], varCase.durationsMs[i]));
        }

        if (!varCase.update) {
            return chi2Var.calcChi2(units, idiManager);
        }

        BitSet bitSet = new BitSet();
        int bitcount = 0;
        double value = 0.0;
        for (int k = 0; k < units.size(); k++) {
            bitSet.set(k, true);
            bitcount++;
            value = chi2Var.updateChi2(units.get(k), bitSet, bitcount, k + 1, idiManager);
        }
        return value;
    }
}
