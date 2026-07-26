package org.pamguard.port.reference;

import Filters.FilterType;
import ltsa.LtsaParameters;
import noiseBandMonitor.BandType;
import noiseBandMonitor.NoiseBandSettings;
import noiseMonitor.NoiseSettings;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.Locale;

/**
 * Exports the default settings for the FFT Noise Monitor, Noise Band Monitor,
 * and LTSA from the pinned PAMGuard Java authority.
 *
 * <p>The portable defaults deliberately omit Java source-name persistence and
 * Noise Band plot preferences. Those values are emitted in separate evidence
 * objects so the port can prove what it excludes without mixing display state
 * into the scientific settings contract.</p>
 */
public final class NoiseLtsaSettingsFixtureExporter {

    private static final String EXPECTED_VERSION = "2.02.18e";
    private static final String EXPECTED_COMMIT =
            "dca55c81ef6f1498a8a3b926c69e7182afb915ee";

    private NoiseLtsaSettingsFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            System.err.println(
                    "Usage: NoiseLtsaSettingsFixtureExporter " +
                    "<output.json> <pamguard-version> <pamguard-commit>");
            System.exit(2);
        }
        Locale.setDefault(Locale.ROOT);
        require(EXPECTED_VERSION.equals(args[1]),
                "Expected PAMGuard " + EXPECTED_VERSION +
                ", found " + args[1]);
        require(EXPECTED_COMMIT.equals(args[2]),
                "Expected PAMGuard commit " + EXPECTED_COMMIT +
                ", found " + args[2]);

        NoiseSettings noise = new NoiseSettings();
        NoiseBandSettings noiseBand = new NoiseBandSettings();
        LtsaParameters ltsa = new LtsaParameters();
        requirePinnedDefaults(noise, noiseBand, ltsa);

        // Capture the serialized backing values before the lazy getters mutate
        // them into the effective scientific defaults.
        double minimumFrequencyBacking =
                doubleField(noiseBand, "minFrequency");
        double maximumFrequencyBacking =
                doubleField(noiseBand, "maxFrequency");
        double referenceFrequencyBacking =
                doubleField(noiseBand, "referenceFrequency");
        int startDecimation = intField(noiseBand, "startDecimation");
        int endDecimation = intField(noiseBand, "endDecimation");
        int lowBandNumber = intField(noiseBand, "lowBandNumber");
        int highBandNumber = intField(noiseBand, "highBandNumber");

        double maximumFrequencyHz = noiseBand.getMaxFrequency();
        double minimumFrequencyHz = noiseBand.getMinFrequency();
        double referenceFrequencyHz = noiseBand.getReferenceFrequency();

        File output = new File(args[0]);
        File parent = output.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("{");
            writer.println("  \"authority\": {");
            writer.printf(
                    Locale.ROOT,
                    "    \"version\": %s,%n",
                    jsonString(args[1]));
            writer.printf(
                    Locale.ROOT,
                    "    \"commit\": %s,%n",
                    jsonString(args[2]));
            writer.println(
                    "    \"exporter\": " +
                    "\"org.pamguard.port.reference." +
                    "NoiseLtsaSettingsFixtureExporter\"");
            writer.println("  },");
            writePortableDefaults(
                    writer,
                    noise,
                    noiseBand,
                    ltsa,
                    minimumFrequencyHz,
                    maximumFrequencyHz,
                    referenceFrequencyHz);
            writer.println(",");
            writeJavaPersistedScientificDefaults(
                    writer,
                    noise,
                    noiseBand,
                    ltsa,
                    minimumFrequencyBacking,
                    maximumFrequencyBacking,
                    referenceFrequencyBacking,
                    startDecimation,
                    endDecimation,
                    lowBandNumber,
                    highBandNumber);
            writer.println(",");
            writeSourceDefaults(writer, noise, noiseBand, ltsa);
            writer.println(",");
            writeLazyEvidence(
                    writer,
                    minimumFrequencyBacking,
                    maximumFrequencyBacking,
                    referenceFrequencyBacking,
                    minimumFrequencyHz,
                    maximumFrequencyHz,
                    referenceFrequencyHz);
            writer.println(",");
            writeDisplayDefaults(writer, noiseBand);
            writer.println();
            writer.println("}");
        }
    }

    private static void writePortableDefaults(
            PrintWriter writer,
            NoiseSettings noise,
            NoiseBandSettings noiseBand,
            LtsaParameters ltsa,
            double minimumFrequencyHz,
            double maximumFrequencyHz,
            double referenceFrequencyHz) {
        writer.println("  \"portableSettingsDefaults\": {");
        writer.println("    \"noiseMonitor\": {");
        writer.printf(
                Locale.ROOT,
                "      \"channelBitmap\": %d,%n",
                noise.channelBitmap);
        writer.printf(
                Locale.ROOT,
                "      \"measurementIntervalSeconds\": %d,%n",
                noise.measurementIntervalSeconds);
        writer.printf(
                Locale.ROOT,
                "      \"nMeasures\": %d,%n",
                noise.nMeasures);
        writer.printf(
                Locale.ROOT,
                "      \"useAll\": %s,%n",
                Boolean.toString(noise.useAll));
        writer.println("      \"bands\": []");
        writer.println("    },");
        writer.println("    \"noiseBandMonitor\": {");
        writer.printf(
                Locale.ROOT,
                "      \"channelBitmap\": %d,%n",
                noiseBand.channelMap);
        writer.println("      \"bandType\": \"thirdOctave\",");
        writer.println("      \"filterType\": \"butterworth\",");
        writer.printf(
                Locale.ROOT,
                "      \"iirOrder\": %d,%n",
                noiseBand.iirOrder);
        writer.printf(
                Locale.ROOT,
                "      \"firOrder\": %d,%n",
                noiseBand.firOrder);
        writer.printf(
                Locale.ROOT,
                "      \"firGamma\": %s,%n",
                Double.toString(noiseBand.firGamma));
        writer.printf(
                Locale.ROOT,
                "      \"outputIntervalSeconds\": %d,%n",
                noiseBand.outputIntervalSeconds);
        writer.printf(
                Locale.ROOT,
                "      \"minimumFrequencyHz\": %s,%n",
                Double.toString(minimumFrequencyHz));
        writer.printf(
                Locale.ROOT,
                "      \"maximumFrequencyHz\": %s,%n",
                Double.toString(maximumFrequencyHz));
        writer.printf(
                Locale.ROOT,
                "      \"referenceFrequencyHz\": %s%n",
                Double.toString(referenceFrequencyHz));
        writer.println("    },");
        writer.println("    \"ltsa\": {");
        writer.printf(
                Locale.ROOT,
                "      \"channelBitmap\": %d,%n",
                ltsa.channelMap);
        writer.printf(
                Locale.ROOT,
                "      \"intervalSeconds\": %d,%n",
                ltsa.intervalSeconds);
        writer.printf(
                Locale.ROOT,
                "      \"longerFactor\": %d%n",
                ltsa.longerFactor);
        writer.println("    }");
        writer.print("  }");
    }

    private static void writeJavaPersistedScientificDefaults(
            PrintWriter writer,
            NoiseSettings noise,
            NoiseBandSettings noiseBand,
            LtsaParameters ltsa,
            double minimumFrequencyBacking,
            double maximumFrequencyBacking,
            double referenceFrequencyBacking,
            int startDecimation,
            int endDecimation,
            int lowBandNumber,
            int highBandNumber) {
        writer.println("  \"javaPersistedScientificDefaults\": {");
        writer.println("    \"noiseMonitor.NoiseSettings\": {");
        writer.printf(
                Locale.ROOT,
                "      \"serialVersionUID\": %d,%n",
                NoiseSettings.serialVersionUID);
        writer.printf(
                Locale.ROOT,
                "      \"channelBitmap\": %d,%n",
                noise.channelBitmap);
        writer.printf(
                Locale.ROOT,
                "      \"measurementIntervalSeconds\": %d,%n",
                noise.measurementIntervalSeconds);
        writer.printf(
                Locale.ROOT,
                "      \"nMeasures\": %d,%n",
                noise.nMeasures);
        writer.printf(
                Locale.ROOT,
                "      \"useAll\": %s,%n",
                Boolean.toString(noise.useAll));
        writer.println("      \"measurementBands\": []");
        writer.println("    },");
        writer.println(
                "    \"noiseBandMonitor.NoiseBandSettings\": {");
        writer.printf(
                Locale.ROOT,
                "      \"serialVersionUID\": %d,%n",
                NoiseBandSettings.serialVersionUID);
        writer.printf(
                Locale.ROOT,
                "      \"channelMap\": %d,%n",
                noiseBand.channelMap);
        writer.printf(
                Locale.ROOT,
                "      \"bandType\": %s,%n",
                jsonString(noiseBand.bandType.name()));
        writer.printf(
                Locale.ROOT,
                "      \"filterType\": %s,%n",
                jsonString(noiseBand.filterType.name()));
        writer.printf(
                Locale.ROOT,
                "      \"iirOrder\": %d,%n",
                noiseBand.iirOrder);
        writer.printf(
                Locale.ROOT,
                "      \"firOrder\": %d,%n",
                noiseBand.firOrder);
        writer.printf(
                Locale.ROOT,
                "      \"firGamma\": %s,%n",
                Double.toString(noiseBand.firGamma));
        writer.printf(
                Locale.ROOT,
                "      \"outputIntervalSeconds\": %d,%n",
                noiseBand.outputIntervalSeconds);
        writer.printf(
                Locale.ROOT,
                "      \"minFrequency\": %s,%n",
                Double.toString(minimumFrequencyBacking));
        writer.printf(
                Locale.ROOT,
                "      \"maxFrequency\": %s,%n",
                Double.toString(maximumFrequencyBacking));
        writer.printf(
                Locale.ROOT,
                "      \"referenceFrequency\": %s,%n",
                Double.toString(referenceFrequencyBacking));
        writer.printf(
                Locale.ROOT,
                "      \"startDecimation\": %d,%n",
                startDecimation);
        writer.printf(
                Locale.ROOT,
                "      \"endDecimation\": %d,%n",
                endDecimation);
        writer.printf(
                Locale.ROOT,
                "      \"lowBandNumber\": %d,%n",
                lowBandNumber);
        writer.printf(
                Locale.ROOT,
                "      \"highBandNumber\": %d%n",
                highBandNumber);
        writer.println("    },");
        writer.println("    \"ltsa.LtsaParameters\": {");
        writer.printf(
                Locale.ROOT,
                "      \"serialVersionUID\": %d,%n",
                LtsaParameters.serialVersionUID);
        writer.printf(
                Locale.ROOT,
                "      \"channelMap\": %d,%n",
                ltsa.channelMap);
        writer.printf(
                Locale.ROOT,
                "      \"intervalSeconds\": %d,%n",
                ltsa.intervalSeconds);
        writer.printf(
                Locale.ROOT,
                "      \"longerFactor\": %d%n",
                ltsa.longerFactor);
        writer.println("    }");
        writer.print("  }");
    }

    private static void writeSourceDefaults(
            PrintWriter writer,
            NoiseSettings noise,
            NoiseBandSettings noiseBand,
            LtsaParameters ltsa) {
        writer.println("  \"javaPersistedSourceDefaults\": {");
        writer.println("    \"noiseMonitor.NoiseSettings\": {");
        writer.printf(
                Locale.ROOT,
                "      \"dataSource\": %s%n",
                nullableJsonString(noise.dataSource));
        writer.println("    },");
        writer.println(
                "    \"noiseBandMonitor.NoiseBandSettings\": {");
        writer.printf(
                Locale.ROOT,
                "      \"rawDataSource\": %s%n",
                nullableJsonString(noiseBand.rawDataSource));
        writer.println("    },");
        writer.println("    \"ltsa.LtsaParameters\": {");
        writer.printf(
                Locale.ROOT,
                "      \"dataSource\": %s%n",
                nullableJsonString(ltsa.dataSource));
        writer.println("    }");
        writer.print("  }");
    }

    private static void writeLazyEvidence(
            PrintWriter writer,
            double minimumFrequencyBacking,
            double maximumFrequencyBacking,
            double referenceFrequencyBacking,
            double minimumFrequencyHz,
            double maximumFrequencyHz,
            double referenceFrequencyHz) {
        writer.println("  \"javaLazyBackingEvidence\": {");
        writer.println(
                "    \"noiseBandMonitor.NoiseBandSettings\": {");
        writer.printf(
                Locale.ROOT,
                "      \"minFrequencyBeforeGetter\": %s,%n",
                Double.toString(minimumFrequencyBacking));
        writer.printf(
                Locale.ROOT,
                "      \"maxFrequencyBeforeGetter\": %s,%n",
                Double.toString(maximumFrequencyBacking));
        writer.printf(
                Locale.ROOT,
                "      \"referenceFrequencyBeforeGetter\": %s,%n",
                Double.toString(referenceFrequencyBacking));
        writer.printf(
                Locale.ROOT,
                "      \"minimumFrequencyHzFromGetter\": %s,%n",
                Double.toString(minimumFrequencyHz));
        writer.printf(
                Locale.ROOT,
                "      \"maximumFrequencyHzFromGetter\": %s,%n",
                Double.toString(maximumFrequencyHz));
        writer.printf(
                Locale.ROOT,
                "      \"referenceFrequencyHzFromGetter\": %s%n",
                Double.toString(referenceFrequencyHz));
        writer.println("    }");
        writer.print("  }");
    }

    private static void writeDisplayDefaults(
            PrintWriter writer,
            NoiseBandSettings noiseBand) {
        writer.println("  \"excludedDisplayDefaults\": {");
        writer.println(
                "    \"noiseBandMonitor.NoiseBandSettings\": {");
        writer.printf(
                Locale.ROOT,
                "      \"logFreqScale\": %s,%n",
                Boolean.toString(noiseBand.logFreqScale));
        writer.printf(
                Locale.ROOT,
                "      \"showGrid\": %s,%n",
                Boolean.toString(noiseBand.showGrid));
        writer.printf(
                Locale.ROOT,
                "      \"showDecimators\": %s,%n",
                Boolean.toString(noiseBand.showDecimators));
        writer.print("      \"showStandard\": [");
        for (int index = 0; index < 3; index++) {
            if (index > 0) {
                writer.print(", ");
            }
            writer.print(
                    Boolean.toString(noiseBand.getShowStandard(index)));
        }
        writer.println("],");
        writer.printf(
                Locale.ROOT,
                "      \"scaleToggleState\": %d%n",
                noiseBand.scaleToggleState);
        writer.println("    }");
        writer.print("  }");
    }

    private static void requirePinnedDefaults(
            NoiseSettings noise,
            NoiseBandSettings noiseBand,
            LtsaParameters ltsa) throws Exception {
        require(NoiseSettings.serialVersionUID == 1L,
                "NoiseSettings serialVersionUID changed");
        require(noise.dataSource == null,
                "NoiseSettings.dataSource default changed");
        require(noise.channelBitmap == 1,
                "NoiseSettings.channelBitmap default changed");
        require(noise.measurementIntervalSeconds == 60,
                "NoiseSettings measurement interval default changed");
        require(noise.nMeasures == 100,
                "NoiseSettings nMeasures default changed");
        require(noise.useAll,
                "NoiseSettings useAll default changed");
        require(noise.getNumMeasurementBands() == 0,
                "NoiseSettings measurement bands are no longer empty");

        require(NoiseBandSettings.serialVersionUID == 1L,
                "NoiseBandSettings serialVersionUID changed");
        require(noiseBand.rawDataSource == null,
                "NoiseBandSettings.rawDataSource default changed");
        require(noiseBand.channelMap == 1,
                "NoiseBandSettings.channelMap default changed");
        require(noiseBand.bandType == BandType.THIRDOCTAVE,
                "NoiseBandSettings band type default changed");
        require(noiseBand.filterType == FilterType.BUTTERWORTH,
                "NoiseBandSettings filter type default changed");
        require(noiseBand.iirOrder == 6,
                "NoiseBandSettings IIR order default changed");
        require(noiseBand.firOrder == 7,
                "NoiseBandSettings FIR order default changed");
        require(noiseBand.firGamma == 2.5,
                "NoiseBandSettings FIR gamma default changed");
        require(noiseBand.outputIntervalSeconds == 10,
                "NoiseBandSettings output interval default changed");
        require(noiseBand.logFreqScale,
                "NoiseBandSettings log-frequency display default changed");
        require(noiseBand.showGrid,
                "NoiseBandSettings grid display default changed");
        require(noiseBand.showDecimators,
                "NoiseBandSettings decimator display default changed");
        require(noiseBand.scaleToggleState == 0,
                "NoiseBandSettings scale toggle default changed");
        for (int index = 0; index < 3; index++) {
            require(!noiseBand.getShowStandard(index),
                    "NoiseBandSettings ANSI display default changed");
        }

        require(LtsaParameters.serialVersionUID == 1L,
                "LtsaParameters serialVersionUID changed");
        require(ltsa.dataSource == null,
                "LtsaParameters.dataSource default changed");
        require(ltsa.channelMap == 0,
                "LtsaParameters.channelMap default changed");
        require(ltsa.intervalSeconds == 60,
                "LtsaParameters interval default changed");
        require(ltsa.longerFactor == 10,
                "LtsaParameters longer factor default changed");
    }

    private static int intField(
            Object instance,
            String name) throws Exception {
        return ((Number) field(instance, name)).intValue();
    }

    private static double doubleField(
            Object instance,
            String name) throws Exception {
        return ((Number) field(instance, name)).doubleValue();
    }

    private static Object field(
            Object instance,
            String name) throws Exception {
        Field field = instance.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field.get(instance);
    }

    private static String nullableJsonString(String value) {
        return value == null ? "null" : jsonString(value);
    }

    private static String jsonString(String value) {
        StringBuilder encoded = new StringBuilder("\"");
        for (int index = 0; index < value.length(); index++) {
            char character = value.charAt(index);
            switch (character) {
                case '"':
                    encoded.append("\\\"");
                    break;
                case '\\':
                    encoded.append("\\\\");
                    break;
                case '\b':
                    encoded.append("\\b");
                    break;
                case '\f':
                    encoded.append("\\f");
                    break;
                case '\n':
                    encoded.append("\\n");
                    break;
                case '\r':
                    encoded.append("\\r");
                    break;
                case '\t':
                    encoded.append("\\t");
                    break;
                default:
                    if (character < 0x20) {
                        encoded.append(
                                String.format(
                                        Locale.ROOT,
                                        "\\u%04x",
                                        (int) character));
                    }
                    else {
                        encoded.append(character);
                    }
            }
        }
        return encoded.append('"').toString();
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
