package org.pamguard.port.reference;

import spectrogramNoiseReduction.SpectrogramNoiseSettings;
import spectrogramNoiseReduction.averageSubtraction.AverageSubtractionParameters;
import spectrogramNoiseReduction.medianFilter.MedianFilterParams;
import spectrogramNoiseReduction.threshold.SpectrogramThreshold;
import spectrogramNoiseReduction.threshold.ThresholdParams;
import whistlesAndMoans.WhistleToneParameters;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.Locale;

/**
 * Exports constructor and owned-process defaults for the pinned PAMGuard
 * Whistle and Moan Detector.
 *
 * <p>The WhistleToneParameters constructor does not populate the method
 * settings owned by SpectrogramNoiseProcess. The portable contract combines
 * its four disabled run flags with the default parameter objects supplied by
 * those methods, while retaining the zero max-frequency Nyquist sentinel.</p>
 */
public final class WhistleMoanSettingsFixtureExporter {

    private static final String EXPECTED_VERSION = "2.02.18e";
    private static final String EXPECTED_COMMIT =
            "dca55c81ef6f1498a8a3b926c69e7182afb915ee";

    private WhistleMoanSettingsFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            System.err.println(
                    "Usage: WhistleMoanSettingsFixtureExporter " +
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

        WhistleToneParameters whistle = new WhistleToneParameters();
        Object noiseBacking =
                field(whistle, "specNoiseSettings");
        double maxFrequencyBacking =
                ((Number) field(whistle, "maxFrequency")).doubleValue();
        SpectrogramNoiseSettings noise =
                whistle.getSpecNoiseSettings();
        MedianFilterParams median = new MedianFilterParams();
        AverageSubtractionParameters average =
                new AverageSubtractionParameters();
        ThresholdParams threshold = new ThresholdParams();

        requirePinnedDefaults(
                whistle,
                noiseBacking,
                maxFrequencyBacking,
                noise,
                median,
                average,
                threshold);

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
                    "WhistleMoanSettingsFixtureExporter\"");
            writer.println("  },");
            writePortableDefaults(
                    writer,
                    whistle,
                    noise,
                    median,
                    average,
                    threshold);
            writer.println(",");
            writeJavaBackingEvidence(
                    writer,
                    whistle,
                    noiseBacking,
                    maxFrequencyBacking,
                    noise);
            writer.println(",");
            writeOwnedMethodEvidence(
                    writer,
                    median,
                    average,
                    threshold);
            writer.println(",");
            writeExcludedDefaults(writer, whistle);
            writer.println();
            writer.println("}");
        }
    }

    private static void writePortableDefaults(
            PrintWriter writer,
            WhistleToneParameters whistle,
            SpectrogramNoiseSettings noise,
            MedianFilterParams median,
            AverageSubtractionParameters average,
            ThresholdParams threshold) {
        writer.println("  \"portableSettingsDefaults\": {");
        writer.printf(
                Locale.ROOT,
                "    \"channelBitmap\": %d,%n",
                whistle.getChanOrSeqBitmap());
        writer.println("    \"groupingType\": \"all\",");
        writer.println("    \"channelGroups\": [],");
        writer.printf(
                Locale.ROOT,
                "    \"minFrequencyHz\": %s,%n",
                Double.toString(whistle.getMinFrequency()));
        // Preserve the persisted zero rather than calling the mutating getter.
        writer.println("    \"maxFrequencyHz\": 0.0,");
        writer.printf(
                Locale.ROOT,
                "    \"connectType\": %d,%n",
                whistle.getConnectType());
        writer.printf(
                Locale.ROOT,
                "    \"minLength\": %d,%n",
                whistle.minLength);
        writer.printf(
                Locale.ROOT,
                "    \"minPixels\": %d,%n",
                whistle.minPixels);
        writer.printf(
                Locale.ROOT,
                "    \"keepShapeStubs\": %s,%n",
                Boolean.toString(whistle.keepShapeStubs));
        writer.printf(
                Locale.ROOT,
                "    \"fragmentationMethod\": %d,%n",
                whistle.fragmentationMethod);
        writer.printf(
                Locale.ROOT,
                "    \"maxCrossLength\": %d,%n",
                whistle.maxCrossLength);
        writer.println("    \"noiseReduction\": {");
        writer.printf(
                Locale.ROOT,
                "      \"medianFilter\": %s,%n",
                Boolean.toString(noise.isRunMethod(0)));
        writer.printf(
                Locale.ROOT,
                "      \"medianFilterLength\": %d,%n",
                median.filterLength);
        writer.printf(
                Locale.ROOT,
                "      \"averageSubtraction\": %s,%n",
                Boolean.toString(noise.isRunMethod(1)));
        writer.printf(
                Locale.ROOT,
                "      \"updateConstant\": %s,%n",
                Double.toString(average.updateConstant));
        writer.printf(
                Locale.ROOT,
                "      \"kernelSmoothing\": %s,%n",
                Boolean.toString(noise.isRunMethod(2)));
        writer.printf(
                Locale.ROOT,
                "      \"threshold\": %s,%n",
                Boolean.toString(noise.isRunMethod(3)));
        writer.printf(
                Locale.ROOT,
                "      \"thresholdDb\": %s,%n",
                Double.toString(threshold.thresholdDB));
        writer.printf(
                Locale.ROOT,
                "      \"finalOutput\": %d%n",
                threshold.finalOutput);
        writer.println("    }");
        writer.print("  }");
    }

    private static void writeJavaBackingEvidence(
            PrintWriter writer,
            WhistleToneParameters whistle,
            Object noiseBacking,
            double maxFrequencyBacking,
            SpectrogramNoiseSettings noise) throws Exception {
        writer.println("  \"javaConstructorBackingEvidence\": {");
        writer.println(
                "    \"whistlesAndMoans.WhistleToneParameters\": {");
        writer.printf(
                Locale.ROOT,
                "      \"serialVersionUID\": %d,%n",
                WhistleToneParameters.serialVersionUID);
        writer.printf(
                Locale.ROOT,
                "      \"dataSource\": %s,%n",
                nullableString(whistle.getDataSource()));
        writer.printf(
                Locale.ROOT,
                "      \"channelBitmap\": %d,%n",
                whistle.getChanOrSeqBitmap());
        writer.printf(
                Locale.ROOT,
                "      \"channelGroups\": %s,%n",
                whistle.getChannelGroups() == null ? "null" : "[]");
        writer.printf(
                Locale.ROOT,
                "      \"groupingType\": %d,%n",
                whistle.getGroupingType());
        writer.printf(
                Locale.ROOT,
                "      \"maxFrequencyBeforeGetter\": %s,%n",
                Double.toString(maxFrequencyBacking));
        writer.printf(
                Locale.ROOT,
                "      \"maxFrequencyAt48000FromGetter\": %s,%n",
                Double.toString(whistle.getMaxFrequency(48000.0)));
        writer.printf(
                Locale.ROOT,
                "      \"specNoiseSettingsBeforeGetter\": %s%n",
                noiseBacking == null ? "null" : "\"non-null\"");
        writer.println("    },");
        writer.println(
                "    \"spectrogramNoiseReduction." +
                "SpectrogramNoiseSettings\": {");
        writer.printf(
                Locale.ROOT,
                "      \"serialVersionUID\": %d,%n",
                SpectrogramNoiseSettings.serialVersionUID);
        writer.printf(
                Locale.ROOT,
                "      \"dataSource\": %s,%n",
                nullableString(noise.dataSource));
        writer.printf(
                Locale.ROOT,
                "      \"channelList\": %d,%n",
                noise.channelList);
        writer.printf(
                Locale.ROOT,
                "      \"runMethod\": %s,%n",
                noise.isRunMethod() == null ? "null" : "[]");
        writer.printf(
                Locale.ROOT,
                "      \"methodSettingsCount\": %d%n",
                noise.methodSettings.size());
        writer.println("    }");
        writer.print("  }");
    }

    private static void writeOwnedMethodEvidence(
            PrintWriter writer,
            MedianFilterParams median,
            AverageSubtractionParameters average,
            ThresholdParams threshold) {
        writer.println("  \"ownedNoiseMethodDefaults\": {");
        writer.printf(
                Locale.ROOT,
                "    \"medianFilterLength\": %d,%n",
                median.filterLength);
        writer.printf(
                Locale.ROOT,
                "    \"averageUpdateConstant\": %s,%n",
                Double.toString(average.updateConstant));
        writer.println("    \"kernel\": \"fixed-3x3-gaussian\",");
        writer.printf(
                Locale.ROOT,
                "    \"thresholdDb\": %s,%n",
                Double.toString(threshold.thresholdDB));
        writer.printf(
                Locale.ROOT,
                "    \"thresholdFinalOutput\": %d,%n",
                threshold.finalOutput);
        writer.printf(
                Locale.ROOT,
                "    \"outputRawConstant\": %d%n",
                SpectrogramThreshold.OUTPUT_RAW);
        writer.print("  }");
    }

    private static void writeExcludedDefaults(
            PrintWriter writer,
            WhistleToneParameters whistle) {
        writer.println("  \"excludedJavaDefaults\": {");
        writer.printf(
                Locale.ROOT,
                "    \"backgroundIntervalSeconds\": %s,%n",
                Double.toString(whistle.getBackgroundInterval()));
        writer.printf(
                Locale.ROOT,
                "    \"showContourOutline\": %s,%n",
                Boolean.toString(whistle.showContourOutline));
        writer.printf(
                Locale.ROOT,
                "    \"stretchContours\": %s,%n",
                Boolean.toString(whistle.stretchContours));
        writer.printf(
                Locale.ROOT,
                "    \"shortLength\": %d,%n",
                whistle.shortLength);
        writer.printf(
                Locale.ROOT,
                "    \"shortShowPolicy\": %d%n",
                whistle.shortShowPolicy);
        writer.print("  }");
    }

    private static void requirePinnedDefaults(
            WhistleToneParameters whistle,
            Object noiseBacking,
            double maxFrequencyBacking,
            SpectrogramNoiseSettings noise,
            MedianFilterParams median,
            AverageSubtractionParameters average,
            ThresholdParams threshold) throws Exception {
        require(WhistleToneParameters.serialVersionUID == 1L,
                "WhistleToneParameters serialVersionUID changed");
        require(whistle.getDataSource() == null,
                "Whistle FFT data-source default changed");
        require(whistle.getChanOrSeqBitmap() == 0,
                "Whistle channel/sequence bitmap default changed");
        require(whistle.getChannelGroups() == null,
                "Whistle group array default changed");
        require(whistle.getGroupingType() == 1,
                "Whistle grouping type is no longer GROUP_ALL");
        require(((Number) field(whistle, "connectType")).intValue() == 8,
                "Whistle connection default changed");
        require(whistle.getMinFrequency() == 0.0 &&
                        maxFrequencyBacking == 0.0,
                "Whistle persisted frequency defaults changed");
        require(whistle.minPixels == 20 &&
                        whistle.minLength == 10 &&
                        whistle.maxCrossLength == 5,
                "Whistle size defaults changed");
        require(whistle.fragmentationMethod ==
                        WhistleToneParameters.FRAGMENT_RELINK,
                "Whistle fragmentation default changed");
        require(!whistle.keepShapeStubs,
                "Whistle stub default changed");
        require(noiseBacking == null,
                "Whistle constructor now creates noise settings eagerly");
        require(SpectrogramNoiseSettings.serialVersionUID == 0L,
                "SpectrogramNoiseSettings serialVersionUID changed");
        require(noise.isRunMethod() == null &&
                        noise.methodSettings.isEmpty(),
                "Bare noise-method state changed");
        for (int method = 0; method < 4; method++) {
            require(!noise.isRunMethod(method),
                    "Bare noise method unexpectedly enabled");
        }
        require(median.filterLength == 61,
                "Median-filter length default changed");
        require(average.updateConstant == 0.02,
                "Average-subtraction default changed");
        require(threshold.thresholdDB == 8.0 &&
                        threshold.finalOutput ==
                        SpectrogramThreshold.OUTPUT_RAW &&
                        SpectrogramThreshold.OUTPUT_RAW == 2,
                "Threshold defaults changed");
    }

    private static Object field(
            Object instance,
            String name) throws Exception {
        Field field = instance.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field.get(instance);
    }

    private static String nullableString(String value) {
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

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
