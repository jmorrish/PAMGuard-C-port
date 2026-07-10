package org.pamguard.port.reference;

import clickTrainDetector.clickTrainAlgorithms.CTAlgorithmInfo;
import clickTrainDetector.clickTrainAlgorithms.mht.MHTChi2;
import clickTrainDetector.clickTrainAlgorithms.mht.MHTChi2Params;
import clickTrainDetector.clickTrainAlgorithms.mht.MHTChi2Provider;
import clickTrainDetector.clickTrainAlgorithms.mht.MHTKernel;
import clickTrainDetector.clickTrainAlgorithms.mht.MHTParams;
import clickTrainDetector.clickTrainAlgorithms.mht.TrackBitSet;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Locale;

/**
 * Exports MHT kernel fixtures driving the real PAMGuard MHTKernel with a
 * deterministic test chi2 implemented identically in the C++ check. This pins
 * the KERNEL semantics — branch growth, stable chi2-sorted pruning with
 * prefix deduplication, coast-based confirmation, junk-intersection removal,
 * and the all-coasts backstop — independently of StandardMHTChi2.
 *
 * Test chi2 (shared definition): over the included units' times,
 * sum((idi2-idi1)^2 / max(idi1*0.2, 5e-4)^2) / (nIncluded-2) for
 * nIncluded >= 3, plus 0.1 per excluded unit; recomputed from scratch on
 * every update. Coasts = kcount-1 minus the last included index (kcount when
 * nothing is included).
 *
 * Scenario times are shared by name with the C++ fixture check
 * (cpp-engine/tools/mht_kernel_fixture_check.cpp). Kernel parameters are the
 * PAMGuard defaults (nHold 20, nPruneback 4, nPruneBackStart 5, maxCoast 3).
 */
public final class MhtKernelFixtureExporter {

    private static final class TimeUnit {
        final long nanoseconds;

        TimeUnit(long nanoseconds) {
            this.nanoseconds = nanoseconds;
        }
    }

    private static final class TestChi2 implements MHTChi2<TimeUnit> {
        private final ArrayList<Long> allTimes;
        private double chi2 = 0.0;
        private int nCoasts = 0;

        TestChi2(ArrayList<Long> allTimes) {
            this.allTimes = allTimes;
        }

        @Override
        public double getChi2() {
            return chi2;
        }

        @Override
        public double getChi2(int pruneback) {
            return chi2;
        }

        @Override
        public int getNCoasts() {
            return nCoasts;
        }

        @Override
        public void update(TimeUnit detection0, TrackBitSet<TimeUnit> trackBitSet, int kcount) {
            ArrayList<Long> included = new ArrayList<>();
            int lastIncluded = -1;
            for (int i = 0; i < kcount; i++) {
                if (trackBitSet.trackBitSet.get(i)) {
                    included.add(allTimes.get(i));
                    lastIncluded = i;
                }
            }

            double value = 0.0;
            if (included.size() >= 3) {
                for (int j = 2; j < included.size(); j++) {
                    double idi1 = (included.get(j - 1) - included.get(j - 2)) / 1E9;
                    double idi2 = (included.get(j) - included.get(j - 1)) / 1E9;
                    value += Math.pow(idi2 - idi1, 2) / Math.pow(Math.max(idi1 * 0.2, 5E-4), 2);
                }
                value = value / (included.size() - 2);
            }
            value += 0.1 * (kcount - included.size());
            chi2 = value;
            nCoasts = lastIncluded < 0 ? kcount : kcount - 1 - lastIncluded;
        }

        @Override
        public MHTChi2<TimeUnit> cloneMHTChi2() {
            TestChi2 clone = new TestChi2(allTimes);
            clone.chi2 = chi2;
            clone.nCoasts = nCoasts;
            return clone;
        }

        @Override
        public void clear() {
            chi2 = 0.0;
            nCoasts = 0;
        }

        @Override
        public void clearKernelGarbage(int newRefIndex) {
        }

        @Override
        public CTAlgorithmInfo getMHTChi2Info() {
            return null;
        }

        @Override
        public void printSettings() {
        }
    }

    private static final class TestChi2Provider implements MHTChi2Provider<TimeUnit> {
        private final ArrayList<Long> allTimes = new ArrayList<>();

        @Override
        public void addDetection(TimeUnit detection, int kcount) {
            allTimes.add(detection.nanoseconds);
        }

        @Override
        public MHTChi2<TimeUnit> newMHTChi2(MHTChi2<TimeUnit> mhtChi2) {
            return new TestChi2(allTimes);
        }

        @Override
        public MHTChi2Params getSettingsObject() {
            return null;
        }

        @Override
        public void clear() {
            allTimes.clear();
        }

        @Override
        public void setMHTParams(MHTParams mhtParams) {
        }

        @Override
        public MHTChi2Params getChi2Params() {
            return null;
        }

        @Override
        public void printSettings() {
        }

        @Override
        public void clearKernelGarbage(int newRefIndex) {
        }
    }

    private static final class KernelCase {
        String name;
        double[] timesMs;

        KernelCase(String name, double[] timesMs) {
            this.name = name;
            this.timesMs = timesMs;
        }
    }

    private MhtKernelFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: MhtKernelFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,record,value1,value2,value3");
            for (KernelCase kernelCase : caseCatalogue()) {
                runCase(kernelCase, writer);
            }
        }
    }

    private static KernelCase[] caseCatalogue() {
        return new KernelCase[]{
                new KernelCase("single-train-then-gap", new double[]{
                        1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700,
                        20000, 40000, 60000, 80000}),
                new KernelCase("two-interleaved-trains", new double[]{
                        1000, 1040, 1100, 1170, 1200, 1300, 1400, 1430,
                        1500, 1560, 1600, 1690, 1700, 1820, 1800, 1950}),
        };
    }

    private static void runCase(KernelCase kernelCase, PrintWriter writer) {
        MHTKernel<TimeUnit> kernel = new MHTKernel<>(new TestChi2Provider());

        for (int k = 0; k < kernelCase.timesMs.length; k++) {
            kernel.addDetection(new TimeUnit((long) (kernelCase.timesMs[k] * 1_000_000.0)));
            writer.printf(Locale.ROOT, "%s,step,%d,%d,%d%n",
                    kernelCase.name, kernel.getKCount(), kernel.getPossibilityCount(), kernel.getNConfrimedTracks());
        }

        kernel.confirmRemainingTracks();
        writer.printf(Locale.ROOT, "%s,final,%d,%d,0%n",
                kernelCase.name, kernel.getKCount(), kernel.getNConfrimedTracks());
        for (int i = 0; i < kernel.getNConfrimedTracks(); i++) {
            TrackBitSet<?> track = kernel.getConfirmedTrack(i);
            writer.printf(Locale.ROOT, "%s,confirmed,%s,%.17g,%d%n",
                    kernelCase.name,
                    MHTKernel.bitSetString(track.trackBitSet, kernelCase.timesMs.length),
                    track.getChi2(),
                    track.trackBitSet.cardinality());
        }
    }
}
