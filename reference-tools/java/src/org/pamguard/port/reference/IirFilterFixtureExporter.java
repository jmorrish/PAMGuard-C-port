package org.pamguard.port.reference;

import Filters.FastIIRFilter;
import Filters.FilterBand;
import Filters.FilterParams;
import Filters.FilterType;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

/**
 * Exports IIR filter fixtures by driving the REAL PAMGuard classes end to end
 * — ButterworthMethod/ChebyshevMethod design and the FastIIRFilter runtime the
 * click detector actually uses (`createFilter` returns FastIIRFilter, not the
 * older IirfFilter). Zero transcription.
 *
 * Rows: `input` carries the shared test signal; each case then carries its
 * filtered output sample by sample, so the C++ check replays the same bytes.
 * The signal is an impulse, two tones, and deterministic pseudo-noise, which
 * exercises transient response, passband, and stopband together.
 *
 * Shared by name with cpp-engine/tools/iir_filter_fixture_check.cpp.
 */
public final class IirFilterFixtureExporter {

    private static final int SAMPLES = 512;
    private static final double SAMPLE_RATE = 48000.0;

    private static final class FilterCase {
        String name;
        FilterParams params;

        FilterCase(String name, FilterType type, FilterBand band, int order,
                   float highPassFreq, float lowPassFreq, double rippleDb) {
            this.name = name;
            params = new FilterParams();
            params.filterType = type;
            params.filterBand = band;
            params.filterOrder = order;
            params.highPassFreq = highPassFreq;
            params.lowPassFreq = lowPassFreq;
            params.passBandRipple = rippleDb;
        }
    }

    private IirFilterFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: IirFilterFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        final double[] signal = testSignal();

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,index,value");
            for (int i = 0; i < signal.length; i++) {
                writer.printf(Locale.ROOT, "input,%d,%.17g%n", i, signal[i]);
            }
            for (FilterCase filterCase : caseCatalogue()) {
                FastIIRFilter filter = new FastIIRFilter(0, SAMPLE_RATE, filterCase.params);
                double[] filtered = new double[signal.length];
                filter.runFilter(signal, filtered);
                for (int i = 0; i < filtered.length; i++) {
                    writer.printf(Locale.ROOT, "%s,%d,%.17g%n", filterCase.name, i, filtered[i]);
                }
            }
        }
    }

    private static double[] testSignal() {
        double[] signal = new double[SAMPLES];
        long seed = 987654321L;
        for (int i = 0; i < SAMPLES; i++) {
            // xorshift pseudo-noise: identical arithmetic is trivial in C++.
            seed ^= seed << 13;
            seed ^= seed >>> 7;
            seed ^= seed << 17;
            double noise = ((double) (seed % 10000L)) / 10000.0 * 0.05;
            double lowTone = 0.4 * Math.sin(2.0 * Math.PI * 300.0 * i / SAMPLE_RATE);
            double highTone = 0.3 * Math.sin(2.0 * Math.PI * 6000.0 * i / SAMPLE_RATE);
            signal[i] = lowTone + highTone + noise + (i == 32 ? 1.0 : 0.0);
        }
        return signal;
    }

    private static FilterCase[] caseCatalogue() {
        return new FilterCase[]{
                // The click detector's own defaults, the parity gap this closes.
                new FilterCase("click-prefilter-hp4-500", FilterType.BUTTERWORTH, FilterBand.HIGHPASS, 4, 500.0F, 0.0F, 2.0),
                new FilterCase("click-trigger-hp2-2000", FilterType.BUTTERWORTH, FilterBand.HIGHPASS, 2, 2000.0F, 0.0F, 2.0),
                new FilterCase("butter-lp6-8000", FilterType.BUTTERWORTH, FilterBand.LOWPASS, 6, 0.0F, 8000.0F, 2.0),
                // Odd order exercises the single (non-pair) stage.
                new FilterCase("butter-lp3-4000", FilterType.BUTTERWORTH, FilterBand.LOWPASS, 3, 0.0F, 4000.0F, 2.0),
                new FilterCase("butter-hp5-1000", FilterType.BUTTERWORTH, FilterBand.HIGHPASS, 5, 1000.0F, 0.0F, 2.0),
                // Band filters: highPassFreq is the LOWER edge, PAMGuard-style.
                new FilterCase("butter-bp4-2000-5000", FilterType.BUTTERWORTH, FilterBand.BANDPASS, 4, 2000.0F, 5000.0F, 2.0),
                new FilterCase("butter-bs4-2000-5000", FilterType.BUTTERWORTH, FilterBand.BANDSTOP, 4, 2000.0F, 5000.0F, 2.0),
                new FilterCase("cheby-lp4-6000-r2", FilterType.CHEBYCHEV, FilterBand.LOWPASS, 4, 0.0F, 6000.0F, 2.0),
                new FilterCase("cheby-hp3-1500-r1", FilterType.CHEBYCHEV, FilterBand.HIGHPASS, 3, 1500.0F, 0.0F, 1.0),
                new FilterCase("cheby-bp4-1000-4000-r2", FilterType.CHEBYCHEV, FilterBand.BANDPASS, 4, 1000.0F, 4000.0F, 2.0),
        };
    }
}
