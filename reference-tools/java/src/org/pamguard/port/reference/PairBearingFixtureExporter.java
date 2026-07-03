package org.pamguard.port.reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.Arrays;
import java.util.Locale;

/**
 * Exports pair bearing fixtures mirroring
 * Localiser.algorithms.timeDelayLocalisers.bearingLoc.PairBearingLocaliser.localise()
 * line for line. The real class cannot be driven headless because prepare()
 * pulls the ArrayManager/PamController singletons, so the localise() maths is
 * reproduced here verbatim (including the clamp, the error propagation
 * formula, and the three-delay bodge) and kept traceable to the Java source.
 *
 * Case parameters are shared by name with the C++ fixture check
 * (cpp-engine/tools/pair_bearing_fixture_check.cpp).
 */
public final class PairBearingFixtureExporter {

    private static final class PairCase {
        String name;
        double spacing;
        double spacingError;
        double speedOfSound;
        double speedOfSoundError;
        double timingError;
        double wobbleRadians;
        double[] delays;

        PairCase(String name, double spacing, double spacingError, double speedOfSound,
                 double speedOfSoundError, double timingError, double wobbleRadians, double... delays) {
            this.name = name;
            this.spacing = spacing;
            this.spacingError = spacingError;
            this.speedOfSound = speedOfSound;
            this.speedOfSoundError = speedOfSoundError;
            this.timingError = timingError;
            this.wobbleRadians = wobbleRadians;
            this.delays = delays;
        }
    }

    private PairBearingFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: PairBearingFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,angleRadians,errorRadians");
            for (PairCase pairCase : caseCatalogue()) {
                double[][] ans = localise(pairCase);
                writer.printf(Locale.ROOT, "%s,%.17g,%.17g%n", pairCase.name, ans[0][0], ans[1][0]);
            }
        }
    }

    private static PairCase[] caseCatalogue() {
        return new PairCase[]{
                new PairCase("broadside", 3.0, 0.01, 1500.0, 5.0, 1.0e-5, 0.001, 0.0),
                new PairCase("mid-cone-positive", 3.0, 0.01, 1500.0, 5.0, 1.0e-5, 0.001, 0.001),
                new PairCase("mid-cone-negative", 3.0, 0.01, 1500.0, 5.0, 1.0e-5, 0.001, -0.001),
                new PairCase("near-endfire", 3.0, 0.01, 1500.0, 5.0, 1.0e-5, 0.001, 0.00199),
                new PairCase("endfire-clamp", 3.0, 0.01, 1500.0, 5.0, 1.0e-5, 0.001, 0.0025),
                new PairCase("negative-spacing", -3.0, 0.01, 1500.0, 5.0, 1.0e-5, 0.001, 0.001),
                new PairCase("vancouver-three-delays", 3.0, 0.01, 1500.0, 5.0, 1.0e-5, 0.001, 0.9, 0.001, 0.9),
        };
    }

    /**
     * Verbatim transcription of PairBearingLocaliser.localise() with the
     * prepared fields supplied by the case instead of ArrayManager.
     */
    private static double[][] localise(PairCase pairCase) {
        double[] delays = pairCase.delays;
        double spacing = pairCase.spacing;
        double spacingError = pairCase.spacingError;
        double speedOfSound = pairCase.speedOfSound;
        double speedOfSoundError = pairCase.speedOfSoundError;
        double timingError = pairCase.timingError;
        double wobbleRadians = pairCase.wobbleRadians;

        double[][] ans = new double[2][1];
        if (delays == null || delays.length == 0) {
            return null;
        }
        if (delays.length == 3) {
            delays = Arrays.copyOfRange(delays, 1, 2);
        }

        double ct = speedOfSound * delays[0] / spacing;
        ct = Math.max(-1., Math.min(1., ct));

        double angle = Math.acos(ct);

        double e1 = speedOfSound * timingError;
        double e2 = speedOfSound * delays[0] / spacing * spacingError;
        double e3 = delays[0] * speedOfSoundError;
        double error = (e1 * e1 + e2 * e2 + e3 * e3) / spacing / Math.sin(angle);
        error += wobbleRadians;
        error = Math.sqrt(error);

        ans[0][0] = angle;
        ans[1][0] = error;

        return ans;
    }
}
