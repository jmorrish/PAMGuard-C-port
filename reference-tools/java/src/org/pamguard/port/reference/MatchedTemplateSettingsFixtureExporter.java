package org.pamguard.port.reference;

import matchedTemplateClassifer.DefaultTemplates;
import matchedTemplateClassifer.MTClassifier;
import matchedTemplateClassifer.MatchTemplate;
import matchedTemplateClassifer.MatchedTemplateParams;
import org.jamdev.jpamutils.wavFiles.WavInterpolator;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

/**
 * Exports the pinned matched-template settings defaults and the complete
 * built-in template catalogue from the real PAMGuard classes.
 */
public final class MatchedTemplateSettingsFixtureExporter {

    private MatchedTemplateSettingsFixtureExporter() {
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
                            Locale.ROOT, "\\u%04x", (int) character));
                }
                else {
                    result.append(character);
                }
                break;
            }
        }
        return result.append('"').toString();
    }

    private static void writeTemplate(
            PrintWriter writer,
            MatchTemplate template) {
        writer.print("{\"name\":");
        writer.print(jsonString(template.name));
        writer.print(",\"sampleRateHz\":");
        writer.print(Float.toString(template.sR));
        writer.print(",\"waveform\":[");
        for (int index = 0; index < template.waveform.length; index++) {
            if (index > 0) {
                writer.print(',');
            }
            writer.print(Double.toString(template.waveform[index]));
        }
        writer.print("]}");
    }

    private static void writeClassifier(
            PrintWriter writer,
            MTClassifier classifier) {
        writer.print("{\"thresholdToAccept\":");
        writer.print(Double.toString(classifier.thresholdToAccept));
        writer.print(",\"normalisation\":");
        writer.print(classifier.normalisation);
        writer.print(",\"matchTemplate\":");
        writeTemplate(writer, classifier.waveformMatch);
        writer.print(",\"rejectTemplate\":");
        writeTemplate(writer, classifier.waveformReject);
        writer.print('}');
    }

    private static void writeDefaults(
            File output,
            MatchedTemplateParams params) throws Exception {
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.print("{\"clickType\":");
            writer.print(Byte.toUnsignedInt(params.type));
            writer.print(",\"normalisationType\":");
            writer.print(params.normalisationType);
            writer.print(",\"peakSearch\":");
            writer.print(params.peakSearch);
            writer.print(",\"peakSmoothing\":");
            writer.print(params.peakSmoothing);
            writer.print(",\"lengthDb\":");
            writer.print(Double.toString(params.lengthdB));
            writer.print(",\"restrictedBins\":");
            writer.print(params.restrictedBins);
            writer.print(",\"channelClassification\":");
            writer.print(params.channelClassification);
            writer.print(",\"classifiers\":[");
            for (int index = 0; index < params.classifiers.size(); index++) {
                if (index > 0) {
                    writer.print(',');
                }
                writeClassifier(writer, params.classifiers.get(index));
            }
            writer.println("]}");
        }
    }

    private static void writeCatalogue(File output) throws Exception {
        MatchTemplate[] templates = DefaultTemplates.getDefaultTemplates();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.print("{\"templates\":[");
            for (int index = 0; index < templates.length; index++) {
                if (index > 0) {
                    writer.print(',');
                }
                writeTemplate(writer, templates[index]);
            }
            writer.println("]}");
        }
    }

    private static void writeResampleCase(
            PrintWriter writer,
            WavInterpolator interpolator,
            String name,
            MatchTemplate template,
            float targetRate) {
        double[] output = interpolator.interpolate(
                template.waveform,
                template.sR,
                targetRate);
        writer.printf(
                Locale.ROOT,
                "%s,%.17g,%.17g,%d,%d",
                name,
                (double) template.sR,
                (double) targetRate,
                template.waveform.length,
                output.length);
        for (double value : output) {
            writer.printf(Locale.ROOT, ",%.17g", value);
        }
        writer.println();
    }

    private static void writeResampleFixture(File output) throws Exception {
        MatchTemplate[] templates = DefaultTemplates.getDefaultTemplates();
        WavInterpolator interpolator = new WavInterpolator();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println(
                    "name,sourceRateHz,targetRateHz,inputSamples," +
                    "outputSamples,values");
            writeResampleCase(
                    writer, interpolator, "beaked-192-to-48",
                    templates[0], 48000);
            writeResampleCase(
                    writer, interpolator, "dolphin-192-to-96",
                    templates[1], 96000);
            writeResampleCase(
                    writer, interpolator, "porpoise-500-to-48",
                    templates[2], 48000);
            writeResampleCase(
                    writer, interpolator, "sperm-equal-48",
                    templates[3], 48000);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            System.err.println(
                    "Usage: MatchedTemplateSettingsFixtureExporter " +
                    "<defaults.json> <template-catalogue.json> " +
                    "<resample.csv>");
            System.exit(2);
        }
        Locale.setDefault(Locale.ROOT);
        File defaults = new File(args[0]);
        File catalogue = new File(args[1]);
        File resample = new File(args[2]);
        defaults.getParentFile().mkdirs();
        catalogue.getParentFile().mkdirs();
        resample.getParentFile().mkdirs();

        MatchedTemplateParams params = new MatchedTemplateParams();
        writeDefaults(defaults, params);
        writeCatalogue(catalogue);
        writeResampleFixture(resample);

        MTClassifier classifier = params.classifiers.get(0);
        System.out.printf(
                Locale.ROOT,
                "classifiers=%d match=%d reject=%d presets=%d%n",
                params.classifiers.size(),
                classifier.waveformMatch.waveform.length,
                classifier.waveformReject.waveform.length,
                DefaultTemplates.getDefaultTemplates().length);
    }
}
