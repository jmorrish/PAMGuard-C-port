package org.pamguard.port.reference;

import Jama.Matrix;
import Jama.QRDecomposition;
import pamMaths.PamVector;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

/**
 * Exports LSQ bearing fixtures for
 * Localiser.algorithms.timeDelayLocalisers.bearingLoc.LSQBearingLocaliser.
 *
 * prepare() cannot be driven headless (ArrayManager/PamController singletons),
 * so its matrix construction is transcribed here with hydrophone positions and
 * per-pair separation error vectors supplied as case constants, while the
 * numerically significant pieces — Jama QRDecomposition/Matrix and PamVector —
 * are the real PAMGuard-classpath classes. localise() and logLikelihood() are
 * transcribed verbatim, including the final error values coming from the last
 * (20 degree) curvature iteration and logLikelihood overwriting chi per pair
 * rather than accumulating.
 *
 * Case parameters are shared by name with the C++ fixture check
 * (cpp-engine/tools/lsq_bearing_fixture_check.cpp).
 */
public final class LsqBearingFixtureExporter {

    private static final class LsqCase {
        String name;
        double[][] hydrophones;
        double[][] pairErrorVectors;
        double speedOfSound;
        double speedOfSoundError;
        double timingError;
        double[] delays;

        LsqCase(String name, double[][] hydrophones, double[][] pairErrorVectors,
                double speedOfSound, double speedOfSoundError, double timingError, double[] delays) {
            this.name = name;
            this.hydrophones = hydrophones;
            this.pairErrorVectors = pairErrorVectors;
            this.speedOfSound = speedOfSound;
            this.speedOfSoundError = speedOfSoundError;
            this.timingError = timingError;
            this.delays = delays;
        }
    }

    private LsqBearingFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: LsqBearingFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,azimuthRadians,elevationRadians,azimuthErrorRadians,elevationErrorRadians");
            for (LsqCase lsqCase : caseCatalogue()) {
                double[][] angs = localise(lsqCase);
                writer.printf(Locale.ROOT, "%s,%.17g,%.17g,%.17g,%.17g%n",
                        lsqCase.name, angs[0][0], angs[0][1], angs[1][0], angs[1][1]);
            }
        }
    }

    private static LsqCase[] caseCatalogue() {
        // Any 3-hydrophone pair set is rank deficient (the third baseline is
        // exactly the difference of the other two), and collinear/coplanar
        // 4-hydrophone sets are too: the real Jama QR solve throws "Matrix is
        // rank deficient" for them (pinned on the C++ side). LSQ cases need at
        // least four non-coplanar hydrophones.
        return new LsqCase[]{
                new LsqCase("volumetric-4ch",
                        new double[][]{{0.0, 0.0, 0.0}, {2.5, 0.0, 0.0}, {0.0, 2.5, 0.0}, {0.0, 0.0, 2.5}},
                        new double[][]{
                                {0.018, 0.003, 0.002},
                                {0.002, 0.02, 0.004},
                                {0.003, 0.002, 0.022},
                                {0.012, 0.011, 0.002},
                                {0.013, 0.003, 0.012},
                                {0.002, 0.014, 0.015}},
                        1500.0, 5.0, 1.0e-5,
                        new double[]{0.0008, -0.0009, 0.0005, -0.0011, -0.0002, 0.0009}),
                new LsqCase("towed-4ch",
                        new double[][]{{0.0, 0.0, 0.0}, {0.05, 3.0, 0.02}, {-0.03, 6.0, 0.04}, {0.02, 9.0, 0.5}},
                        new double[][]{
                                {0.002, 0.02, 0.005},
                                {0.001, 0.03, 0.004},
                                {0.003, 0.025, 0.006},
                                {0.002, 0.02, 0.004},
                                {0.001, 0.03, 0.008},
                                {0.002, 0.025, 0.01}},
                        1500.0, 5.0, 1.0e-5,
                        new double[]{-0.0012, -0.0026, -0.0014, -0.0015, -0.0028, -0.0013}),
                new LsqCase("volumetric-4ch-alt-weights",
                        new double[][]{{0.0, 0.0, 0.0}, {2.5, 0.0, 0.0}, {0.0, 2.5, 0.0}, {0.0, 0.0, 2.5}},
                        new double[][]{
                                {0.004, 0.001, 0.001},
                                {0.001, 0.03, 0.002},
                                {0.002, 0.001, 0.012},
                                {0.02, 0.018, 0.003},
                                {0.006, 0.002, 0.02},
                                {0.001, 0.008, 0.007}},
                        1480.0, 10.0, 5.0e-5,
                        new double[]{0.0008, -0.0009, 0.0005, -0.0011, -0.0002, 0.0009}),
                new LsqCase("wide-aperture-4ch",
                        new double[][]{{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {0.0, 12.0, 0.0}, {3.0, 4.0, 8.0}},
                        new double[][]{
                                {0.03, 0.005, 0.004},
                                {0.006, 0.04, 0.005},
                                {0.012, 0.01, 0.03},
                                {0.025, 0.02, 0.006},
                                {0.02, 0.006, 0.024},
                                {0.005, 0.022, 0.02}},
                        1500.0, 5.0, 1.0e-5,
                        new double[]{0.004, -0.005, 0.002, -0.006, -0.001, 0.0045}),
        };
    }

    private static double[][] localise(LsqCase lsqCase) {
        int nHyd = lsqCase.hydrophones.length;
        int nDelay = (nHyd * (nHyd - 1)) / 2;
        Matrix weightedHydrophoneVectors = new Matrix(nDelay, 3);
        Matrix hydrophoneVectors = new Matrix(nDelay, 3);
        Matrix hydrophoneErrorVectors = new Matrix(nDelay, 3);
        double[] hydrophoneSpacing = new double[nDelay];
        double[] fitWeights = new double[nDelay];
        double c = lsqCase.speedOfSound;
        int iRow = 0;
        for (int i = 0; i < nHyd; i++) {
            PamVector vi = new PamVector(lsqCase.hydrophones[i]);
            for (int j = i + 1; j < nHyd; j++) {
                PamVector vj = new PamVector(lsqCase.hydrophones[j]);
                PamVector v = vj.sub(vi);
                hydrophoneSpacing[iRow] = v.norm();
                PamVector uv = v.getUnitVector();
                PamVector errorVec = new PamVector(lsqCase.pairErrorVectors[iRow]);
                double errorComponent = uv.dotProd(errorVec);
                fitWeights[iRow] = Math.pow(v.norm() / errorComponent, 2);
                for (int e = 0; e < 3; e++) {
                    weightedHydrophoneVectors.set(iRow, e, v.getElement(e) / c * fitWeights[iRow]);
                    hydrophoneVectors.set(iRow, e, v.getElement(e) / c);
                    hydrophoneErrorVectors.set(iRow, e, errorVec.getElement(e) / c);
                }
                iRow++;
            }
        }
        QRDecomposition qrHydrophones = new QRDecomposition(weightedHydrophoneVectors);

        double[] delays = lsqCase.delays;
        Matrix normDelays = new Matrix(delays.length, 1);
        for (int i = 0; i < delays.length; i++) {
            normDelays.set(i, 0, -delays[i] * fitWeights[i]);
        }
        Matrix soln2 = qrHydrophones.solve(normDelays);
        double[][] angs = new double[2][2];
        PamVector v = new PamVector(soln2.get(0, 0), soln2.get(1, 0), soln2.get(2, 0));
        double m = v.normalise();
        angs[0][0] = Math.PI / 2. - Math.atan2(v.getElement(0), v.getElement(1));
        angs[0][1] = Math.asin(v.getElement(2));

        double oneDeg = Math.PI / 180.;
        double testDeg = 5;
        double[][] er = new double[2][20];
        for (int i = 0; i < 20; i++) {
            testDeg = 1 + i;
            double aDiff = testDeg * oneDeg;
            double l1, l2, l3, l1a, l3a;
            l1 = logLikelihood(lsqCase, hydrophoneVectors, hydrophoneErrorVectors, delays, angs[0][0] - aDiff, angs[0][1]);
            l2 = logLikelihood(lsqCase, hydrophoneVectors, hydrophoneErrorVectors, delays, angs[0][0], angs[0][1]);
            l3 = logLikelihood(lsqCase, hydrophoneVectors, hydrophoneErrorVectors, delays, angs[0][0] + aDiff, angs[0][1]);
            er[0][i] = angs[1][0] = Math.sqrt(1. / (l1 + l3 - 2 * l2)) * aDiff;
            l1a = logLikelihood(lsqCase, hydrophoneVectors, hydrophoneErrorVectors, delays, angs[0][0], angs[0][1] - aDiff);
            l3a = logLikelihood(lsqCase, hydrophoneVectors, hydrophoneErrorVectors, delays, angs[0][0], angs[0][1] + aDiff);
            er[1][i] = angs[1][1] = Math.sqrt(1. / (l1a + l3a - 2 * l2)) * aDiff;
        }

        return angs;
    }

    private static double logLikelihood(LsqCase lsqCase, Matrix hydrophoneVectors, Matrix hydrophoneErrorVectors,
                                        double[] delays, double angle0, double angle1) {
        Matrix whaleVec = new Matrix(3, 1);
        whaleVec.set(0, 0, Math.cos(angle1) * Math.cos(angle0));
        whaleVec.set(1, 0, Math.cos(angle1) * Math.sin(angle0));
        whaleVec.set(2, 0, Math.sin(angle1));

        Matrix times = hydrophoneVectors.times(whaleVec);
        Matrix timeErrors = hydrophoneErrorVectors.times(whaleVec);
        double c = lsqCase.speedOfSound;
        double dc = lsqCase.speedOfSoundError;
        Matrix timeErrors2 = times.times(dc / c / c);
        double chi = 0;
        for (int i = 0; i < times.getRowDimension(); i++) {
            double expectedVariance = Math.pow(timeErrors.get(i, 0), 2) + Math.pow(timeErrors2.get(i, 0), 2)
                    + Math.pow(lsqCase.timingError, 2);
            chi = Math.pow((times.get(i, 0) + delays[i]), 2) / expectedVariance;
        }
        return chi / 2;
    }
}
