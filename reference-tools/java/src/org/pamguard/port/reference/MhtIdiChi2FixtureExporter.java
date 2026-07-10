package org.pamguard.port.reference;

import PamguardMVC.PamDataUnit;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.IDIChi2;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.IDIManager;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.BitSet;
import java.util.Locale;

/**
 * Exports MHT IDI chi2 fixtures driving the real PAMGuard
 * clickTrainDetector...mhtvar.IDIChi2 class (default IDIChi2Params: error
 * 0.2, minError 0.0005, minIDI 0.0005; junk track penalty 2e7).
 *
 * Data units use a fixed-nanosecond subclass; IDIManager.calcTime needs a
 * parent data block's sample rate, so a subclass reproduces calcTimeSR's two
 * timing branches with a constant sample rate and no correlation correction
 * (useCorrelation defaults off).
 *
 * Case parameters are shared by name with the C++ fixture check
 * (cpp-engine/tools/mht_idi_chi2_fixture_check.cpp).
 */
public final class MhtIdiChi2FixtureExporter {

    private static final double SAMPLE_RATE = 48000.0;

    private static final class FixedTimeDataUnit extends PamDataUnit {
        private final long nanoseconds;

        FixedTimeDataUnit(long nanoseconds) {
            super(nanoseconds / 1_000_000L);
            this.nanoseconds = nanoseconds;
        }

        @Override
        public long getTimeNanoseconds() {
            return nanoseconds;
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

    private static final class Chi2Case {
        String name;
        boolean update;
        double[] timesMs;
        boolean[] inTrack;

        Chi2Case(String name, boolean update, double[] timesMs, boolean[] inTrack) {
            this.name = name;
            this.update = update;
            this.timesMs = timesMs;
            this.inTrack = inTrack;
        }
    }

    private MhtIdiChi2FixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: MhtIdiChi2FixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,chi2");
            for (Chi2Case chi2Case : caseCatalogue()) {
                double value = chi2Case.update ? runUpdateCase(chi2Case) : runCalcCase(chi2Case);
                writer.printf(Locale.ROOT, "%s,%.17g%n", chi2Case.name, value);
            }
        }
    }

    private static Chi2Case[] caseCatalogue() {
        boolean[] allIn8 = {true, true, true, true, true, true, true, true};
        boolean[] skipFourth = {true, true, true, false, true, true, true, true};
        return new Chi2Case[]{
                new Chi2Case("steady-calc", false,
                        new double[]{1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700}, null),
                new Chi2Case("jittered-calc", false,
                        new double[]{1000, 1100, 1205, 1295, 1405, 1500}, null),
                new Chi2Case("steady-update", true,
                        new double[]{1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700}, allIn8),
                new Chi2Case("jittered-update", true,
                        new double[]{1000, 1100, 1205, 1295, 1405, 1500, 1610, 1700}, allIn8),
                new Chi2Case("skip-update", true,
                        new double[]{1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700}, skipFourth),
                new Chi2Case("junk-idi-update", true,
                        new double[]{1000, 1100, 1200, 1200.2, 1300, 1400, 1500, 1600}, allIn8),
        };
    }

    private static double runCalcCase(Chi2Case chi2Case) {
        IDIChi2 idiChi2 = new IDIChi2();
        ArrayList<PamDataUnit> units = new ArrayList<>();
        for (double timeMs : chi2Case.timesMs) {
            units.add(new FixedTimeDataUnit((long) (timeMs * 1_000_000.0)));
        }
        return idiChi2.calcChi2(units, new FixedRateIdiManager());
    }

    private static double runUpdateCase(Chi2Case chi2Case) {
        IDIChi2 idiChi2 = new IDIChi2();
        FixedRateIdiManager idiManager = new FixedRateIdiManager();
        BitSet bitSet = new BitSet();
        int bitcount = 0;
        double value = 0.0;
        for (int k = 0; k < chi2Case.timesMs.length; k++) {
            bitSet.set(k, chi2Case.inTrack[k]);
            if (chi2Case.inTrack[k]) {
                bitcount++;
            }
            value = idiChi2.updateChi2(new FixedTimeDataUnit((long) (chi2Case.timesMs[k] * 1_000_000.0)),
                    bitSet, bitcount, k + 1, idiManager);
        }
        return value;
    }
}
