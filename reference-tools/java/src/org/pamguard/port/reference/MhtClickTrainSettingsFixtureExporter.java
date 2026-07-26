package org.pamguard.port.reference;

import clickDetector.alarm.ClickAlarmParameters;
import clickTrainDetector.ClickTrainParams;
import clickTrainDetector.classification.CTClassifierParams;
import clickTrainDetector.classification.CTClassifierType;
import clickTrainDetector.classification.bearingClassifier.BearingClassifierParams;
import clickTrainDetector.classification.idiClassifier.IDIClassifierParams;
import clickTrainDetector.classification.simplechi2classifier.Chi2ThresholdParams;
import clickTrainDetector.classification.templateClassifier.TemplateClassifierParams;
import clickTrainDetector.clickTrainAlgorithms.mht.MHTKernelParams;
import clickTrainDetector.clickTrainAlgorithms.mht.MHTParams;
import clickTrainDetector.clickTrainAlgorithms.mht.StandardMHTChi2Params;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.AmplitudeChi2Params;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.BearingChi2VarParams;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.CorrelationChi2Params;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.IDIChi2Params;
import clickTrainDetector.clickTrainAlgorithms.mht.mhtvar.SimpleChi2VarParams;
import clickTrainDetector.localisation.CTLocParams;
import matchedTemplateClassifer.MatchTemplate;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.Locale;

/**
 * Exports the pinned ClickTrainControl and MHT constructor defaults in the
 * portable C++ settings shape.
 */
public final class MhtClickTrainSettingsFixtureExporter {

    private static final String EXPECTED_VERSION = "2.02.18e";
    private static final String EXPECTED_COMMIT =
            "dca55c81ef6f1498a8a3b926c69e7182afb915ee";

    private MhtClickTrainSettingsFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            System.err.println(
                    "Usage: MhtClickTrainSettingsFixtureExporter " +
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

        ClickTrainParams clickTrain = new ClickTrainParams();
        MHTParams mht = new MHTParams();
        require(mht.chi2Params instanceof StandardMHTChi2Params,
                "MHTParams did not construct StandardMHTChi2Params");
        StandardMHTChi2Params chi2 =
                (StandardMHTChi2Params) mht.chi2Params;
        MHTKernelParams kernel = mht.mhtKernal;

        IDIChi2Params idiVariable = element(
                chi2.chi2Settings, 0, IDIChi2Params.class, "ICI");
        AmplitudeChi2Params amplitudeVariable = element(
                chi2.chi2Settings, 1,
                AmplitudeChi2Params.class, "Amplitude");
        BearingChi2VarParams bearingVariable = element(
                chi2.chi2Settings, 2,
                BearingChi2VarParams.class, "Bearing Delta");
        CorrelationChi2Params correlationVariable = element(
                chi2.chi2Settings, 3,
                CorrelationChi2Params.class, "Correlation");
        SimpleChi2VarParams timeDelayVariable = element(
                chi2.chi2Settings, 4,
                SimpleChi2VarParams.class, "Time Delays");
        SimpleChi2VarParams lengthVariable = element(
                chi2.chi2Settings, 5,
                SimpleChi2VarParams.class, "Click Length");
        SimpleChi2VarParams peakFrequencyVariable = element(
                chi2.chi2Settings, 6,
                SimpleChi2VarParams.class, "Peak Frequency");

        ClickAlarmParameters selector = new ClickAlarmParameters();
        Object useSpeciesList = field(selector, "useSpeciesList");
        Chi2ThresholdParams preClassifier =
                clickTrain.simpleCTClassifier;
        IDIClassifierParams idiClassifier =
                new IDIClassifierParams();
        BearingClassifierParams bearingClassifier =
                new BearingClassifierParams();
        TemplateClassifierParams templateClassifier =
                new TemplateClassifierParams();
        CTLocParams localisation = clickTrain.ctLocParams;

        requirePinnedStructure(
                clickTrain,
                chi2,
                useSpeciesList,
                idiVariable,
                amplitudeVariable,
                bearingVariable,
                correlationVariable,
                timeDelayVariable,
                lengthVariable,
                peakFrequencyVariable,
                templateClassifier);

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
                    "MhtClickTrainSettingsFixtureExporter\"");
            writer.println("  },");
            writePortableDefaults(
                    writer,
                    clickTrain,
                    selector,
                    kernel,
                    chi2,
                    idiVariable,
                    amplitudeVariable,
                    bearingVariable,
                    correlationVariable,
                    timeDelayVariable,
                    lengthVariable,
                    peakFrequencyVariable,
                    preClassifier,
                    idiClassifier,
                    bearingClassifier,
                    templateClassifier,
                    localisation);
            writer.println();
            writer.println("}");
        }
    }

    private static void writePortableDefaults(
            PrintWriter writer,
            ClickTrainParams clickTrain,
            ClickAlarmParameters selector,
            MHTKernelParams kernel,
            StandardMHTChi2Params chi2,
            IDIChi2Params idiVariable,
            AmplitudeChi2Params amplitudeVariable,
            BearingChi2VarParams bearingVariable,
            CorrelationChi2Params correlationVariable,
            SimpleChi2VarParams timeDelayVariable,
            SimpleChi2VarParams lengthVariable,
            SimpleChi2VarParams peakFrequencyVariable,
            Chi2ThresholdParams preClassifier,
            IDIClassifierParams idiClassifier,
            BearingClassifierParams bearingClassifier,
            TemplateClassifierParams templateClassifier,
            CTLocParams localisation) {
        writer.println("  \"portableSettingsDefaults\": {");
        writer.println("    \"algorithm\": \"mht\",");
        writer.print("    \"channelGroups\": ");
        writeIntArray(writer, clickTrain.channelGroups);
        writer.println(",");
        writer.println("    \"dataSelector\": {");
        writer.printf(
                Locale.ROOT,
                "      \"enabled\": %s,%n",
                Boolean.toString(clickTrain.useDataSelector));
        writer.printf(
                Locale.ROOT,
                "      \"useEchoes\": %s,%n",
                Boolean.toString(selector.useEchoes));
        writer.printf(
                Locale.ROOT,
                "      \"minimumAmplitudeDb\": %s,%n",
                number(selector.minimumAmplitude));
        writer.println("      \"includedClickTypes\": []");
        writer.println("    },");
        writer.println("    \"kernel\": {");
        writer.printf(
                Locale.ROOT,
                "      \"nHold\": %d,%n",
                kernel.nHold);
        writer.printf(
                Locale.ROOT,
                "      \"nPruneback\": %d,%n",
                kernel.nPruneback);
        writer.printf(
                Locale.ROOT,
                "      \"nPrunebackStart\": %d,%n",
                kernel.nPruneBackStart);
        writer.printf(
                Locale.ROOT,
                "      \"maxCoast\": %d%n",
                kernel.maxCoast);
        writer.println("    },");
        writer.println("    \"chi2\": {");
        writer.printf(
                Locale.ROOT,
                "      \"maximumIciSeconds\": %s,%n",
                number(chi2.maxICI));
        writer.printf(
                Locale.ROOT,
                "      \"coastPenalty\": %s,%n",
                number(chi2.coastPenalty));
        writer.printf(
                Locale.ROOT,
                "      \"newTrackPenalty\": %s,%n",
                number(chi2.newTrackPenalty));
        writer.printf(
                Locale.ROOT,
                "      \"newTrackClicks\": %d,%n",
                chi2.newTrackN);
        writer.printf(
                Locale.ROOT,
                "      \"longTrackExponent\": %s,%n",
                number(chi2.longTrackExponent));
        writer.printf(
                Locale.ROOT,
                "      \"lowIciExponent\": %s,%n",
                number(chi2.lowICIExponent));
        writer.println("      \"electricalNoiseFilter\": {");
        writer.printf(
                Locale.ROOT,
                "        \"enabled\": %s,%n",
                Boolean.toString(chi2.useElectricNoiseFilter));
        writer.printf(
                Locale.ROOT,
                "        \"minimumChi2\": %s,%n",
                number(chi2.electricalNoiseParams.minChi2));
        writer.printf(
                Locale.ROOT,
                "        \"dataUnits\": %d%n",
                chi2.electricalNoiseParams.nDataUnits);
        writer.println("      },");
        writer.println("      \"variables\": {");
        writer.println("        \"idi\": {");
        writeEnabledAndErrors(
                writer, chi2.enable[0], idiVariable);
        writer.printf(
                Locale.ROOT,
                "          \"minimumIdiSeconds\": %s%n",
                number(idiVariable.minIDI));
        writer.println("        },");
        writer.println("        \"amplitude\": {");
        writeEnabledAndErrors(
                writer, chi2.enable[1], amplitudeVariable);
        writer.printf(
                Locale.ROOT,
                "          \"jumpEnabled\": %s,%n",
                Boolean.toString(amplitudeVariable.ampJumpEnable));
        writer.printf(
                Locale.ROOT,
                "          \"maximumJumpDb\": %s%n",
                number(amplitudeVariable.maxAmpJump));
        writer.println("        },");
        writer.println("        \"bearing\": {");
        writer.printf(
                Locale.ROOT,
                "          \"enabled\": %s,%n",
                Boolean.toString(chi2.enable[2]));
        writer.printf(
                Locale.ROOT,
                "          \"errorRadians\": %s,%n",
                number(bearingVariable.error));
        writer.printf(
                Locale.ROOT,
                "          \"minimumErrorRadians\": %s,%n",
                number(bearingVariable.minError));
        writer.printf(
                Locale.ROOT,
                "          \"jumpEnabled\": %s,%n",
                Boolean.toString(bearingVariable.bearingJumpEnable));
        writer.printf(
                Locale.ROOT,
                "          \"maximumJumpRadians\": %s,%n",
                number(bearingVariable.maxBearingJump));
        writer.printf(
                Locale.ROOT,
                "          \"jumpDirection\": %s%n",
                jsonString(jumpDirection(bearingVariable)));
        writer.println("        },");
        writeCommonVariable(
                writer, "correlation", chi2.enable[3],
                correlationVariable, true);
        writeCommonVariable(
                writer, "timeDelay", chi2.enable[4],
                timeDelayVariable, true);
        writeCommonVariable(
                writer, "length", chi2.enable[5],
                lengthVariable, true);
        writeCommonVariable(
                writer, "peakFrequency", chi2.enable[6],
                peakFrequencyVariable, false);
        writer.println("      }");
        writer.println("    },");
        writer.println("    \"classifier\": {");
        writer.printf(
                Locale.ROOT,
                "      \"runClassifier\": %s,%n",
                Boolean.toString(clickTrain.runClassifier));
        writer.println("      \"preClassifier\": {");
        writer.printf(
                Locale.ROOT,
                "        \"chi2Threshold\": %s,%n",
                number(preClassifier.chi2Threshold));
        writer.printf(
                Locale.ROOT,
                "        \"minimumClicks\": %d,%n",
                preClassifier.minClicks);
        writer.printf(
                Locale.ROOT,
                "        \"minimumSelectedPercentage\": %s,%n",
                number(preClassifier.minPercentage));
        writer.printf(
                Locale.ROOT,
                "        \"minimumTimeSeconds\": %s,%n",
                number(preClassifier.minTime));
        writer.printf(
                Locale.ROOT,
                "        \"speciesFlag\": %d%n",
                preClassifier.speciesFlag);
        writer.println("      },");
        writer.println("      \"idi\": {");
        writer.printf(
                Locale.ROOT,
                "        \"enabled\": %s,%n",
                Boolean.toString(classifierPresent(
                        clickTrain.ctClassifierParams,
                        CTClassifierType.IDICLASSIFIER)));
        writer.printf(
                Locale.ROOT,
                "        \"useMedianIdi\": %s,%n",
                Boolean.toString(idiClassifier.useMedianIDI));
        writer.printf(
                Locale.ROOT,
                "        \"minimumMedianIdi\": %s,%n",
                number(idiClassifier.minMedianIDI));
        writer.printf(
                Locale.ROOT,
                "        \"maximumMedianIdi\": %s,%n",
                number(idiClassifier.maxMedianIDI));
        writer.printf(
                Locale.ROOT,
                "        \"useMeanIdi\": %s,%n",
                Boolean.toString(idiClassifier.useMeanIDI));
        writer.printf(
                Locale.ROOT,
                "        \"minimumMeanIdi\": %s,%n",
                number(idiClassifier.minMeanIDI));
        writer.printf(
                Locale.ROOT,
                "        \"maximumMeanIdi\": %s,%n",
                number(idiClassifier.maxMeanIDI));
        writer.printf(
                Locale.ROOT,
                "        \"useStdIdi\": %s,%n",
                Boolean.toString(idiClassifier.useStdIDI));
        writer.printf(
                Locale.ROOT,
                "        \"minimumStdIdi\": %s,%n",
                number(idiClassifier.minStdIDI));
        writer.printf(
                Locale.ROOT,
                "        \"maximumStdIdi\": %s,%n",
                number(idiClassifier.maxStdIDI));
        writer.printf(
                Locale.ROOT,
                "        \"speciesFlag\": %d%n",
                idiClassifier.speciesFlag);
        writer.println("      },");
        writer.println("      \"bearing\": {");
        writer.printf(
                Locale.ROOT,
                "        \"enabled\": %s,%n",
                Boolean.toString(classifierPresent(
                        clickTrain.ctClassifierParams,
                        CTClassifierType.BEARINGCLASSIFIER)));
        writer.printf(
                Locale.ROOT,
                "        \"minimumBearingRadians\": %s,%n",
                number(bearingClassifier.bearingLimMin));
        writer.printf(
                Locale.ROOT,
                "        \"maximumBearingRadians\": %s,%n",
                number(bearingClassifier.bearingLimMax));
        writer.printf(
                Locale.ROOT,
                "        \"useMean\": %s,%n",
                Boolean.toString(bearingClassifier.useMean));
        writer.printf(
                Locale.ROOT,
                "        \"minimumMeanDerivative\": %s,%n",
                number(bearingClassifier.minMeanBearingD));
        writer.printf(
                Locale.ROOT,
                "        \"maximumMeanDerivative\": %s,%n",
                number(bearingClassifier.maxMeanBearingD));
        writer.printf(
                Locale.ROOT,
                "        \"useMedian\": %s,%n",
                Boolean.toString(bearingClassifier.useMedian));
        writer.printf(
                Locale.ROOT,
                "        \"minimumMedianDerivative\": %s,%n",
                number(bearingClassifier.minMedianBearingD));
        writer.printf(
                Locale.ROOT,
                "        \"maximumMedianDerivative\": %s,%n",
                number(bearingClassifier.maxMedianBearingD));
        writer.printf(
                Locale.ROOT,
                "        \"useStd\": %s,%n",
                Boolean.toString(bearingClassifier.useStD));
        writer.printf(
                Locale.ROOT,
                "        \"minimumStdDerivative\": %s,%n",
                number(bearingClassifier.minStdBearingD));
        writer.printf(
                Locale.ROOT,
                "        \"maximumStdDerivative\": %s,%n",
                number(bearingClassifier.maxStdBearingD));
        writer.printf(
                Locale.ROOT,
                "        \"speciesFlag\": %d%n",
                bearingClassifier.speciesFlag);
        writer.println("      },");
        writer.println("      \"spectrumTemplate\": {");
        writer.printf(
                Locale.ROOT,
                "        \"enabled\": %s,%n",
                Boolean.toString(classifierPresent(
                        clickTrain.ctClassifierParams,
                        CTClassifierType.TEMPLATECLASSIFIER)));
        MatchTemplate template = templateClassifier.spectrumTemplate;
        writer.printf(
                Locale.ROOT,
                "        \"name\": %s,%n",
                jsonString(template.name));
        writer.printf(
                Locale.ROOT,
                "        \"sampleRateHz\": %s,%n",
                number(template.sR));
        writer.print("        \"spectrum\": ");
        writeDoubleArray(writer, template.waveform);
        writer.println(",");
        writer.printf(
                Locale.ROOT,
                "        \"correlationThreshold\": %s,%n",
                number(templateClassifier.corrThreshold));
        writer.printf(
                Locale.ROOT,
                "        \"speciesFlag\": %d%n",
                templateClassifier.speciesFlag);
        writer.println("      }");
        writer.println("    },");
        writer.println("    \"localisation\": {");
        writer.printf(
                Locale.ROOT,
                "      \"enabled\": %s,%n",
                Boolean.toString(localisation.shouldloc));
        writer.printf(
                Locale.ROOT,
                "      \"minimumDataUnits\": %d,%n",
                localisation.minDataUnits);
        writer.printf(
                Locale.ROOT,
                "      \"minimumAngleRangeRadians\": %s%n",
                number(localisation.minAngleRange));
        writer.print("    }");
        writer.println();
        writer.print("  }");
    }

    private static void writeEnabledAndErrors(
            PrintWriter writer,
            boolean enabled,
            SimpleChi2VarParams params) {
        writer.printf(
                Locale.ROOT,
                "          \"enabled\": %s,%n",
                Boolean.toString(enabled));
        writer.printf(
                Locale.ROOT,
                "          \"error\": %s,%n",
                number(params.error));
        writer.printf(
                Locale.ROOT,
                "          \"minimumError\": %s,%n",
                number(params.minError));
    }

    private static void writeCommonVariable(
            PrintWriter writer,
            String name,
            boolean enabled,
            SimpleChi2VarParams params,
            boolean comma) {
        writer.printf(
                Locale.ROOT,
                "        %s: {%n",
                jsonString(name));
        writer.printf(
                Locale.ROOT,
                "          \"enabled\": %s,%n",
                Boolean.toString(enabled));
        writer.printf(
                Locale.ROOT,
                "          \"error\": %s,%n",
                number(params.error));
        writer.printf(
                Locale.ROOT,
                "          \"minimumError\": %s%n",
                number(params.minError));
        writer.print("        }");
        writer.println(comma ? "," : "");
    }

    private static void writeIntArray(
            PrintWriter writer,
            int[] values) {
        writer.print('[');
        for (int index = 0; index < values.length; index++) {
            if (index > 0) {
                writer.print(',');
            }
            writer.print(values[index]);
        }
        writer.print(']');
    }

    private static void writeDoubleArray(
            PrintWriter writer,
            double[] values) {
        writer.print('[');
        for (int index = 0; index < values.length; index++) {
            if (index > 0) {
                writer.print(',');
            }
            writer.print(number(values[index]));
        }
        writer.print(']');
    }

    private static boolean classifierPresent(
            CTClassifierParams[] classifiers,
            CTClassifierType type) {
        if (classifiers == null) {
            return false;
        }
        for (CTClassifierParams classifier : classifiers) {
            if (classifier != null && classifier.type == type) {
                return true;
            }
        }
        return false;
    }

    private static String jumpDirection(
            BearingChi2VarParams params) {
        switch (params.bearingJumpDrctn) {
        case BOTH:
            return "both";
        case POSITIVE:
            return "positive";
        case NEGATIVE:
            return "negative";
        default:
            throw new IllegalStateException(
                    "Unknown bearing jump direction");
        }
    }

    private static void requirePinnedStructure(
            ClickTrainParams clickTrain,
            StandardMHTChi2Params chi2,
            Object useSpeciesList,
            IDIChi2Params idiVariable,
            AmplitudeChi2Params amplitudeVariable,
            BearingChi2VarParams bearingVariable,
            CorrelationChi2Params correlationVariable,
            SimpleChi2VarParams timeDelayVariable,
            SimpleChi2VarParams lengthVariable,
            SimpleChi2VarParams peakFrequencyVariable,
            TemplateClassifierParams templateClassifier) {
        require(clickTrain.ctDetectorType == 0,
                "Expected ctDetectorType zero");
        require(clickTrain.dataSourceName == null,
                "Expected null Click Train data source");
        require(clickTrain.dataSourceIndex == 0,
                "Expected Click Train data source index zero");
        require(clickTrain.channelGroups != null,
                "Expected Click Train channel groups");
        require(clickTrain.ctClassifierParams == null,
                "Expected null Click Train classifier list");
        require(useSpeciesList == null,
                "Expected null click selector species list");
        require(chi2.chi2Settings != null &&
                chi2.chi2Settings.length == 7,
                "Expected seven standard MHT variables");
        require(chi2.enable != null && chi2.enable.length == 7,
                "Expected seven standard MHT enable flags");
        require("ICI".equals(idiVariable.name),
                "Unexpected IDI variable");
        require("Amplitude".equals(amplitudeVariable.name),
                "Unexpected amplitude variable");
        require("Bearing Delta".equals(bearingVariable.name),
                "Unexpected bearing variable");
        require("Correlation".equals(correlationVariable.name),
                "Unexpected correlation variable");
        require("Time Delays".equals(timeDelayVariable.name),
                "Unexpected time-delay variable");
        require("Click Length".equals(lengthVariable.name),
                "Unexpected length variable");
        require("Peak Frequency".equals(peakFrequencyVariable.name),
                "Unexpected peak-frequency variable");
        require(templateClassifier.spectrumTemplate != null,
                "Expected a default spectrum template");
        require(templateClassifier.spectrumTemplate.waveform != null &&
                templateClassifier.spectrumTemplate.waveform.length == 30,
                "Expected the 30-bin Beaked Whale spectrum template");
    }

    private static <T> T element(
            Object[] values,
            int index,
            Class<T> type,
            String name) {
        require(values != null && index < values.length,
                "Missing MHT variable " + name);
        require(type.isInstance(values[index]),
                "Unexpected settings class for MHT variable " + name);
        return type.cast(values[index]);
    }

    private static Object field(
            Object owner,
            String name) throws Exception {
        Field field = owner.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field.get(owner);
    }

    private static String number(double value) {
        require(Double.isFinite(value),
                "Cannot export non-finite JSON number");
        return Double.toString(value);
    }

    private static String jsonString(String value) {
        if (value == null) {
            return "null";
        }
        StringBuilder result = new StringBuilder("\"");
        for (int index = 0; index < value.length(); index++) {
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
                break;
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
