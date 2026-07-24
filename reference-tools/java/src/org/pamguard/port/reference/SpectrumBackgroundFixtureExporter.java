package org.pamguard.port.reference;

import PamUtils.complex.ComplexArray;
import PamguardMVC.PamProcess;
import Spectrogram.SpectrumBackground;
import fftManager.FFTDataBlock;
import fftManager.FFTDataUnit;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

/** Drives the real Spectrogram.SpectrumBackground implementation. */
public final class SpectrumBackgroundFixtureExporter {
    private static final class FixtureProcess extends PamProcess {
        FixtureProcess(float sampleRate) {
            super(null, null, "Spectrum background fixture");
            setSampleRate(sampleRate, false);
        }

        @Override public void pamStart() {
        }

        @Override public void pamStop() {
        }
    }

    private SpectrumBackgroundFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        Locale.setDefault(Locale.ROOT);
        if (args.length != 1) {
            System.err.println("Usage: SpectrumBackgroundFixtureExporter <output.csv>");
            System.exit(2);
        }

        FixtureProcess parent = new FixtureProcess(8.0f);
        FFTDataBlock source = new FFTDataBlock("fixture", parent, 1, 2, 8);
        SpectrumBackground background = new SpectrumBackground(source, 0);
        if (!background.prepareS(1.0)) {
            throw new IllegalStateException("SpectrumBackground.prepareS failed");
        }

        File output = new File(args[0]);
        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("# sampleRateHz,8,fftHop,2,fftLength,8,timeConstantSeconds,1");
            writer.println("slice,bin,inputRe,inputIm,backgroundPower");
            for (int slice = 0; slice < 8; slice++) {
                double[] complex = new double[8];
                for (int bin = 0; bin < 4; bin++) {
                    double re = 0.4 + slice * 0.3 + bin * 0.11;
                    double im = -0.2 + slice * 0.07 - bin * 0.05;
                    if (slice == 4 && bin == 1) {
                        re = Double.NaN;
                    }
                    if (slice == 5 && bin == 2) {
                        im = Double.POSITIVE_INFINITY;
                    }
                    complex[bin * 2] = re;
                    complex[bin * 2 + 1] = im;
                }
                FFTDataUnit unit = new FFTDataUnit(
                        slice * 250L, 1, slice * 2L, 8,
                        new ComplexArray(complex), slice);
                double[] actual = background.process(unit);
                for (int bin = 0; bin < actual.length; bin++) {
                    writer.printf(Locale.ROOT, "%d,%d,%.17g,%.17g,%.17g%n",
                            slice, bin, complex[bin * 2],
                            complex[bin * 2 + 1], actual[bin]);
                }
            }
        }
    }
}
