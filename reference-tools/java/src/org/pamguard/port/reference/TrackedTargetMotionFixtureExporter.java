package org.pamguard.port.reference;

import Localiser.algorithms.genericLocaliser.Chi2Bearings;
import Localiser.algorithms.genericLocaliser.leastSquares.LeastSquares;
import Localiser.algorithms.locErrors.SimpleError;
import pamMaths.PamQuaternion;
import pamMaths.PamVector;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Deterministic oracle for the 2-D bearing-group Least Squares path used by
 * TrackedClickLocaliser -> GeneralGroupLocaliser ->
 * DetectionGroupLocaliser2.
 *
 * The numerical result is produced by the pinned PAMGuard Chi2Bearings,
 * LeastSquares, LinFit, PamVector, PamQuaternion, and SimpleError classes.
 * Building a real TMGroupLocInfo requires live PamController, ArrayManager,
 * GPSDataBlock, and PamDataUnit state. Consequently, the fixture makes its
 * already-resolved ambiguity sides, Cartesian origins, headings, and bearings
 * explicit. The even sub-detection selection and final range/height filters
 * below are literal extractions of TMGroupLocInfo.copySubDetections and
 * GeneralGroupLocaliser.resultsFilterOK / DetectionGroupLocaliser2's runaway
 * guard, respectively.
 */
public final class TrackedTargetMotionFixtureExporter {

    private static final String ORACLE_VERSION = "2.02.18e";
    private static final String ORACLE_COMMIT =
            "dca55c81ef6f1498a8a3b926c69e7182afb915ee";
    private static final double EARTH_RADIUS_METRES = 6_371_000.0;

    private static final class Observation {
        final double x;
        final double y;
        final double z;
        final double headingDegrees;
        final double bearingDegrees;

        Observation(
                double x,
                double y,
                double z,
                double headingDegrees,
                double bearingDegrees) {
            this.x = x;
            this.y = y;
            this.z = z;
            this.headingDegrees = headingDegrees;
            this.bearingDegrees = bearingDegrees;
        }
    }

    private static final class FixtureCase {
        final String name;
        final String ambiguityGroup;
        final int ambiguitySide;
        final Observation[] observations;
        final int maxLocalisationPoints;
        final double perpendicularDistanceMetres;
        final double heightMetres;
        final double maxRangeMetres;
        final double minHeightMetres;
        final double maxHeightMetres;

        FixtureCase(
                String name,
                String ambiguityGroup,
                int ambiguitySide,
                Observation[] observations,
                int maxLocalisationPoints,
                double perpendicularDistanceMetres,
                double heightMetres,
                double maxRangeMetres,
                double minHeightMetres,
                double maxHeightMetres) {
            this.name = name;
            this.ambiguityGroup = ambiguityGroup;
            this.ambiguitySide = ambiguitySide;
            this.observations = observations;
            this.maxLocalisationPoints = maxLocalisationPoints;
            this.perpendicularDistanceMetres = perpendicularDistanceMetres;
            this.heightMetres = heightMetres;
            this.maxRangeMetres = maxRangeMetres;
            this.minHeightMetres = minHeightMetres;
            this.maxHeightMetres = maxHeightMetres;
        }
    }

    private static final class OracleResult {
        boolean success;
        String status;
        int referenceIndex = -1;
        double x = Double.NaN;
        double y = Double.NaN;
        double z = Double.NaN;
        double rawChi2 = Double.NaN;
        double reducedChi2 = Double.NaN;
        double aic = Double.NaN;
        double perpendicularError = Double.NaN;
        double parallelError = Double.NaN;
        double errorAngleRadians = Double.NaN;
    }

    private TrackedTargetMotionFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println(
                    "Usage: TrackedTargetMotionFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.printf(
                    Locale.ROOT,
                    "# oracleVersion=%s,oracleCommit=%s%n",
                    ORACLE_VERSION,
                    ORACLE_COMMIT);
            writer.println(
                    "case,ambiguityGroup,ambiguitySide,observationIndex,"
                    + "selected,maxLocalisationPoints,originXMetres,"
                    + "originYMetres,originZMetres,headingRadians,"
                    + "bearingRadians,javaSuccess,javaStatus,"
                    + "referenceObservationIndex,resultXMetres,"
                    + "resultYMetres,resultZMetres,rawChi2,reducedChi2,aic,"
                    + "perpendicularErrorMetres,parallelErrorMetres,"
                    + "errorAngleRadians,perpendicularDistanceMetres,"
                    + "heightMetres,maxRangeMetres,minHeightMetres,"
                    + "maxHeightMetres,passesRunawayGuard,"
                    + "passesConfiguredLimits,acceptedByJavaFilters");

            for (FixtureCase fixtureCase : caseCatalogue()) {
                int[] selected = selectedIndices(
                        fixtureCase.observations.length,
                        fixtureCase.maxLocalisationPoints);
                OracleResult result = runOracle(fixtureCase, selected);
                boolean passesRunaway =
                        !(fixtureCase.perpendicularDistanceMetres
                                > 2.0 * Math.PI * EARTH_RADIUS_METRES);
                boolean passesConfigured =
                        fixtureCase.perpendicularDistanceMetres
                                        <= fixtureCase.maxRangeMetres
                                && fixtureCase.heightMetres
                                        >= fixtureCase.minHeightMetres
                                && fixtureCase.heightMetres
                                        <= fixtureCase.maxHeightMetres;
                boolean accepted =
                        result.success && passesRunaway && passesConfigured;

                for (int i = 0; i < fixtureCase.observations.length; ++i) {
                    Observation observation = fixtureCase.observations[i];
                    PamQuaternion rotation = new PamQuaternion(
                            Math.toRadians(observation.headingDegrees),
                            0.0,
                            0.0);
                    PamVector bearing = PamVector.fromHeadAndSlantR(
                            Math.toRadians(observation.bearingDegrees),
                            0.0);
                    writer.printf(
                            Locale.ROOT,
                            "%s,%s,%d,%d,%s,%d,"
                            + "%.17g,%.17g,%.17g,%.17g,%.17g,"
                            + "%s,%s,%d,"
                            + "%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,"
                            + "%.17g,%.17g,%.17g,"
                            + "%.17g,%.17g,%.17g,%.17g,%.17g,"
                            + "%s,%s,%s%n",
                            fixtureCase.name,
                            fixtureCase.ambiguityGroup,
                            fixtureCase.ambiguitySide,
                            i,
                            contains(selected, i),
                            fixtureCase.maxLocalisationPoints,
                            observation.x,
                            observation.y,
                            observation.z,
                            rotation.toHeading(),
                            PamVector.vectorToSurfaceBearing(bearing),
                            result.success,
                            result.status,
                            result.referenceIndex,
                            result.x,
                            result.y,
                            result.z,
                            result.rawChi2,
                            result.reducedChi2,
                            result.aic,
                            result.perpendicularError,
                            result.parallelError,
                            result.errorAngleRadians,
                            fixtureCase.perpendicularDistanceMetres,
                            fixtureCase.heightMetres,
                            fixtureCase.maxRangeMetres,
                            fixtureCase.minHeightMetres,
                            fixtureCase.maxHeightMetres,
                            passesRunaway,
                            passesConfigured,
                            accepted);
                }
            }
        }
    }

    private static OracleResult runOracle(
            FixtureCase fixtureCase,
            int[] selectedIndices) {
        OracleResult result = new OracleResult();
        int count = selectedIndices.length;
        PamVector[] bearings = new PamVector[count];
        PamVector[] angleErrors = new PamVector[count];
        PamVector[] origins = new PamVector[count];
        PamQuaternion[] rotations = new PamQuaternion[count];

        for (int i = 0; i < count; ++i) {
            Observation observation =
                    fixtureCase.observations[selectedIndices[i]];
            origins[i] =
                    new PamVector(observation.x, observation.y, observation.z);
            rotations[i] = new PamQuaternion(
                    Math.toRadians(observation.headingDegrees),
                    0.0,
                    0.0);
            bearings[i] = PamVector.fromHeadAndSlantR(
                    Math.toRadians(observation.bearingDegrees),
                    0.0);
            // Chi2Bearings requires the vector, although PAMGuard LeastSquares
            // deliberately ignores it and uses its fixed 3-degree model.
            angleErrors[i] =
                    new PamVector(Math.toRadians(1.0), 0.0, 0.0);
        }

        result.referenceIndex =
                referenceObservationIndex(bearings, rotations, selectedIndices);
        try {
            Chi2Bearings chi2Bearings = new Chi2Bearings(
                    bearings,
                    angleErrors,
                    origins,
                    rotations,
                    2);
            LeastSquares leastSquares = new LeastSquares();
            leastSquares.setMinimisationFunction(chi2Bearings);
            result.success = leastSquares.runAlgorithm();
            result.status = result.success ? "success" : "java-false";
            if (!result.success) {
                return result;
            }

            double[] position = leastSquares.getResult()[0];
            result.x = position[0];
            result.y = position[1];
            result.z = position[2];
            result.rawChi2 = leastSquares.getChi2()[0];
            result.reducedChi2 = result.rawChi2;
            if (count > 2) {
                result.reducedChi2 /= (count - 2);
            }
            result.aic = result.rawChi2 + 4.0;

            SimpleError error =
                    (SimpleError) leastSquares.getErrors()[0];
            result.perpendicularError = error.getPerpError();
            result.parallelError = error.getParallelError();
            result.errorAngleRadians = error.getPerpAngle();
            return result;
        }
        catch (RuntimeException exception) {
            result.success = false;
            result.status =
                    "java-exception-" + exception.getClass().getSimpleName();
            return result;
        }
    }

    /**
     * Literal equivalent of TMGroupLocInfo.copySubDetections. Java float
     * arithmetic and Math.round(float) are intentional.
     */
    private static int[] selectedIndices(int totalUnits, int maxPoints) {
        int keptUnits = totalUnits;
        if (maxPoints == 0 || maxPoints < totalUnits) {
            keptUnits = maxPoints;
        }
        if (keptUnits <= 0) {
            return new int[0];
        }
        int[] selected = new int[keptUnits];
        float keepRatio =
                (float) (totalUnits - 1) / (float) (keptUnits - 1);
        for (int i = 0; i < keptUnits; ++i) {
            selected[i] = Math.round(i * keepRatio);
        }
        return selected;
    }

    private static int referenceObservationIndex(
            PamVector[] bearings,
            PamQuaternion[] rotations,
            int[] originalIndices) {
        double bestAngle = 9999999.0;
        int best = -1;
        for (int i = 0; i < bearings.length; ++i) {
            double angle = Math.abs(constrainedAngle(
                    PamVector.vectorToSurfaceBearing(bearings[i])
                            - rotations[i].toHeading()));
            if (Math.abs(angle - Math.PI / 2.0)
                    < Math.abs(bestAngle - Math.PI / 2.0)) {
                best = originalIndices[i];
                bestAngle = angle;
            }
        }
        return best;
    }

    private static double constrainedAngle(double angle) {
        while (angle > Math.PI) {
            angle -= 2.0 * Math.PI;
        }
        while (angle <= -Math.PI) {
            angle += 2.0 * Math.PI;
        }
        return angle;
    }

    private static boolean contains(int[] values, int target) {
        for (int value : values) {
            if (value == target) {
                return true;
            }
        }
        return false;
    }

    private static Observation[] targetBearings(
            double[][] origins,
            double[] headingsDegrees,
            double targetX,
            double targetY,
            double[] perturbationsDegrees,
            boolean mirrorAboutHeading) {
        Observation[] observations = new Observation[origins.length];
        for (int i = 0; i < origins.length; ++i) {
            double bearing = Math.toDegrees(Math.atan2(
                    targetX - origins[i][0],
                    targetY - origins[i][1]));
            if (perturbationsDegrees != null) {
                bearing += perturbationsDegrees[i];
            }
            if (mirrorAboutHeading) {
                bearing = 2.0 * headingsDegrees[i] - bearing;
            }
            observations[i] = new Observation(
                    origins[i][0],
                    origins[i][1],
                    origins[i][2],
                    headingsDegrees[i],
                    bearing);
        }
        return observations;
    }

    private static FixtureCase[] caseCatalogue() {
        double[][] straightOrigins = {
                {0.0, -240.0, -8.0},
                {0.0, -120.0, -8.5},
                {0.0, 0.0, -9.0},
                {0.0, 120.0, -9.5},
                {0.0, 240.0, -10.0}
        };
        double[] straightHeadings = {0.0, 0.0, 0.0, 0.0, 0.0};
        Observation[] straightStarboard = targetBearings(
                straightOrigins,
                straightHeadings,
                750.0,
                60.0,
                null,
                false);
        Observation[] straightPort = targetBearings(
                straightOrigins,
                straightHeadings,
                750.0,
                60.0,
                null,
                true);

        double[][] curvedOrigins = {
                {-80.0, -360.0, -12.0},
                {-50.0, -240.0, -12.3},
                {-12.0, -115.0, -12.6},
                {35.0, 18.0, -12.9},
                {93.0, 160.0, -13.2},
                {164.0, 311.0, -13.5},
                {248.0, 472.0, -13.8}
        };
        double[] curvedHeadings =
                {12.0, 14.0, 16.0, 18.5, 21.0, 24.0, 27.0};
        double[] noisy =
                {0.15, -0.08, 0.05, -0.11, 0.09, -0.04, 0.12};
        Observation[] curvedNoisy = targetBearings(
                curvedOrigins,
                curvedHeadings,
                1100.0,
                360.0,
                noisy,
                false);
        Observation[] curvedMirror = targetBearings(
                curvedOrigins,
                curvedHeadings,
                1100.0,
                360.0,
                noisy,
                true);

        Observation[] parallel = {
                new Observation(0.0, -100.0, -5.0, 0.0, 90.0),
                new Observation(0.0, 0.0, -5.0, 0.0, 90.0),
                new Observation(0.0, 100.0, -5.0, 0.0, 90.0),
                new Observation(0.0, 200.0, -5.0, 0.0, 90.0)
        };
        Observation[] divergent = {
                new Observation(0.0, -150.0, -6.0, 0.0, 82.0),
                new Observation(0.0, -50.0, -6.0, 0.0, 72.0),
                new Observation(0.0, 50.0, -6.0, 0.0, 62.0),
                new Observation(0.0, 150.0, -6.0, 0.0, 52.0)
        };
        Observation[] singleton = {
                new Observation(10.0, 20.0, -4.0, 32.0, 81.0)
        };

        List<FixtureCase> cases = new ArrayList<>();
        cases.add(new FixtureCase(
                "straight-starboard",
                "straight-paired-array",
                0,
                straightStarboard,
                Integer.MAX_VALUE,
                750.0,
                0.0,
                20_000.0,
                -5_000.0,
                5.0));
        cases.add(new FixtureCase(
                "straight-port",
                "straight-paired-array",
                1,
                straightPort,
                Integer.MAX_VALUE,
                750.0,
                0.0,
                20_000.0,
                -5_000.0,
                5.0));
        cases.add(new FixtureCase(
                "curved-noisy-all",
                "curved-paired-array",
                0,
                curvedNoisy,
                Integer.MAX_VALUE,
                935.0,
                -12.0,
                20_000.0,
                -5_000.0,
                5.0));
        cases.add(new FixtureCase(
                "curved-noisy-mirror",
                "curved-paired-array",
                1,
                curvedMirror,
                Integer.MAX_VALUE,
                910.0,
                -12.0,
                20_000.0,
                -5_000.0,
                5.0));
        cases.add(new FixtureCase(
                "curved-noisy-limited-four",
                "curved-paired-array",
                0,
                curvedNoisy,
                4,
                25_001.0,
                0.0,
                20_000.0,
                -5_000.0,
                5.0));
        cases.add(new FixtureCase(
                "height-filter-rejection",
                "straight-paired-array",
                0,
                straightStarboard,
                Integer.MAX_VALUE,
                750.0,
                6.0,
                20_000.0,
                -5_000.0,
                5.0));
        cases.add(new FixtureCase(
                "filter-boundaries-inclusive",
                "straight-paired-array",
                0,
                straightStarboard,
                Integer.MAX_VALUE,
                20_000.0,
                -5_000.0,
                20_000.0,
                -5_000.0,
                5.0));
        cases.add(new FixtureCase(
                "runaway-filter-rejection",
                "straight-paired-array",
                0,
                straightStarboard,
                Integer.MAX_VALUE,
                2.0 * Math.PI * EARTH_RADIUS_METRES + 1.0,
                0.0,
                100_000_000.0,
                -5_000.0,
                5.0));
        cases.add(new FixtureCase(
                "parallel-nonfinite-java-success",
                "failure",
                0,
                parallel,
                Integer.MAX_VALUE,
                Double.POSITIVE_INFINITY,
                Double.NaN,
                20_000.0,
                -5_000.0,
                5.0));
        cases.add(new FixtureCase(
                "divergent-bearing-failure",
                "failure",
                0,
                divergent,
                Integer.MAX_VALUE,
                0.0,
                0.0,
                20_000.0,
                -5_000.0,
                5.0));
        cases.add(new FixtureCase(
                "single-bearing-failure",
                "failure",
                0,
                singleton,
                Integer.MAX_VALUE,
                0.0,
                0.0,
                20_000.0,
                -5_000.0,
                5.0));
        cases.add(new FixtureCase(
                "one-point-limit-failure",
                "failure",
                0,
                straightStarboard,
                1,
                0.0,
                0.0,
                20_000.0,
                -5_000.0,
                5.0));
        cases.add(new FixtureCase(
                "zero-point-limit-failure",
                "failure",
                0,
                straightStarboard,
                0,
                0.0,
                0.0,
                20_000.0,
                -5_000.0,
                5.0));
        return cases.toArray(new FixtureCase[0]);
    }
}
