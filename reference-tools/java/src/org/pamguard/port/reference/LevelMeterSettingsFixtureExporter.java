package org.pamguard.port.reference;

import levelMeter.LevelMeterParams;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

/** Exports the pinned LevelMeterParams defaults and stored enum values. */
public final class LevelMeterSettingsFixtureExporter {

    private LevelMeterSettingsFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println(
                    "Usage: LevelMeterSettingsFixtureExporter <output.csv>");
            System.exit(2);
        }
        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        LevelMeterParams params = new LevelMeterParams();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println(
                    "minLevel,scaleReference,scaleType," +
                    "fullScaleValue,voltsValue,micropascalValue," +
                    "peakValue,rmsValue");
            writer.printf(
                    Locale.ROOT,
                    "%d,%d,%d,%d,%d,%d,%d,%d%n",
                    params.minLevel,
                    params.scaleReference,
                    params.scaleType,
                    LevelMeterParams.DISPLAY_FULLSCALE,
                    LevelMeterParams.DISPLAY_VOLTS,
                    LevelMeterParams.DISPLAY_MICROPASCAL,
                    LevelMeterParams.DISPLAY_PEAK,
                    LevelMeterParams.DISPLAY_RMS);
        }
    }
}
