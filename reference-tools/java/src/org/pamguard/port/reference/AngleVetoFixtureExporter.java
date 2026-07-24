package org.pamguard.port.reference;

import angleVetoes.AngleVeto;
import angleVetoes.AngleVetoParameters;
import angleVetoes.AngleVetoes;
import sun.misc.Unsafe;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.Locale;

/** Executes the real PAMGuard AngleVetoes pass/fail implementation. */
public final class AngleVetoFixtureExporter {
    private AngleVetoFixtureExporter() {
    }

    private static final class TestCase {
        final String name;
        final double angle;
        final AngleVeto[] vetoes;

        TestCase(String name, double angle, AngleVeto... vetoes) {
            this.name = name;
            this.angle = angle;
            this.vetoes = vetoes;
        }
    }

    public static void main(String[] args) throws Exception {
        Locale.setDefault(Locale.ROOT);
        if (args.length != 1) {
            System.err.println("Usage: AngleVetoFixtureExporter <output.csv>");
            System.exit(2);
        }
        AngleVeto a = new AngleVeto(0x1, 35.0, 55.0);
        AngleVeto b = new AngleVeto(0x8, 75.0, 85.0);
        AngleVeto reversed = new AngleVeto(0, 90.0, 20.0);
        TestCase[] cases = {
            new TestCase("empty-default", 0.0),
            new TestCase("below", 34.999, a),
            new TestCase("start-inclusive", 35.0, a),
            new TestCase("inside", 42.0, a),
            new TestCase("end-inclusive", 55.0, a),
            new TestCase("above", 55.001, a),
            new TestCase("negative-absolute", -42.0, a),
            new TestCase("channels-ignored", 42.0,
                    new AngleVeto(0, 35.0, 55.0)),
            new TestCase("second-veto", 80.0, a, b),
            new TestCase("both-pass", 65.0, a, b),
            new TestCase("reversed-range", 42.0, reversed)
        };

        try (PrintWriter out = new PrintWriter(new File(args[0]))) {
            out.println("name,angleDegrees,expectedPass,vetoCount,channels0,start0,end0,channels1,start1,end1");
            for (TestCase test : cases) {
                AngleVetoParameters parameters = new AngleVetoParameters();
                for (AngleVeto veto : test.vetoes) {
                    parameters.addVeto(veto);
                }
                AngleVetoes implementation = allocate(AngleVetoes.class);
                implementation.setAngleVetoParameters(parameters);
                boolean pass = implementation.passAllVetoes(test.angle, false);
                out.printf("%s,%.17g,%s,%d", test.name, test.angle, pass,
                        test.vetoes.length);
                for (int i = 0; i < 2; i++) {
                    if (i < test.vetoes.length) {
                        AngleVeto veto = test.vetoes[i];
                        out.printf(",%d,%.17g,%.17g", veto.getChannels(),
                                veto.getStartAngle(), veto.getEndAngle());
                    }
                    else {
                        out.print(",,,");
                    }
                }
                out.println();
            }
        }
    }

    private static <T> T allocate(Class<T> type) throws Exception {
        Field field = Unsafe.class.getDeclaredField("theUnsafe");
        field.setAccessible(true);
        Unsafe unsafe = (Unsafe) field.get(null);
        return type.cast(unsafe.allocateInstance(type));
    }
}
