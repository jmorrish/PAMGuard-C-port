package org.pamguard.port.reference;

import IshmaelDetector.EnergySumParams;
import IshmaelDetector.IshDetParams;
import IshmaelDetector.MatchFiltParams;
import IshmaelDetector.SgramCorrParams;
import PamView.GroupedSourceParameters;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Exports constructor settings for all three live Ishmael controlled units
 * from the pinned PAMGuard Java authority.
 *
 * <p>Source block names are graph bindings in the portable project. Java null
 * group arrays are normalized to empty arrays. Match-filter waveform samples
 * are empty at construction because the Java process only reads them after an
 * operator chooses a kernel file.</p>
 */
public final class IshmaelSettingsFixtureExporter {

    private static final String EXPECTED_VERSION = "2.02.18e";
    private static final String EXPECTED_COMMIT =
            "dca55c81ef6f1498a8a3b926c69e7182afb915ee";

    private IshmaelSettingsFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            System.err.println(
                    "Usage: IshmaelSettingsFixtureExporter " +
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

        EnergySumParams energy = new EnergySumParams();
        SgramCorrParams sgram = new SgramCorrParams();
        MatchFiltParams match = new MatchFiltParams();
        requirePinnedDefaults(energy, sgram, match);

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
                    "IshmaelSettingsFixtureExporter\"");
            writer.println("  },");
            writer.println("  \"portableSettingsDefaults\": {");
            writeEnergy(writer, energy);
            writer.println(",");
            writeSgram(writer, sgram);
            writer.println(",");
            writeMatch(writer, match);
            writer.println();
            writer.println("  },");
            writeJavaBackingEvidence(writer, energy, sgram, match);
            writer.println(",");
            writeExcludedEvidence(writer, energy, sgram, match);
            writer.println();
            writer.println("}");
        }
    }

    private static void writeCommon(
            PrintWriter writer,
            IshDetParams params,
            String indent) {
        GroupedSourceParameters source = params.groupedSourceParmas;
        writer.printf(
                Locale.ROOT,
                "%s\"channelBitmap\": %d,%n",
                indent,
                source.getChanOrSeqBitmap());
        writer.printf(
                "%s\"groupingType\": \"all\",%n",
                indent);
        writer.printf(
                "%s\"channelGroups\": [],%n",
                indent);
        writer.printf(
                Locale.ROOT,
                "%s\"threshold\": %s,%n",
                indent,
                Double.toString(params.thresh));
        writer.printf(
                Locale.ROOT,
                "%s\"minTimeSeconds\": %s,%n",
                indent,
                Double.toString(params.minTime));
        writer.printf(
                Locale.ROOT,
                "%s\"maxTimeSeconds\": %s,%n",
                indent,
                Double.toString(params.maxTime));
        writer.printf(
                Locale.ROOT,
                "%s\"refractoryTimeSeconds\": %s,%n",
                indent,
                Double.toString(params.refractoryTime));
    }

    private static void writeEnergy(
            PrintWriter writer,
            EnergySumParams params) {
        writer.println("    \"energySum\": {");
        writeCommon(writer, params, "      ");
        writer.printf(
                Locale.ROOT,
                "      \"f0Hz\": %s,%n",
                Double.toString(params.f0));
        writer.printf(
                Locale.ROOT,
                "      \"f1Hz\": %s,%n",
                Double.toString(params.f1));
        writer.printf(
                Locale.ROOT,
                "      \"ratioF0Hz\": %s,%n",
                Double.toString(params.ratiof0));
        writer.printf(
                Locale.ROOT,
                "      \"ratioF1Hz\": %s,%n",
                Double.toString(params.ratiof1));
        writer.printf(
                "      \"useRatio\": %s,%n",
                Boolean.toString(params.useRatio));
        writer.printf(
                "      \"adaptiveThreshold\": %s,%n",
                Boolean.toString(params.adaptiveThreshold));
        writer.printf(
                Locale.ROOT,
                "      \"longFilter\": %s,%n",
                Double.toString(params.longFilter));
        writer.printf(
                "      \"useLog\": %s,%n",
                Boolean.toString(params.useLog));
        writer.printf(
                Locale.ROOT,
                "      \"spikeDecay\": %s,%n",
                Double.toString(params.spikeDecay));
        writer.printf(
                "      \"outputSmoothing\": %s,%n",
                Boolean.toString(params.outPutSmoothing));
        writer.printf(
                Locale.ROOT,
                "      \"shortFilter\": %s%n",
                Double.toString(params.shortFilter));
        writer.print("    }");
    }

    private static void writeSgram(
            PrintWriter writer,
            SgramCorrParams params) {
        writer.println("    \"spectrogramCorrelation\": {");
        writeCommon(writer, params, "      ");
        writer.println("      \"segments\": [],");
        writer.printf(
                Locale.ROOT,
                "      \"spreadHz\": %s,%n",
                Double.toString(params.spread));
        writer.printf(
                "      \"useLog\": %s%n",
                Boolean.toString(params.useLog));
        writer.print("    }");
    }

    private static void writeMatch(
            PrintWriter writer,
            MatchFiltParams params) throws Exception {
        @SuppressWarnings("unchecked")
        List<String> filenames =
                (List<String>) field(params, "kernelFilenameList");
        writer.println("    \"matchedFilter\": {");
        writeCommon(writer, params, "      ");
        writer.print("      \"kernelFilenameList\": [");
        for (int index = 0; index < filenames.size(); index++) {
            if (index > 0) {
                writer.print(", ");
            }
            writer.print(jsonString(filenames.get(index)));
        }
        writer.println("],");
        writer.println("      \"kernelSamples\": []");
        writer.print("    }");
    }

    private static void writeJavaBackingEvidence(
            PrintWriter writer,
            EnergySumParams energy,
            SgramCorrParams sgram,
            MatchFiltParams match) throws Exception {
        writer.println("  \"javaConstructorBackingEvidence\": {");
        writer.println("    \"common\": {");
        writer.println("      \"serialVersionUID\": 0,");
        writer.println("      \"dataSource\": null,");
        writer.println("      \"channelBitmap\": 0,");
        writer.println("      \"channelGroups\": null,");
        writer.println("      \"groupingType\": 1");
        writer.println("    },");
        writer.println("    \"EnergySumParams\": {");
        writer.printf(
                "      \"dontUpgrade\": %s%n",
                Boolean.toString(
                        (Boolean) field(energy, "dontUpgrade")));
        writer.println("    },");
        writer.println("    \"SgramCorrParams\": {");
        writer.printf(
                Locale.ROOT,
                "      \"segmentRows\": %d%n",
                sgram.segment.length);
        writer.println("    },");
        writer.println("    \"MatchFiltParams\": {");
        writer.printf(
                Locale.ROOT,
                "      \"maximumFilenameListSize\": %d,%n",
                MatchFiltParams.MAX_FILENAME_LIST_SIZE);
        writer.printf(
                "      \"activeKernelFilename\": %s%n",
                jsonString(match.getKernelFilename()));
        writer.println("    }");
        writer.print("  }");
    }

    private static void writeExcludedEvidence(
            PrintWriter writer,
            EnergySumParams energy,
            SgramCorrParams sgram,
            MatchFiltParams match) {
        writer.println("  \"excludedJavaDefaults\": {");
        writer.println("    \"deprecatedAndDisplay\": {");
        writer.printf(
                Locale.ROOT,
                "      \"vscale\": %s,%n",
                Double.toString(energy.vscale));
        writer.printf(
                Locale.ROOT,
                "      \"smoothing\": %s%n",
                Double.toString(energy.smoothing));
        writer.println("    },");
        writer.println("    \"sourceNames\": {");
        writer.printf(
                "      \"energySum\": %s,%n",
                nullableJsonString(
                        energy.groupedSourceParmas.getDataSource()));
        writer.printf(
                "      \"spectrogramCorrelation\": %s,%n",
                nullableJsonString(
                        sgram.groupedSourceParmas.getDataSource()));
        writer.printf(
                "      \"matchedFilter\": %s%n",
                nullableJsonString(
                        match.groupedSourceParmas.getDataSource()));
        writer.println("    }");
        writer.print("  }");
    }

    private static void requirePinnedDefaults(
            EnergySumParams energy,
            SgramCorrParams sgram,
            MatchFiltParams match) throws Exception {
        requireCommon(energy);
        requireCommon(sgram);
        requireCommon(match);
        require(energy.f0 == 0.0 && energy.f1 == 1000.0,
                "Energy sum primary frequency defaults changed");
        require(energy.ratiof0 == 1000.0 &&
                        energy.ratiof1 == 2000.0 &&
                        !energy.useRatio,
                "Energy sum ratio defaults changed");
        require(!energy.adaptiveThreshold &&
                        energy.longFilter == 0.0001 &&
                        !energy.useLog &&
                        energy.spikeDecay == 100.0,
                "Energy sum adaptive/log defaults changed");
        require(!energy.outPutSmoothing &&
                        energy.shortFilter != null &&
                        energy.shortFilter == 0.1,
                "Energy sum smoothing defaults changed");
        require((Boolean) field(energy, "dontUpgrade"),
                "Energy sum upgrade sentinel changed");
        require(sgram.segment != null &&
                        sgram.segment.length == 0 &&
                        sgram.spread == 100.0 &&
                        !sgram.useLog,
                "Spectrogram correlation defaults changed");
        @SuppressWarnings("unchecked")
        ArrayList<String> filenames =
                (ArrayList<String>) field(
                        match,
                        "kernelFilenameList");
        require(filenames.isEmpty() &&
                        match.getKernelFilename().isEmpty() &&
                        MatchFiltParams.MAX_FILENAME_LIST_SIZE == 10,
                "Matched-filter kernel history defaults changed");
    }

    private static void requireCommon(
            IshDetParams params) {
        GroupedSourceParameters source =
                params.groupedSourceParmas;
        require(source != null,
                "GroupedSourceParameters is no longer constructed");
        require(source.getDataSource() == null,
                "Ishmael source-name default changed");
        require(source.getChanOrSeqBitmap() == 0,
                "Ishmael channel bitmap default changed");
        require(source.getChannelGroups() == null,
                "Ishmael channel-group array default changed");
        require(source.getGroupingType() == 1,
                "Ishmael grouping is no longer GROUP_ALL");
        require(params.thresh == 1.0 &&
                        params.minTime == 0.0 &&
                        params.maxTime == 99999.0 &&
                        params.refractoryTime == 0.0,
                "Ishmael peak defaults changed");
        require(params.vscale == 50.0 &&
                        params.smoothing == 0.0,
                "Ishmael excluded display/dead defaults changed");
    }

    private static Object field(
            Object instance,
            String name) throws Exception {
        Class<?> type = instance.getClass();
        while (type != null) {
            try {
                Field field = type.getDeclaredField(name);
                field.setAccessible(true);
                return field.get(instance);
            }
            catch (NoSuchFieldException ignored) {
                type = type.getSuperclass();
            }
        }
        throw new NoSuchFieldException(name);
    }

    private static String nullableJsonString(String value) {
        return value == null ? "null" : jsonString(value);
    }

    private static String jsonString(String value) {
        StringBuilder encoded = new StringBuilder("\"");
        for (int index = 0; index < value.length(); index++) {
            char character = value.charAt(index);
            if (character == '"' || character == '\\') {
                encoded.append('\\');
            }
            encoded.append(character);
        }
        return encoded.append('"').toString();
    }

    private static void require(
            boolean condition,
            String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
