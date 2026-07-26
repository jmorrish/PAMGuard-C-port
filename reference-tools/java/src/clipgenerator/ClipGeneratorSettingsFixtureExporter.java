package clipgenerator;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;

/**
 * Exports pinned ClipSettings and ClipGenSetting defaults in the portable C++
 * shape and audits the Java source-level trigger eligibility boundaries.
 *
 * This exporter lives in package clipgenerator so it can exercise the
 * protected ClipGenSetting.clone() migration path directly.
 */
public final class ClipGeneratorSettingsFixtureExporter {

    private static final String EXPECTED_VERSION = "2.02.18e";
    private static final String EXPECTED_COMMIT =
            "dca55c81ef6f1498a8a3b926c69e7182afb915ee";
    private static final String FIXTURE_DATA_NAME =
            "__fixture_trigger__";
    private static final String FIXTURE_UNIT_ID =
            "fixture-trigger-unit";
    private static final String FIXTURE_OUTPUT_ROLE =
            "detections";

    private ClipGeneratorSettingsFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 4) {
            System.err.println(
                    "Usage: ClipGeneratorSettingsFixtureExporter " +
                    "<output.json> <pamguard-version> <pamguard-commit> " +
                    "<pamguard-java-root>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        require(EXPECTED_VERSION.equals(args[1]),
                "Expected PAMGuard " + EXPECTED_VERSION +
                ", found " + args[1]);
        require(EXPECTED_COMMIT.equals(args[2]),
                "Expected PAMGuard commit " + EXPECTED_COMMIT +
                ", found " + args[2]);

        ClipSettings settings = new ClipSettings();
        ClipSettings clonedSettings = settings.clone();
        ClipGenSetting trigger =
                new ClipGenSetting(FIXTURE_DATA_NAME);
        ClipGenSetting clonedTrigger = trigger.clone();

        boolean hadMapLine =
                booleanField(trigger, "hadMapLine");
        boolean clonedHadMapLine =
                booleanField(clonedTrigger, "hadMapLine");

        require(ClipSettings.serialVersionUID == 1L,
                "ClipSettings serialVersionUID changed");
        require(ClipGenSetting.serialVersionUID == 1L,
                "ClipGenSetting serialVersionUID changed");
        require(ClipSettings.STORE_WAVFILES == 0 &&
                        ClipSettings.STORE_BINARY == 1 &&
                        ClipSettings.STORE_ANNOTATION == 2 &&
                        ClipSettings.STORE_BOTH == 3,
                "ClipSettings storage constants changed");
        require(
                ClipGenSetting.DETECTION_CHANNELS_ONLY == 0 &&
                        ClipGenSetting.FIRST_DETECTION_CHANNEL_ONLY == 1 &&
                        ClipGenSetting.ALL_CHANNELS == 2,
                "ClipGenSetting channel constants changed");
        require(
                ClipGenSetting.channelSelTypes.length == 3 &&
                        "Detection channels only".equals(
                                ClipGenSetting.channelSelTypes[0]) &&
                        "First detection channel only".equals(
                                ClipGenSetting.channelSelTypes[1]) &&
                        "All channels".equals(
                                ClipGenSetting.channelSelTypes[2]),
                "ClipGenSetting channel labels changed");

        require(settings.dataSourceName == null &&
                        settings.outputFolder == null &&
                        settings.datedSubFolders &&
                        settings.storageOption ==
                                ClipSettings.STORE_BINARY &&
                        settings.compressorIndex == 0 &&
                        settings.getNumClipGenerators() == 0,
                "ClipSettings constructor defaults changed");
        require(clonedSettings != settings &&
                        clonedSettings.getNumClipGenerators() == 0,
                "ClipSettings fresh clone semantics changed");

        require(FIXTURE_DATA_NAME.equals(trigger.dataName) &&
                        trigger.enable &&
                        trigger.preSeconds == 0.0 &&
                        trigger.postSeconds == 0.0 &&
                        trigger.channelSelection ==
                                ClipGenSetting.DETECTION_CHANNELS_ONLY &&
                        trigger.clipPrefix == null &&
                        trigger.useDataBudget &&
                        trigger.dataBudget == 10 * 1024 &&
                        trigger.budgetPeriodHours == 24.0 &&
                        trigger.mapLineLength == null &&
                        !hadMapLine,
                "ClipGenSetting constructor defaults changed");
        require(clonedTrigger != trigger &&
                        Double.valueOf(1000.0).equals(
                                clonedTrigger.mapLineLength) &&
                        clonedHadMapLine,
                "ClipGenSetting legacy map-line clone migration changed");

        Path javaRoot = Path.of(args[3]);
        auditEligibilitySources(javaRoot);

        File output = new File(args[0]);
        File parent = output.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }
        try (PrintWriter writer = new PrintWriter(
                output, StandardCharsets.UTF_8)) {
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
                    "\"clipgenerator." +
                    "ClipGeneratorSettingsFixtureExporter\",");
            writer.println(
                    "    \"settingsClass\": " +
                    "\"clipgenerator.ClipSettings\",");
            writer.println(
                    "    \"triggerPolicyClass\": " +
                    "\"clipgenerator.ClipGenSetting\"");
            writer.println("  },");
            writer.println("  \"portableOmissions\": {");
            writer.println(
                    "    \"dataSourceName\": " +
                    "\"owned by the controlled-unit rawAudio graph " +
                    "binding\",");
            writer.println(
                    "    \"outputFolder\": " +
                    "\"host/deployment-owned filesystem destination\",");
            writer.println(
                    "    \"compressorIndex\": " +
                    "\"unfinished Java annotation-storage path\",");
            writer.println(
                    "    \"mapLineLength\": " +
                    "\"Clip display/map preference\",");
            writer.println(
                    "    \"hadMapLine\": " +
                    "\"private legacy display-setting migration state\",");
            writer.println(
                    "    \"dataName\": " +
                    "\"replaced by stable triggerSource unitId and " +
                    "outputRole\"");
            writer.println("  },");
            writer.println("  \"sourceEligibilityBoundary\": {");
            writer.println("    \"receiverOwnedPolicies\": true,");
            writer.println("    \"requiresCanClipGenerate\": true,");
            writer.println(
                    "    \"clickDetectorOutputEligible\": false,");
            writer.println(
                    "    \"spectrogramMarksEligible\": true,");
            writer.println(
                    "    \"spectrogramMarkUsesDataBudget\": false");
            writer.println("  },");
            writer.println("  \"javaStoredConstants\": {");
            writer.println("    \"storage\": {");
            writer.printf(
                    Locale.ROOT,
                    "      \"wavFiles\": %d,%n",
                    ClipSettings.STORE_WAVFILES);
            writer.printf(
                    Locale.ROOT,
                    "      \"binary\": %d,%n",
                    ClipSettings.STORE_BINARY);
            writer.printf(
                    Locale.ROOT,
                    "      \"annotation\": %d,%n",
                    ClipSettings.STORE_ANNOTATION);
            writer.printf(
                    Locale.ROOT,
                    "      \"both\": %d%n",
                    ClipSettings.STORE_BOTH);
            writer.println("    },");
            writer.println("    \"channelSelection\": {");
            writer.printf(
                    Locale.ROOT,
                    "      \"detectionChannelsOnly\": %d,%n",
                    ClipGenSetting.DETECTION_CHANNELS_ONLY);
            writer.printf(
                    Locale.ROOT,
                    "      \"firstDetectionChannelOnly\": %d,%n",
                    ClipGenSetting.FIRST_DETECTION_CHANNEL_ONLY);
            writer.printf(
                    Locale.ROOT,
                    "      \"allChannels\": %d%n",
                    ClipGenSetting.ALL_CHANNELS);
            writer.println("    }");
            writer.println("  },");
            writer.println("  \"portableSettingsDefaults\": {");
            writer.printf(
                    Locale.ROOT,
                    "    \"storageMode\": %s,%n",
                    jsonString(storageMode(settings.storageOption)));
            writer.printf(
                    Locale.ROOT,
                    "    \"datedSubFolders\": %s,%n",
                    Boolean.toString(settings.datedSubFolders));
            writer.println("    \"triggerPolicies\": []");
            writer.println("  },");
            writer.println("  \"triggerPolicyFieldDefaults\": {");
            writer.println("    \"triggerSource\": {");
            writer.printf(
                    Locale.ROOT,
                    "      \"unitId\": %s,%n",
                    jsonString(FIXTURE_UNIT_ID));
            writer.printf(
                    Locale.ROOT,
                    "      \"outputRole\": %s%n",
                    jsonString(FIXTURE_OUTPUT_ROLE));
            writer.println("    },");
            writer.printf(
                    Locale.ROOT,
                    "    \"enabled\": %s,%n",
                    Boolean.toString(trigger.enable));
            writer.printf(
                    Locale.ROOT,
                    "    \"secondsBeforeTrigger\": %s,%n",
                    number(trigger.preSeconds));
            writer.printf(
                    Locale.ROOT,
                    "    \"secondsAfterTrigger\": %s,%n",
                    number(trigger.postSeconds));
            writer.printf(
                    Locale.ROOT,
                    "    \"channelSelection\": %s,%n",
                    jsonString(channelSelection(
                            trigger.channelSelection)));
            writer.println("    \"clipPrefix\": null,");
            writer.printf(
                    Locale.ROOT,
                    "    \"useDataBudget\": %s,%n",
                    Boolean.toString(trigger.useDataBudget));
            writer.printf(
                    Locale.ROOT,
                    "    \"dataBudgetKilobytes\": %d,%n",
                    trigger.dataBudget);
            writer.printf(
                    Locale.ROOT,
                    "    \"budgetPeriodHours\": %s%n",
                    number(trigger.budgetPeriodHours));
            writer.println("  },");
            writer.println("  \"excludedJavaFieldDefaults\": {");
            writer.println("    \"dataSourceName\": null,");
            writer.println("    \"outputFolder\": null,");
            writer.printf(
                    Locale.ROOT,
                    "    \"compressorIndex\": %d,%n",
                    settings.compressorIndex);
            writer.printf(
                    Locale.ROOT,
                    "    \"freshTriggerCount\": %d,%n",
                    settings.getNumClipGenerators());
            writer.printf(
                    Locale.ROOT,
                    "    \"triggerDataName\": %s,%n",
                    jsonString(trigger.dataName));
            writer.println("    \"mapLineLengthMetres\": null,");
            writer.printf(
                    Locale.ROOT,
                    "    \"hadMapLine\": %s,%n",
                    Boolean.toString(hadMapLine));
            writer.printf(
                    Locale.ROOT,
                    "    \"clonedMapLineLengthMetres\": %s,%n",
                    number(clonedTrigger.mapLineLength));
            writer.printf(
                    Locale.ROOT,
                    "    \"clonedHadMapLine\": %s%n",
                    Boolean.toString(clonedHadMapLine));
            writer.println("  }");
            writer.println("}");
        }
    }

    private static void auditEligibilitySources(
            Path javaRoot) throws Exception {
        String clipControl = compactSource(
                javaRoot.resolve(
                        "src/clipgenerator/ClipControl.java"));
        String spectrogramMarks = compactSource(
                javaRoot.resolve(
                        "src/clipgenerator/" +
                        "ClipSpectrogramMarkDataBlock.java"));
        String clickDetector = compactSource(
                javaRoot.resolve(
                        "src/clickDetector/ClickDetector.java"));

        require(clipControl.contains(
                        "if(aBlock.isCanClipGenerate())" +
                        "{clipBlocks.add(aBlock);}"),
                "ClipControl no longer filters trigger blocks by " +
                "canClipGenerate");
        require(clipControl.contains(
                        "if(aBlock==clipProcess." +
                        "getClipSpectrogramMarkDataBlock())" +
                        "{genSet.useDataBudget=false;}"),
                "ClipControl spectrogram-mark budget default changed");
        require(spectrogramMarks.contains(
                        "setCanClipGenerate(true);"),
                "ClipSpectrogramMarkDataBlock eligibility changed");
        require(clickDetector.contains(
                        "outputClickData.setCanClipGenerate(false);"),
                "Click Detector clip-trigger eligibility changed");
    }

    private static String compactSource(Path source)
            throws Exception {
        require(Files.isRegularFile(source),
                "PAMGuard source not found: " + source);
        return Files.readString(
                source, StandardCharsets.UTF_8)
                .replaceAll("\\s+", "");
    }

    private static boolean booleanField(
            Object instance,
            String name) throws Exception {
        Field field =
                instance.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field.getBoolean(instance);
    }

    private static String storageMode(int value) {
        switch (value) {
            case ClipSettings.STORE_WAVFILES:
                return "wav-files";
            case ClipSettings.STORE_BINARY:
                return "binary";
            case ClipSettings.STORE_BOTH:
                return "both";
            default:
                throw new IllegalStateException(
                        "Unsupported executable storage mode " +
                        value);
        }
    }

    private static String channelSelection(int value) {
        switch (value) {
            case ClipGenSetting.DETECTION_CHANNELS_ONLY:
                return "detection-channels-only";
            case ClipGenSetting.FIRST_DETECTION_CHANNEL_ONLY:
                return "first-detection-channel-only";
            case ClipGenSetting.ALL_CHANNELS:
                return "all-channels";
            default:
                throw new IllegalStateException(
                        "Unsupported channel selection " + value);
        }
    }

    private static String number(double value) {
        require(
                Double.isFinite(value),
                "Fixture number is not finite");
        return Double.toString(value);
    }

    private static String jsonString(String value) {
        StringBuilder result = new StringBuilder("\"");
        for (int index = 0; index < value.length(); ++index) {
            char character = value.charAt(index);
            switch (character) {
                case '\\':
                    result.append("\\\\");
                    break;
                case '"':
                    result.append("\\\"");
                    break;
                case '\n':
                    result.append("\\n");
                    break;
                case '\r':
                    result.append("\\r");
                    break;
                case '\t':
                    result.append("\\t");
                    break;
                default:
                    if (character < 0x20) {
                        result.append(String.format(
                                Locale.ROOT,
                                "\\u%04x",
                                (int) character));
                    }
                    else {
                        result.append(character);
                    }
            }
        }
        return result.append('"').toString();
    }

    private static void require(
            boolean condition,
            String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
