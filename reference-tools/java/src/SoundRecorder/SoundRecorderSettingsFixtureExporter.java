package SoundRecorder;

import PamController.PamConfiguration;
import PamController.PamController;
import SoundRecorder.trigger.RecorderTriggerData;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Locale;

/**
 * Exports the pinned SoundRecorder.RecorderSettings constructor defaults in
 * the portable C++ settings shape.
 *
 * This class deliberately lives in the SoundRecorder package because the
 * RecorderSettings constructor and several persisted settings are
 * package-private in PAMGuard 2.02.18e.
 */
public final class SoundRecorderSettingsFixtureExporter {

    private static final String EXPECTED_VERSION = "2.02.18e";
    private static final String EXPECTED_COMMIT =
            "dca55c81ef6f1498a8a3b926c69e7182afb915ee";

    private SoundRecorderSettingsFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            System.err.println(
                    "Usage: SoundRecorderSettingsFixtureExporter " +
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

        /*
         * RecorderSettings asks the singleton controller for raw block zero.
         * A fixture exporter has no full PAMGuard model, so install the
         * smallest real PamConfiguration that makes that lookup return null.
         * RecorderSettings itself is still constructed normally: field
         * initialisers and its constructor are the authority being measured.
         */
        installEmptyController();
        RecorderSettings settings = new RecorderSettings();
        RecorderTriggerData triggerDefaults =
                new RecorderTriggerData("__fixture_trigger__", 0.0, 10.0);

        @SuppressWarnings("unchecked")
        ArrayList<RecorderTriggerData> triggerPolicies =
                (ArrayList<RecorderTriggerData>) field(
                        settings, "recorderTriggerDatas");

        require(RecorderSettings.serialVersionUID == 3L,
                "RecorderSettings serialVersionUID changed");
        require(RecorderSettings.BITDEPTHS.length == 4 &&
                        RecorderSettings.BITDEPTHS[0] == 8 &&
                        RecorderSettings.BITDEPTHS[1] == 16 &&
                        RecorderSettings.BITDEPTHS[2] == 24 &&
                        RecorderSettings.BITDEPTHS[3] == 32,
                "RecorderSettings BITDEPTHS changed");
        require(settings.rawDataSource == null,
                "Empty controller unexpectedly supplied a raw data source");
        require(settings.outputFolder != null &&
                        settings.outputFolder.endsWith(
                                File.separator + "PAMRecordings"),
                "RecorderSettings default output folder policy changed");
        require(triggerPolicies != null && triggerPolicies.isEmpty(),
                "Fresh RecorderSettings trigger policy list is not empty");

        require(settings.channelBitmap == 3 &&
                        settings.bitDepth == 16 &&
                        !settings.enableBuffer &&
                        settings.bufferLength == 30 &&
                        "PAM".equals(settings.fileInitials) &&
                        "WAVE".equals(field(settings, "fileType")) &&
                        settings.autoInterval == 300 &&
                        settings.autoDuration == 10 &&
                        settings.maxLengthSeconds == 3600 &&
                        settings.limitLengthSeconds &&
                        settings.isRoundFileStarts() &&
                        settings.maxLengthMegaBytes == 640L &&
                        settings.limitLengthMegaBytes &&
                        settings.datedSubFolders &&
                        !settings.autoStart &&
                        settings.startStatus == 0,
                "RecorderSettings constructor defaults changed");

        require(!triggerDefaults.isEnabled() &&
                        triggerDefaults.getSecondsBeforeTrigger() == 0.0 &&
                        triggerDefaults.getSecondsAfterTrigger() == 10.0 &&
                        triggerDefaults.getMinDetectionCount() == 1 &&
                        triggerDefaults.getCountSeconds() == 0 &&
                        triggerDefaults.getMinGapBetweenTriggers() == 0 &&
                        triggerDefaults.getMaxTotalTriggerLength() == 0 &&
                        triggerDefaults.getDayBudgetMB() == 0 &&
                        longField(triggerDefaults, "lastTriggerStart") == 0L &&
                        longField(triggerDefaults, "lastTriggerEnd") == 0L &&
                        longField(triggerDefaults, "usedDayBudget") == 0L,
                "RecorderTriggerData field defaults changed");

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
                    "\"SoundRecorder.SoundRecorderSettingsFixtureExporter\",");
            writer.println(
                    "    \"settingsClass\": " +
                    "\"SoundRecorder.RecorderSettings\",");
            writer.println(
                    "    \"triggerPolicyClass\": " +
                    "\"SoundRecorder.trigger.RecorderTriggerData\"");
            writer.println("  },");
            writer.println("  \"portableOmissions\": {");
            writer.println(
                    "    \"rawDataSource\": " +
                    "\"owned by the controlled-unit rawAudio binding\",");
            writer.println(
                    "    \"outputFolder\": " +
                    "\"host-derived deployment path; Java defaults to " +
                    "PamFolders project/PAMRecordings\",");
            writer.println(
                    "    \"decisionMaker\": " +
                    "\"transient RecorderTriggerData runtime state\"");
            writer.println("  },");
            writer.println("  \"portableSettingsDefaults\": {");
            writer.println("    \"operationMode\": \"idle\",");
            writer.printf(
                    Locale.ROOT,
                    "    \"channelBitmap\": %d,%n",
                    settings.channelBitmap);
            writer.printf(
                    Locale.ROOT,
                    "    \"bitDepth\": %d,%n",
                    settings.bitDepth);
            writer.printf(
                    Locale.ROOT,
                    "    \"enableBuffer\": %s,%n",
                    Boolean.toString(settings.enableBuffer));
            writer.printf(
                    Locale.ROOT,
                    "    \"bufferLengthSeconds\": %d,%n",
                    settings.bufferLength);
            writer.printf(
                    Locale.ROOT,
                    "    \"fileInitials\": %s,%n",
                    jsonString(settings.fileInitials));
            writer.printf(
                    Locale.ROOT,
                    "    \"fileType\": %s,%n",
                    jsonString((String) field(settings, "fileType")));
            writer.printf(
                    Locale.ROOT,
                    "    \"autoIntervalSeconds\": %d,%n",
                    settings.autoInterval);
            writer.printf(
                    Locale.ROOT,
                    "    \"autoDurationSeconds\": %d,%n",
                    settings.autoDuration);
            writer.printf(
                    Locale.ROOT,
                    "    \"limitLengthSeconds\": %s,%n",
                    Boolean.toString(settings.limitLengthSeconds));
            writer.printf(
                    Locale.ROOT,
                    "    \"maxLengthSeconds\": %d,%n",
                    settings.maxLengthSeconds);
            writer.printf(
                    Locale.ROOT,
                    "    \"roundFileStarts\": %s,%n",
                    Boolean.toString(settings.isRoundFileStarts()));
            writer.printf(
                    Locale.ROOT,
                    "    \"limitLengthMegaBytes\": %s,%n",
                    Boolean.toString(settings.limitLengthMegaBytes));
            writer.printf(
                    Locale.ROOT,
                    "    \"maxLengthMegaBytes\": %d,%n",
                    settings.maxLengthMegaBytes);
            writer.printf(
                    Locale.ROOT,
                    "    \"datedSubFolders\": %s,%n",
                    Boolean.toString(settings.datedSubFolders));
            writer.println("    \"triggerPolicies\": []");
            writer.println("  },");
            writer.println("  \"triggerPolicyFieldDefaults\": {");
            writeTriggerPolicy(writer, triggerDefaults);
            writer.println();
            writer.println("  }");
            writer.println("}");
        }
    }

    private static void writeTriggerPolicy(
            PrintWriter writer,
            RecorderTriggerData settings) throws Exception {
        writer.printf(
                Locale.ROOT,
                "    \"triggerName\": %s,%n",
                jsonString(settings.getTriggerName()));
        writer.printf(
                Locale.ROOT,
                "    \"enabled\": %s,%n",
                Boolean.toString(settings.isEnabled()));
        writer.printf(
                Locale.ROOT,
                "    \"secondsBeforeTrigger\": %s,%n",
                number(settings.getSecondsBeforeTrigger()));
        writer.printf(
                Locale.ROOT,
                "    \"secondsAfterTrigger\": %s,%n",
                number(settings.getSecondsAfterTrigger()));
        writer.printf(
                Locale.ROOT,
                "    \"minDetectionCount\": %d,%n",
                settings.getMinDetectionCount());
        writer.printf(
                Locale.ROOT,
                "    \"countSeconds\": %d,%n",
                settings.getCountSeconds());
        writer.printf(
                Locale.ROOT,
                "    \"minGapBetweenTriggersSeconds\": %d,%n",
                settings.getMinGapBetweenTriggers());
        writer.printf(
                Locale.ROOT,
                "    \"maxTotalTriggerLengthSeconds\": %d,%n",
                settings.getMaxTotalTriggerLength());
        writer.printf(
                Locale.ROOT,
                "    \"dayBudgetMegaBytes\": %d,%n",
                settings.getDayBudgetMB());
        writer.printf(
                Locale.ROOT,
                "    \"lastTriggerStartUnixMs\": %d,%n",
                longField(settings, "lastTriggerStart"));
        writer.printf(
                Locale.ROOT,
                "    \"lastTriggerEndUnixMs\": %d,%n",
                longField(settings, "lastTriggerEnd"));
        writer.printf(
                Locale.ROOT,
                "    \"usedDayBudgetBytes\": %d",
                longField(settings, "usedDayBudget"));
    }

    private static void installEmptyController() throws Exception {
        /*
         * Resolve Unsafe reflectively so the exporter does not acquire a
         * compile-time dependency on a JDK-internal API. It is used only to
         * install the empty singleton context required by RecorderSettings'
         * constructor; the measured object itself is normally constructed.
         */
        Class<?> unsafeClass =
                Class.forName("sun.misc.Unsafe");
        Field unsafeField =
                unsafeClass.getDeclaredField("theUnsafe");
        unsafeField.setAccessible(true);
        Object unsafe = unsafeField.get(null);
        Method allocateInstance =
                unsafeClass.getMethod("allocateInstance", Class.class);
        PamController controller =
                (PamController) allocateInstance.invoke(
                        unsafe, PamController.class);

        Field configuration =
                PamController.class.getDeclaredField("pamConfiguration");
        configuration.setAccessible(true);
        configuration.set(controller, new PamConfiguration());

        Field uniqueController =
                PamController.class.getDeclaredField("uniqueController");
        uniqueController.setAccessible(true);
        uniqueController.set(null, controller);
    }

    private static Object field(
            Object instance,
            String name) throws Exception {
        Field field = instance.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field.get(instance);
    }

    private static long longField(
            Object instance,
            String name) throws Exception {
        Field field = instance.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field.getLong(instance);
    }

    private static String number(double value) {
        require(Double.isFinite(value), "Fixture number is not finite");
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
