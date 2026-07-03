package org.pamguard.port.reference;

import PamguardMVC.PamDataUnit;
import clickTrainDetector.IDIInfo;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Exports click train IDI statistics fixtures by driving the real PAMGuard
 * clickTrainDetector.IDIInfo class with data units at controlled times.
 *
 * Scenario click times (in milliseconds) are shared by name with the C++
 * fixture check (cpp-engine/tools/click_train_idi_fixture_check.cpp).
 */
public final class ClickTrainIdiFixtureExporter {

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

    private static final class IdiScenario {
        String name;
        long[] clickTimesMs;

        IdiScenario(String name, long... clickTimesMs) {
            this.name = name;
            this.clickTimesMs = clickTimesMs;
        }
    }

    private ClickTrainIdiFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: ClickTrainIdiFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("scenario,nUnits,meanIdiSeconds,medianIdiSeconds,stdIdiSeconds");
            for (IdiScenario scenario : scenarioCatalogue()) {
                IDIInfo idiInfo = new IDIInfo();
                idiInfo.calcTimeSeriesData(dataUnits(scenario.clickTimesMs));
                writer.printf(Locale.ROOT, "%s,%d,%.17g,%.17g,%.17g%n",
                        scenario.name,
                        idiInfo.lastNumber,
                        idiInfo.meanIDI,
                        idiInfo.medianIDI,
                        idiInfo.stdIDI);
            }
        }
    }

    private static IdiScenario[] scenarioCatalogue() {
        return new IdiScenario[]{
                new IdiScenario("regular-100ms", 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700),
                new IdiScenario("jittered-even-idis", 1000, 1080, 1200, 1295, 1400, 1510, 1600),
                new IdiScenario("jittered-odd-idis", 1000, 1080, 1200, 1295, 1400, 1510, 1600, 1700),
                new IdiScenario("three-click-minimum", 1000, 1120, 1250),
                // Same click times as jittered-even-idis supplied out of order:
                // pins IDIInfo's internal nanosecond-time sort.
                new IdiScenario("unsorted-jittered-even-idis", 1400, 1000, 1600, 1080, 1295, 1510, 1200),
        };
    }

    private static List<PamDataUnit<?, ?>> dataUnits(long[] clickTimesMs) {
        List<PamDataUnit<?, ?>> units = new ArrayList<>();
        for (long timeMs : clickTimesMs) {
            units.add(new FixedTimeDataUnit(timeMs * 1_000_000L));
        }
        return units;
    }
}
