package org.pamguard.port.reference;

import Filters.FilterBand;
import Filters.FilterType;
import PamDetection.RawDataUnit;
import decimator.DecimatorParams;
import decimator.DecimatorWorker;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Exports the real PAMGuard DecimatorWorker output for all three interpolation
 * modes and each FilterParams implementation consumed by the C++ runtime.
 * Two consecutive blocks exercise filter, phase, and interpolation history
 * state rather than comparing a stateless resample.
 */
public final class DecimatorFixtureExporter {

    private static final int INPUT_RATE = 48000;
    private static final int OUTPUT_RATE = 12000;
    private static final int FRACTIONAL_OUTPUT_RATE = 19200;
    private static final int UPSAMPLE_INPUT_RATE = 12000;
    private static final int UPSAMPLE_OUTPUT_RATE = 48000;
    private static final int CHANNELS = 2;
    private static final int CHUNK_SAMPLES = 32;
    private static final int CHUNKS = 2;

    private DecimatorFixtureExporter() {
    }

    private static final class FixtureCase {
        final String name;
        final DecimatorParams params;
        final int inputRate;
        final int chunks;
        final boolean streamOnly;

        FixtureCase(String name, DecimatorParams params) {
            this(name, params, INPUT_RATE, CHUNKS, false);
        }

        FixtureCase(
                String name,
                DecimatorParams params,
                int inputRate,
                int chunks,
                boolean streamOnly) {
            this.name = name;
            this.params = params;
            this.inputRate = inputRate;
            this.chunks = chunks;
            this.streamOnly = streamOnly;
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: DecimatorFixtureExporter <output.csv>");
            System.exit(2);
        }
        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,interpolation,chunk,channel,startSample,index,value");
            for (FixtureCase fixtureCase : fixtureCases()) {
                DecimatorParams params = fixtureCase.params;
                params.channelMap = (1 << CHANNELS) - 1;
                DecimatorWorker worker = new DecimatorWorker(
                        params,
                        params.channelMap,
                        fixtureCase.inputRate,
                        params.newSampleRate);
                for (int chunk = 0; chunk < fixtureCase.chunks; chunk++) {
                    for (int channel = 0; channel < CHANNELS; channel++) {
                        long startSample = (long) chunk * CHUNK_SAMPLES;
                        RawDataUnit input = new RawDataUnit(
                                1000L +
                                        startSample * 1000L /
                                                fixtureCase.inputRate,
                                1 << channel,
                                startSample,
                                CHUNK_SAMPLES);
                        input.setRawData(
                                signal(
                                        chunk,
                                        channel,
                                        fixtureCase.inputRate),
                                true);
                        RawDataUnit result = worker.process(input);
                        if (result == null) {
                            if (fixtureCase.streamOnly) {
                                continue;
                            }
                            throw new IllegalStateException(
                                    "Fixture chunk did not produce decimator output");
                        }
                        double[] data = result.getRawData();
                        for (int i = 0; i < data.length; i++) {
                            writer.printf(
                                    Locale.ROOT,
                                    "%s,%d,%d,%d,%d,%d,%.17g%n",
                                    fixtureCase.name,
                                    params.interpolation,
                                    chunk,
                                    channel,
                                    result.getStartSample(),
                                    i,
                                    data[i]);
                        }
                    }
                }
            }
        }
    }

    private static List<FixtureCase> fixtureCases() {
        List<FixtureCase> cases = new ArrayList<>();
        for (int interpolation = 0; interpolation <= 2; interpolation++) {
            DecimatorParams params = new DecimatorParams(OUTPUT_RATE, 6);
            params.interpolation = interpolation;
            cases.add(new FixtureCase("default-" + interpolation, params));
        }
        for (int interpolation = 0; interpolation <= 2; interpolation++) {
            DecimatorParams params =
                    new DecimatorParams(FRACTIONAL_OUTPUT_RATE, 6);
            params.interpolation = interpolation;
            cases.add(new FixtureCase(
                    "fractional-" + interpolation,
                    params));
        }

        DecimatorParams chebyshev = new DecimatorParams(OUTPUT_RATE, 4);
        chebyshev.interpolation = 1;
        chebyshev.filterParams.filterType = FilterType.CHEBYCHEV;
        chebyshev.filterParams.filterBand = FilterBand.LOWPASS;
        chebyshev.filterParams.lowPassFreq = 4200;
        chebyshev.filterParams.passBandRipple = 0.75;
        chebyshev.filterParams.stopBandRipple = 9.5;
        chebyshev.filterParams.chebyGamma = 4.25;
        cases.add(new FixtureCase("chebyshev-custom", chebyshev));

        DecimatorParams firWindow = new DecimatorParams(OUTPUT_RATE, 5);
        firWindow.interpolation = 0;
        firWindow.filterParams.filterType = FilterType.FIRWINDOW;
        firWindow.filterParams.filterBand = FilterBand.BANDPASS;
        firWindow.filterParams.highPassFreq = 1200;
        firWindow.filterParams.lowPassFreq = 4800;
        firWindow.filterParams.passBandRipple = 1.25;
        firWindow.filterParams.stopBandRipple = 11.0;
        firWindow.filterParams.chebyGamma = 4.0;
        cases.add(new FixtureCase("fir-window-custom", firWindow));

        DecimatorParams firArbitrary = new DecimatorParams(OUTPUT_RATE, 5);
        firArbitrary.interpolation = 2;
        firArbitrary.filterParams.filterType = FilterType.FIRARBITRARY;
        firArbitrary.filterParams.filterBand = FilterBand.BANDPASS;
        firArbitrary.filterParams.passBandRipple = 1.5;
        firArbitrary.filterParams.stopBandRipple = 12.0;
        firArbitrary.filterParams.chebyGamma = 3.5;
        firArbitrary.filterParams.arbFreqs =
                new double[] {0, 1500, 3000, 12000, 15000, 24000};
        firArbitrary.filterParams.arbGains =
                new double[] {-60, -60, 0, 0, -60, -60};
        cases.add(new FixtureCase("fir-arbitrary-custom", firArbitrary));

        DecimatorParams fft = new DecimatorParams(OUTPUT_RATE, 4);
        fft.interpolation = 1;
        fft.filterParams.filterType = FilterType.FFT;
        fft.filterParams.filterBand = FilterBand.BANDPASS;
        fft.filterParams.highPassFreq = 1500;
        fft.filterParams.lowPassFreq = 5000;
        fft.filterParams.passBandRipple = 1.75;
        fft.filterParams.stopBandRipple = 13.0;
        fft.filterParams.chebyGamma = 4.5;
        cases.add(new FixtureCase("fft-custom", fft));

        DecimatorParams none = new DecimatorParams(OUTPUT_RATE, 6);
        none.interpolation = 2;
        none.filterParams.filterType = FilterType.NONE;
        none.filterParams.stopBandRipple = 14.0;
        cases.add(new FixtureCase("none-custom", none));

        DecimatorParams upsample =
                new DecimatorParams(UPSAMPLE_OUTPUT_RATE, 4);
        upsample.interpolation = 2;
        upsample.filterParams.filterType = FilterType.CHEBYCHEV;
        upsample.filterParams.filterBand = FilterBand.LOWPASS;
        upsample.filterParams.lowPassFreq = 5000;
        upsample.filterParams.passBandRipple = 0.75;
        upsample.filterParams.stopBandRipple = 8.5;
        upsample.filterParams.chebyGamma = 4.75;
        cases.add(new FixtureCase(
                "upsample-chebyshev",
                upsample,
                UPSAMPLE_INPUT_RATE,
                3,
                true));
        return cases;
    }

    private static double[] signal(
            int chunk,
            int channel,
            int inputRate) {
        double[] signal = new double[CHUNK_SAMPLES];
        for (int i = 0; i < signal.length; i++) {
            int sample = chunk * CHUNK_SAMPLES + i;
            signal[i] =
                    0.6 * Math.sin(2.0 * Math.PI * 900.0 * sample / inputRate) +
                    0.25 * Math.sin(2.0 * Math.PI * 9000.0 * sample / inputRate) +
                    (sample == 11 + channel * 3 ? 0.8 : 0.0) +
                    channel * 0.1;
        }
        return signal;
    }
}
