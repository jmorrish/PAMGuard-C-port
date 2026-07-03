package org.pamguard.port.reference;

import Localiser.DelayMeasurementParams;
import Localiser.algorithms.Correlations;
import Localiser.algorithms.TimeDelayData;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

public final class CorrelationDelayFixtureExporter {
    private CorrelationDelayFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 5) {
            System.err.println("Usage: CorrelationDelayFixtureExporter <sampleRate> <fftLength> <maxDelaySamples> <signalLength> <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        double sampleRate = Double.parseDouble(args[0]);
        int fftLength = Integer.parseInt(args[1]);
        double maxDelaySamples = Double.parseDouble(args[2]);
        int signalLength = Integer.parseInt(args[3]);
        File output = new File(args[4]);

        double[] channel0 = new double[signalLength];
        double[] channel1 = new double[signalLength];
        for (int i = 0; i < signalLength; i++) {
            channel0[i] = pulse(i, 24.0);
            channel1[i] = 0.92 * pulse(i, 29.0);
        }

        DelayMeasurementParams params = new DelayMeasurementParams();
        Correlations correlations = new Correlations();
        TimeDelayData delay = correlations.getDelay(channel0, channel1, params, sampleRate, fftLength, maxDelaySamples);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("delaySamples,delayScore");
            writer.printf(Locale.ROOT, "%.17g,%.17g%n", delay.getDelay(), delay.getDelayScore());
        }
    }

    private static double pulse(int sample, double centre) {
        double x = sample - centre;
        double envelope = Math.exp(-(x * x) / (2.0 * 10.0));
        return envelope * Math.cos(0.54 * x) + 0.01 * Math.sin(sample * 0.17);
    }
}
