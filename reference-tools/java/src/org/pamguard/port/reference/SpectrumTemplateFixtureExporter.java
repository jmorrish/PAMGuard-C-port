package org.pamguard.port.reference;

import clickTrainDetector.classification.templateClassifier.DefualtSpectrumTemplates;
import matchedTemplateClassifer.MatchTemplate;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

/**
 * Exports PAMGuard's default click train spectrum templates by driving the
 * real DefualtSpectrumTemplates class, so the C++ copies of these constants
 * can be checked rather than trusted.
 *
 * Each row is: templateName,sampleRate,nBins,v0,v1,...
 */
public final class SpectrumTemplateFixtureExporter {

    private SpectrumTemplateFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: SpectrumTemplateFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        MatchTemplate[] templates = DefualtSpectrumTemplates.getDefaultTemplates();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("name,sampleRate,nBins,values");
            for (MatchTemplate template : templates) {
                if (template == null) {
                    continue;
                }
                StringBuilder values = new StringBuilder();
                for (int i = 0; i < template.waveform.length; i++) {
                    if (i > 0) {
                        values.append(' ');
                    }
                    values.append(String.format(Locale.ROOT, "%.17g", template.waveform[i]));
                }
                writer.printf(Locale.ROOT, "%s,%.17g,%d,%s%n",
                        template.name.replace(',', ';'),
                        template.sR,
                        template.waveform.length,
                        values);
            }
        }
        System.out.println("templates=" + templates.length);
    }
}
