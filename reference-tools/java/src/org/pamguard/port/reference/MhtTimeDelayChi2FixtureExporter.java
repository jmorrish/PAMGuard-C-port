package org.pamguard.port.reference;

import PamDetection.AbstractLocalisation;
import PamguardMVC.PamDataUnit;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.IDIManager;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.TimeDelayChi2Delta;

import java.io.File;
import java.io.PrintWriter;
import java.util.BitSet;
import java.util.Locale;

/**
 * Exports MHT time-delay chi2 fixtures driving the real PAMGuard
 * TimeDelayChi2Delta (defaults error 1e-6 s, minError 1e-9 s).
 *
 * Data units carry a minimal AbstractLocalisation subclass returning the
 * case's per-pair time delays; IDIManager.calcTime uses the same fixed-rate
 * subclass as the other MHT exporters.
 *
 * Case parameters are shared by name with the C++ fixture check
 * (cpp-engine/tools/mht_time_delay_chi2_fixture_check.cpp).
 */
public final class MhtTimeDelayChi2FixtureExporter {

    private static final double SAMPLE_RATE = 48000.0;

    private static final class FixedDelayUnit extends PamDataUnit {
        private final long nanoseconds;

        FixedDelayUnit(long nanoseconds, double[] delays) {
            super(nanoseconds / 1_000_000L);
            this.nanoseconds = nanoseconds;
            setLocalisation(new FixedLocalisation(this, delays));
        }

        @Override
        public long getTimeNanoseconds() {
            return nanoseconds;
        }
    }

    private static final class FixedLocalisation extends AbstractLocalisation {
        private final double[] delays;

        FixedLocalisation(PamDataUnit pamDataUnit, double[] delays) {
            super(pamDataUnit, 0, 0);
            this.delays = delays;
        }

        @Override
        public double[] getTimeDelays() {
            return delays;
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

    private static final class DelayCase {
        String name;
        double[] timesMs;
        double[][] delaysSeconds;

        DelayCase(String name, double[] timesMs, double[][] delaysSeconds) {
            this.name = name;
            this.timesMs = timesMs;
            this.delaysSeconds = delaysSeconds;
        }
    }

    private MhtTimeDelayChi2FixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: MhtTimeDelayChi2FixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,chi2");
            for (DelayCase delayCase : caseCatalogue()) {
                writer.printf(Locale.ROOT, "%s,%.17g%n", delayCase.name, run(delayCase));
            }
        }
    }

    private static DelayCase[] caseCatalogue() {
        double[] times = {1000, 1100, 1200, 1300, 1400};
        return new DelayCase[]{
                // Constant delays: every delta is zero.
                new DelayCase("constant-three-pairs", times, new double[][]{
                        {1e-4, 2e-4, 3e-4}, {1e-4, 2e-4, 3e-4}, {1e-4, 2e-4, 3e-4},
                        {1e-4, 2e-4, 3e-4}, {1e-4, 2e-4, 3e-4}}),
                // Steady drift on every pair: constant deltas, so zero chi2.
                new DelayCase("linear-drift", times, new double[][]{
                        {1e-4, 2e-4, 3e-4}, {1.1e-4, 2.1e-4, 3.1e-4}, {1.2e-4, 2.2e-4, 3.2e-4},
                        {1.3e-4, 2.3e-4, 3.3e-4}, {1.4e-4, 2.4e-4, 3.4e-4}}),
                // One pair is wild while the others are steady: the
                // drop-the-worst-pair rule should absorb it.
                new DelayCase("one-bad-pair", times, new double[][]{
                        {1e-4, 2e-4, 3e-4}, {1.1e-4, 2.1e-4, 9e-4}, {1.2e-4, 2.2e-4, 1e-5},
                        {1.3e-4, 2.3e-4, 8e-4}, {1.4e-4, 2.4e-4, 2e-5}}),
                // Two wild pairs: only one can be dropped, so chi2 is large.
                new DelayCase("two-bad-pairs", times, new double[][]{
                        {1e-4, 5e-4, 3e-4}, {1.1e-4, 1e-5, 9e-4}, {1.2e-4, 6e-4, 1e-5},
                        {1.3e-4, 2e-5, 8e-4}, {1.4e-4, 7e-4, 2e-5}}),
                // Single pair: the worst-pair subtraction removes the only
                // term, so chi2 stays zero regardless of the delays.
                new DelayCase("single-pair", times, new double[][]{
                        {1e-4}, {5e-4}, {2e-4}, {9e-4}, {3e-4}}),
        };
    }

    private static double run(DelayCase delayCase) {
        TimeDelayChi2Delta timeDelayChi2 = new TimeDelayChi2Delta();
        FixedRateIdiManager idiManager = new FixedRateIdiManager();
        BitSet bitSet = new BitSet();
        int bitcount = 0;
        double value = 0.0;
        for (int k = 0; k < delayCase.timesMs.length; k++) {
            bitSet.set(k, true);
            bitcount++;
            PamDataUnit unit = new FixedDelayUnit((long) (delayCase.timesMs[k] * 1_000_000.0),
                    delayCase.delaysSeconds[k]);
            value = timeDelayChi2.updateChi2(unit, bitSet, bitcount, k + 1, idiManager);
        }
        return value;
    }
}
