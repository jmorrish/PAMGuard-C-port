package org.pamguard.port.reference;

import Localiser.DelayMeasurementParams;
import Localiser.algorithms.DelayGroup;
import Localiser.algorithms.TimeDelayData;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

public final class DelayGroupFixtureExporter {
    private DelayGroupFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 4) {
            System.err.println("Usage: DelayGroupFixtureExporter <sampleRate> <signalLength> <maxDelaySamples> <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        float sampleRate = Float.parseFloat(args[0]);
        int signalLength = Integer.parseInt(args[1]);
        double maxDelaySamples = Double.parseDouble(args[2]);
        File output = new File(args[3]);

        double[][] waveforms = new double[3][signalLength];
        for (int i = 0; i < signalLength; i++) {
            waveforms[0][i] = pulse(i, 24.0);
            waveforms[1][i] = 0.92 * pulse(i, 29.0);
            waveforms[2][i] = 0.78 * pulse(i, 21.0);
        }

        DelayMeasurementParams params = new DelayMeasurementParams();
        DelayGroup delayGroup = new DelayGroup();
        TimeDelayData[] delays = delayGroup.getDelays(waveforms, sampleRate, params, new double[] {
                maxDelaySamples, maxDelaySamples, maxDelaySamples
        });

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("pairIndex,channelA,channelB,delaySamples,delayScore");
            int pair = 0;
            for (int i = 0; i < waveforms.length; i++) {
                for (int j = i + 1; j < waveforms.length; j++, pair++) {
                    writer.printf(Locale.ROOT, "%d,%d,%d,%.17g,%.17g%n",
                            pair,
                            i,
                            j,
                            delays[pair].getDelay(),
                            delays[pair].getDelayScore());
                }
            }
        }
    }

    private static double pulse(int sample, double centre) {
        double x = sample - centre;
        double envelope = Math.exp(-(x * x) / (2.0 * 10.0));
        return envelope * Math.cos(0.54 * x) + 0.01 * Math.sin(sample * 0.17);
    }
}
