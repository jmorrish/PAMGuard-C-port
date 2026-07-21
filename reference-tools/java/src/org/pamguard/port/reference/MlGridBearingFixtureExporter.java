package org.pamguard.port.reference;

import Jama.Matrix;
import Localiser.algorithms.PeakPosition2D;
import Localiser.algorithms.PeakSearch;
import pamMaths.PamVector;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.Locale;

/**
 * Exports MLGridBearingLocaliser2 fixtures.
 *
 * That class reaches ArrayManager.getArrayManager() and the current PamArray in
 * its constructor, both PamController-coupled, so its prepare/fullGridSearch
 * structure is transcribed here over explicit hydrophone positions while every
 * numerically delicate leaf stays the real PAMGuard class:
 *
 * - pamMaths.PamVector for fromHeadAndSlantR, rotate(Matrix), vecProd,
 *   getPerpendicularVector, sumComponentsSquared, dotProd, and addQuadrature.
 * - Jama.Matrix.inverse() for the array-axis rotation frame.
 * - Localiser.algorithms.PeakSearch for the interpolated 2D peak, including
 *   its wrapping rules.
 *
 * The array shape and direction logic is the same transcription used by
 * ArrayShapeFixtureExporter, reached through that class by reflection so the
 * two exporters cannot drift apart.
 *
 * Case geometry and delays are shared by name with the C++ fixture check
 * (cpp-engine/tools/ml_grid_bearing_fixture_check.cpp).
 */
public final class MlGridBearingFixtureExporter {

    private static final int ARRAY_TYPE_LINE = 2;
    private static final int ARRAY_TYPE_PLANE = 3;
    private static final int ARRAY_TYPE_VOLUME = 4;

    private static final double SPEED_OF_SOUND = 1500.0;

    /**
     * MLLineBearingLocaliser2 overrides thetaBinToAngle to return
     * pi/2 - super.thetaBinToAngle(bin). Java dispatches that virtually, and
     * prepare() calls it while building the delay table, so the override
     * changes the table as well as the reported angle.
     *
     * That subclass is selected only for a line sub-array of more than two
     * hydrophones and only when SMRUEnable.isEnable(), which gates licensed
     * extras absent from the open distribution.
     */
    private static boolean lineThetaConvention = false;

    private static double thetaBinToAngle(double bin, double[] thetaRange, double thetaStep) {
        double base = Math.PI / 2. - (thetaRange[0] + bin * thetaStep);
        return lineThetaConvention ? Math.PI / 2. - base : base;
    }

    private static final class GridCase {
        String name;
        double[][] positions;
        double[][] positionErrors;
        double speedOfSoundError;
        double timingError;
        /** Source direction the delays are generated from, or null for explicit delays. */
        double[] sourceUnitVector;
        double[] explicitDelays;

        GridCase(String name, double[][] positions, double[][] positionErrors, double speedOfSoundError,
                 double timingError, double[] sourceUnitVector) {
            this.name = name;
            this.positions = positions;
            this.positionErrors = positionErrors;
            this.speedOfSoundError = speedOfSoundError;
            this.timingError = timingError;
            this.sourceUnitVector = sourceUnitVector;
        }
    }

    private MlGridBearingFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 2) {
            System.err.println("Usage: MlGridBearingFixtureExporter <output.csv> [grid|line]");
            System.exit(2);
        }
        lineThetaConvention = args.length == 2 && args[1].equals("line");

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        try (PrintWriter writer = new PrintWriter(output)) {
            // The delays are exported rather than left for the C++ check to
            // recompute, so a mistake in the plane-wave formula cannot appear
            // on both sides at once and read as parity.
            writer.println("case,arrayType,nThetaBins,nPhiBins,thetaRadians,thetaErrorRadians,"
                    + "phiRadians,phiErrorRadians,hasPhi,peakLogLikelihood,delaysSeconds");
            for (GridCase gridCase : lineThetaConvention ? lineCaseCatalogue() : caseCatalogue()) {
                writeCase(writer, gridCase);
            }
        }
    }

    private static void writeCase(PrintWriter writer, GridCase gridCase) throws Exception {
        int nPhones = gridCase.positions.length;
        int nPairs = nPhones * (nPhones - 1) / 2;

        PamVector[] phoneVectors = new PamVector[nPhones];
        for (int i = 0; i < nPhones; i++) {
            phoneVectors[i] = new PamVector(gridCase.positions[i]);
        }
        int arrayType = arrayShape(phoneVectors);
        PamVector[] arrayAxis = arrayDirections(phoneVectors);

        double thetaStep = Math.toRadians(3);
        double phiStep = Math.toRadians(3);
        double[] thetaRange = {-Math.PI, Math.PI};
        double[] phiRange = new double[2];
        PeakSearch peakSearch = new PeakSearch(true);
        switch (arrayType) {
        case ARRAY_TYPE_LINE:
            thetaRange[0] = 0;
            phiRange[0] = phiRange[1] = 0;
            peakSearch.setWrapDim0(false);
            peakSearch.setWrapStep0(1);
            break;
        case ARRAY_TYPE_PLANE:
            phiRange[0] = 0;
            phiRange[1] = Math.PI / 2.;
            peakSearch.setWrapDim0(true);
            peakSearch.setWrapStep0(2);
            break;
        case ARRAY_TYPE_VOLUME:
            phiRange[0] = -Math.PI / 2.;
            phiRange[1] = Math.PI / 2.;
            peakSearch.setWrapDim0(true);
            peakSearch.setWrapStep0(2);
            break;
        default:
            throw new IllegalStateException("case " + gridCase.name + " is not a line, plane, or volume");
        }

        int nThetaBins = (int) Math.floor((thetaRange[1] - thetaRange[0]) / thetaStep) + 1;
        int nPhiBins = phiRange[1] == phiRange[0]
                ? 1
                : (int) Math.floor((phiRange[1] - phiRange[0]) / phiStep) + 1;

        int[][] phonePairs = new int[nPairs][2];
        int iPair = 0;
        for (int i = 0; i < nPhones; i++) {
            for (int j = i + 1; j < nPhones; j++) {
                phonePairs[iPair][0] = i;
                phonePairs[iPair][1] = j;
                iPair++;
            }
        }

        PamVector[] rotVectors = Arrays.copyOf(arrayAxis, 3);
        if (rotVectors[1] == null) {
            rotVectors[1] = rotVectors[0].getPerpendicularVector();
        }
        if (rotVectors[2] == null) {
            rotVectors[2] = rotVectors[0].vecProd(rotVectors[1]);
        }
        Matrix rotMatrix = PamVector.arrayToMatrix(rotVectors).inverse();

        double[][][] delayGrid = new double[nThetaBins][nPhiBins][nPairs];
        double[][][] delayErrorGrid = new double[nThetaBins][nPhiBins][nPairs];
        double sosErrorFactor = gridCase.speedOfSoundError / SPEED_OF_SOUND / SPEED_OF_SOUND;

        for (int iP = 0; iP < nPairs; iP++) {
            PamVector pV0 = phoneVectors[phonePairs[iP][0]];
            PamVector pV1 = phoneVectors[phonePairs[iP][1]];
            PamVector pairVector = pV1.sub(pV0);
            PamVector pairErrorVector = PamVector.addQuadrature(
                    new PamVector(gridCase.positionErrors[phonePairs[iP][1]]),
                    new PamVector(gridCase.positionErrors[phonePairs[iP][0]]));
            pairVector = pairVector.rotate(rotMatrix);
            pairErrorVector = pairErrorVector.rotate(rotMatrix);

            for (int iT = 0; iT < nThetaBins; iT++) {
                for (int iPhi = 0; iPhi < nPhiBins; iPhi++) {
                    double theta = thetaBinToAngle(iT, thetaRange, thetaStep);
                    double phi = phiRange[0] + iPhi * phiStep;
                    PamVector bearingVector = PamVector.fromHeadAndSlantR(Math.PI / 2. - theta, phi);
                    delayGrid[iT][iPhi][iP] = -bearingVector.dotProd(pairVector) / SPEED_OF_SOUND;
                    double e1 = pairErrorVector.sumComponentsSquared(bearingVector) / SPEED_OF_SOUND;
                    double e2 = pairVector.dotProd(bearingVector) * sosErrorFactor;
                    double e3 = gridCase.timingError;
                    delayErrorGrid[iT][iPhi][iP] = Math.sqrt(e1 * e1 + e2 * e2 + e3 * e3);
                }
            }
        }

        double[] delays = gridCase.explicitDelays != null
                ? gridCase.explicitDelays
                : delaysFromDirection(phoneVectors, phonePairs, gridCase.sourceUnitVector);

        double[][] likelihood = new double[nThetaBins][nPhiBins];
        for (int i = 0; i < nThetaBins; i++) {
            for (int j = 0; j < nPhiBins; j++) {
                likelihood[i][j] = calcLValue(delays, delayGrid[i][j], delayErrorGrid[i][j]);
            }
        }

        PeakPosition2D peakResult = peakSearch.interpolatedPeakSearch(likelihood);
        double[] errors = getErrors((int) Math.round(peakResult.getBin0()), (int) Math.round(peakResult.getBin1()),
                delays, delayGrid, delayErrorGrid, nThetaBins, nPhiBins, thetaStep, phiStep);

        double theta = thetaBinToAngle(peakResult.getBin0(), thetaRange, thetaStep);
        double phi = phiRange[0] + peakResult.getBin1() * phiStep;
        boolean hasPhi = arrayType != ARRAY_TYPE_LINE;

        StringBuilder delayText = new StringBuilder();
        for (int i = 0; i < delays.length; i++) {
            if (i > 0) {
                delayText.append(';');
            }
            delayText.append(String.format(Locale.ROOT, "%.17g", delays[i]));
        }

        writer.printf(Locale.ROOT, "%s,%d,%d,%d,%.17g,%.17g,%.17g,%.17g,%d,%.17g,%s%n",
                gridCase.name, arrayType, nThetaBins, nPhiBins,
                theta, errors[0],
                hasPhi ? phi : 0.0, hasPhi ? errors[1] : 0.0,
                hasPhi ? 1 : 0, peakResult.getHeight(), delayText);
    }

    /**
     * Delays for a plane wave arriving from a unit direction: the projection of
     * each pair's baseline onto the direction, divided by the speed of sound,
     * with the same sign convention as the delay grid.
     */
    private static double[] delaysFromDirection(PamVector[] phoneVectors, int[][] phonePairs, double[] direction) {
        PamVector unit = new PamVector(direction).getUnitVector();
        double[] delays = new double[phonePairs.length];
        for (int i = 0; i < phonePairs.length; i++) {
            PamVector pairVector = phoneVectors[phonePairs[i][1]].sub(phoneVectors[phonePairs[i][0]]);
            delays[i] = -unit.dotProd(pairVector) / SPEED_OF_SOUND;
        }
        return delays;
    }

    private static double calcLValue(double[] delays, double[] delayRow, double[] errorRow) {
        double val = 0;
        for (int i = 0; i < delays.length; i++) {
            val -= 0.5 * Math.pow((delays[i] - delayRow[i]) / errorRow[i], 2);
        }
        return val;
    }

    private static double[] getErrors(int thetaBin, int phiBin, double[] delays, double[][][] delayGrid,
                                      double[][][] delayErrorGrid, int nThetaBins, int nPhiBins,
                                      double thetaStep, double phiStep) {
        double[] errors = new double[2];
        if (nThetaBins >= 3) {
            thetaBin = Math.min(Math.max(1, thetaBin), nThetaBins - 2);
            errors[0] = getCurvature(
                    calcLValue(delays, delayGrid[thetaBin - 1][phiBin], delayErrorGrid[thetaBin - 1][phiBin]),
                    calcLValue(delays, delayGrid[thetaBin][phiBin], delayErrorGrid[thetaBin][phiBin]),
                    calcLValue(delays, delayGrid[thetaBin + 1][phiBin], delayErrorGrid[thetaBin + 1][phiBin]))
                    * thetaStep;
        }
        if (nPhiBins >= 3) {
            phiBin = Math.min(Math.max(1, phiBin), nPhiBins - 2);
            errors[1] = getCurvature(
                    calcLValue(delays, delayGrid[thetaBin][phiBin - 1], delayErrorGrid[thetaBin][phiBin - 1]),
                    calcLValue(delays, delayGrid[thetaBin][phiBin], delayErrorGrid[thetaBin][phiBin]),
                    calcLValue(delays, delayGrid[thetaBin][phiBin + 1], delayErrorGrid[thetaBin][phiBin + 1]))
                    * phiStep;
        }
        return errors;
    }

    private static double getCurvature(double v1, double v2, double v3) {
        double a2 = 1. / (2 * v2 - v1 - v3);
        if (a2 >= 0) {
            return Math.sqrt(a2);
        }
        return 0;
    }

    private static int arrayShape(PamVector[] positions) throws Exception {
        Method method = ArrayShapeFixtureExporter.class.getDeclaredMethod(
                "getArrayShape", PamVector[].class, int[].class);
        method.setAccessible(true);
        return (Integer) method.invoke(null, positions, null);
    }

    private static PamVector[] arrayDirections(PamVector[] positions) throws Exception {
        Method method = ArrayShapeFixtureExporter.class.getDeclaredMethod(
                "getArrayDirections", PamVector[].class, int[].class);
        method.setAccessible(true);
        return (PamVector[]) method.invoke(null, positions, null);
    }

    /**
     * The line-convention subclass is only ever selected for line sub-arrays,
     * so its catalogue covers those alone.
     */
    private static GridCase[] lineCaseCatalogue() {
        double[][] linePositions = {
                {0.0, 0.0, 0.0},
                {0.0, 2.0, 0.0},
                {0.0, 4.0, 0.0},
        };
        double[][] lineFour = {
                {0.0, 0.0, 0.0},
                {0.0, 1.5, 0.0},
                {0.0, 3.0, 0.0},
                {0.0, 4.5, 0.0},
        };
        double[][] errors3 = {
                {0.1, 0.1, 0.1},
                {0.1, 0.1, 0.1},
                {0.1, 0.1, 0.1},
        };
        double[][] errors4 = {
                {0.1, 0.1, 0.1},
                {0.1, 0.1, 0.1},
                {0.1, 0.1, 0.1},
                {0.1, 0.1, 0.1},
        };
        return new GridCase[]{
                new GridCase("line-broadside", linePositions, errors3, 0.0, 1.0e-5, new double[]{1.0, 0.0, 0.0}),
                new GridCase("line-oblique", linePositions, errors3, 0.0, 1.0e-5, new double[]{1.0, 1.0, 0.0}),
                new GridCase("line-endfire", linePositions, errors3, 0.0, 1.0e-5, new double[]{0.0, 1.0, 0.0}),
                new GridCase("line-four-oblique", lineFour, errors4, 20.0, 2.0e-5, new double[]{2.0, 1.0, 0.0}),
        };
    }

    private static GridCase[] caseCatalogue() {
        double[][] planePositions = {
                {0.0, 0.0, 0.0},
                {2.0, 0.0, 0.0},
                {0.0, 3.0, 0.0},
                {2.0, 3.0, 0.0},
        };
        double[][] volumePositions = {
                {0.0, 0.0, 0.0},
                {2.0, 0.0, 0.0},
                {0.0, 3.0, 0.0},
                {0.5, 1.0, 2.5},
        };
        double[][] linePositions = {
                {0.0, 0.0, 0.0},
                {0.0, 2.0, 0.0},
                {0.0, 4.0, 0.0},
        };
        double[][] errors4 = {
                {0.1, 0.1, 0.1},
                {0.1, 0.1, 0.1},
                {0.1, 0.1, 0.1},
                {0.1, 0.1, 0.1},
        };
        double[][] errors3 = {
                {0.1, 0.1, 0.1},
                {0.1, 0.1, 0.1},
                {0.1, 0.1, 0.1},
        };
        double[][] asymmetricErrors4 = {
                {0.05, 0.2, 0.3},
                {0.1, 0.1, 0.1},
                {0.4, 0.05, 0.15},
                {0.2, 0.2, 0.02},
        };

        return new GridCase[]{
                new GridCase("plane-broadside", planePositions, errors4, 0.0, 1.0e-5, new double[]{0.0, 1.0, 0.0}),
                new GridCase("plane-diagonal", planePositions, errors4, 0.0, 1.0e-5, new double[]{1.0, 1.0, 0.0}),
                new GridCase("plane-endfire", planePositions, errors4, 0.0, 1.0e-5, new double[]{1.0, 0.0, 0.0}),
                new GridCase("plane-elevated", planePositions, errors4, 0.0, 1.0e-5, new double[]{1.0, 2.0, 1.0}),
                new GridCase("plane-sos-error", planePositions, errors4, 30.0, 1.0e-5, new double[]{1.0, 1.0, 0.0}),
                new GridCase("plane-asymmetric-errors", planePositions, asymmetricErrors4, 10.0, 2.0e-5,
                        new double[]{1.0, 2.0, 0.0}),
                new GridCase("volume-above", volumePositions, errors4, 0.0, 1.0e-5, new double[]{0.0, 1.0, 1.0}),
                new GridCase("volume-below", volumePositions, errors4, 0.0, 1.0e-5, new double[]{0.5, 1.0, -1.5}),
                new GridCase("volume-sos-error", volumePositions, errors4, 25.0, 3.0e-5, new double[]{-1.0, 2.0, 0.5}),
                new GridCase("line-broadside", linePositions, errors3, 0.0, 1.0e-5, new double[]{1.0, 0.0, 0.0}),
                new GridCase("line-oblique", linePositions, errors3, 0.0, 1.0e-5, new double[]{1.0, 1.0, 0.0}),
        };
    }
}
