package org.pamguard.port.reference;

import noiseBandMonitor.BandData;
import noiseBandMonitor.BandType;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Method;
import java.util.Locale;

/**
 * Exports noise band tables from the REAL noiseBandMonitor.BandData — the
 * scientifically critical part of the noise band monitor, since ANSI band
 * numbering mistakes silently shift every reported level to the wrong
 * frequency. Centres and edges per (bandType, minFreq, maxFreq, reference)
 * case, ascending, exactly as calculateBands builds them.
 *
 * Shared by name with cpp-engine/tools/noise_band_fixture_check.cpp.
 */
public final class NoiseBandFixtureExporter {

    private static final class BandCase {
        String name;
        BandType type;
        double minFreq, maxFreq, referenceFreq;

        BandCase(String name, BandType type, double minFreq, double maxFreq, double referenceFreq) {
            this.name = name;
            this.type = type;
            this.minFreq = minFreq;
            this.maxFreq = maxFreq;
            this.referenceFreq = referenceFreq;
        }
    }

    private NoiseBandFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: NoiseBandFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        BandCase[] cases = {
                new BandCase("third-octave-48k", BandType.THIRDOCTAVE, 10.0, 24000.0, 1000.0),
                new BandCase("third-octave-narrow", BandType.THIRDOCTAVE, 100.0, 2000.0, 1000.0),
                new BandCase("octave-96k", BandType.OCTAVE, 20.0, 48000.0, 1000.0),
                new BandCase("decidecade-48k", BandType.DECIDECADE, 10.0, 24000.0, 1000.0),
                new BandCase("decade-500k", BandType.DECADE, 10.0, 250000.0, 1000.0),
                new BandCase("tenth-octave-2k", BandType.TENTHOCTAVE, 500.0, 2000.0, 1000.0),
                new BandCase("twelfth-octave-4k", BandType.TWELTHOCTAVE, 800.0, 4000.0, 1000.0),
        };

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,band,centreHz,loEdgeHz,hiEdgeHz");
            for (BandCase bandCase : cases) {
                // BandData's constructor is package-private; reach it the way
                // the tests reach package-private members elsewhere.
                java.lang.reflect.Constructor<BandData> ctor = BandData.class.getDeclaredConstructor(
                        BandType.class, double.class, double.class, double.class);
                ctor.setAccessible(true);
                BandData bandData = ctor.newInstance(bandCase.type, bandCase.minFreq, bandCase.maxFreq,
                        bandCase.referenceFreq);
                double[] centres = bandData.getBandCentres();
                double[] los = bandData.getBandLoEdges();
                double[] his = bandData.getBandHiEdges();
                for (int i = 0; i < centres.length; i++) {
                    writer.printf(Locale.ROOT, "%s,%d,%.17g,%.17g,%.17g%n",
                            bandCase.name, i, centres[i], los[i], his[i]);
                }
            }
        }
    }
}
