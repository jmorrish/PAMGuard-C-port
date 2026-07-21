package org.pamguard.port.reference;

import PamDetection.AbstractLocalisation;
import PamguardMVC.PamDataUnit;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.BearingChi2Delta;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.BearingChi2VarParams;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.IDIManager;

import java.io.File;
import java.io.PrintWriter;
import java.util.BitSet;
import java.util.Locale;

/**
 * Exports MHT bearing chi2 fixtures driving the real PAMGuard
 * BearingChi2Delta (SimpleChi2VarDelta path; defaults error 4 degrees,
 * minError 2 degrees, bearing jump disabled at 20 degrees POSITIVE).
 *
 * Data units carry a minimal AbstractLocalisation subclass whose getAngles()
 * returns the case bearing in radians; IDIManager.calcTime uses the same
 * fixed-rate subclass as the other MHT exporters.
 *
 * Case parameters are shared by name with the C++ fixture check
 * (cpp-engine/tools/mht_bearing_chi2_fixture_check.cpp).
 */
public final class MhtBearingChi2FixtureExporter {

    private static final double SAMPLE_RATE = 48000.0;

    private static final class FixedBearingUnit extends PamDataUnit {
        private final long nanoseconds;

        FixedBearingUnit(long nanoseconds, double bearingRadians) {
            super(nanoseconds / 1_000_000L);
            this.nanoseconds = nanoseconds;
            setLocalisation(new FixedLocalisation(this, bearingRadians));
        }

        @Override
        public long getTimeNanoseconds() {
            return nanoseconds;
        }
    }

    private static final class FixedLocalisation extends AbstractLocalisation {
        private final double bearingRadians;

        FixedLocalisation(PamDataUnit pamDataUnit, double bearingRadians) {
            super(pamDataUnit, 0, 0);
            this.bearingRadians = bearingRadians;
        }

        @Override
        public double[] getAngles() {
            return new double[]{bearingRadians};
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

    private static final class BearingCase {
        String name;
        double[] timesMs;
        double[] bearingsDeg;
        boolean jumpEnable;

        BearingCase(String name, double[] timesMs, double[] bearingsDeg, boolean jumpEnable) {
            this.name = name;
            this.timesMs = timesMs;
            this.bearingsDeg = bearingsDeg;
            this.jumpEnable = jumpEnable;
        }
    }

    private MhtBearingChi2FixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: MhtBearingChi2FixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,chi2");
            for (BearingCase bearingCase : caseCatalogue()) {
                writer.printf(Locale.ROOT, "%s,%.17g%n", bearingCase.name, run(bearingCase));
            }
        }
    }

    private static BearingCase[] caseCatalogue() {
        double[] times = {1000, 1100, 1200, 1300, 1400, 1500};
        return new BearingCase[]{
                // Constant bearing: deltas are all zero.
                new BearingCase("constant", times, new double[]{40, 40, 40, 40, 40, 40}, false),
                // Linear sweep: constant delta, so the delta-of-deltas is zero.
                new BearingCase("linear-sweep", times, new double[]{10, 14, 18, 22, 26, 30}, false),
                // Irregular bearings exercise the delta chi2 accumulation.
                new BearingCase("irregular", times, new double[]{10, 14, 25, 27, 40, 44}, false),
                // Crossing zero: pins the wraparound-aware minimum difference.
                new BearingCase("wraparound", times, new double[]{350, 355, 2, 7, 12, 17}, false),
                // Large jumps with the jump penalty enabled (POSITIVE direction default).
                new BearingCase("jump-penalty", times, new double[]{10, 14, 60, 64, 110, 114}, true),
        };
    }

    private static double run(BearingCase bearingCase) {
        BearingChi2Delta bearingChi2 = new BearingChi2Delta();
        BearingChi2VarParams params = (BearingChi2VarParams) bearingChi2.getSimpleChiVarParams();
        params.bearingJumpEnable = bearingCase.jumpEnable;
        bearingChi2.setSimpleChiVarParams(params);

        FixedRateIdiManager idiManager = new FixedRateIdiManager();
        BitSet bitSet = new BitSet();
        int bitcount = 0;
        double value = 0.0;
        for (int k = 0; k < bearingCase.timesMs.length; k++) {
            bitSet.set(k, true);
            bitcount++;
            PamDataUnit unit = new FixedBearingUnit((long) (bearingCase.timesMs[k] * 1_000_000.0),
                    Math.toRadians(bearingCase.bearingsDeg[k]));
            value = bearingChi2.updateChi2(unit, bitSet, bitcount, k + 1, idiManager);
        }
        return value;
    }
}
