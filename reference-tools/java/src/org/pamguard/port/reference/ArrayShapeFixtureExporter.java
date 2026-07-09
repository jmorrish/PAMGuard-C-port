package org.pamguard.port.reference;

import pamMaths.PamVector;

import java.io.File;
import java.io.PrintWriter;
import java.util.Arrays;
import java.util.Locale;

/**
 * Exports array shape/direction fixtures for Array.ArrayManager.
 *
 * getArrayShape/getArrayDirections are instance methods on the
 * PamController-coupled ArrayManager singleton, so their logic is transcribed
 * here verbatim over explicit hydrophone positions (single streamer), while
 * every vector operation (angle, parallel/in-line tests, cross and triple
 * products, principal axis selection) is the real pamMaths.PamVector class.
 * Quirks preserved: the last-duplicate-wins unique filter, getMaxVolume
 * taking the max of signed triple products, and the plane-perpendicular loop
 * overwriting its result across outer iterations.
 *
 * Case positions are shared by name with the C++ fixture check
 * (cpp-engine/tools/array_shape_fixture_check.cpp).
 */
public final class ArrayShapeFixtureExporter {

    private static final int ARRAY_TYPE_NONE = 0;
    private static final int ARRAY_TYPE_POINT = 1;
    private static final int ARRAY_TYPE_LINE = 2;
    private static final int ARRAY_TYPE_PLANE = 3;
    private static final int ARRAY_TYPE_VOLUME = 4;

    private static final class ShapeCase {
        String name;
        double[][] positions;

        ShapeCase(String name, double[][] positions) {
            this.name = name;
            this.positions = positions;
        }
    }

    private ArrayShapeFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: ArrayShapeFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,shape,nVectors,v0x,v0y,v0z,v1x,v1y,v1z,v2x,v2y,v2z");
            for (ShapeCase shapeCase : caseCatalogue()) {
                PamVector[] positions = toVectors(shapeCase.positions);
                int shape = getArrayShape(positions);
                PamVector[] directions = getArrayDirections(positions);
                double[] flat = new double[9];
                int nVectors = directions == null ? 0 : directions.length;
                for (int i = 0; i < nVectors; i++) {
                    for (int e = 0; e < 3; e++) {
                        flat[i * 3 + e] = directions[i].getElement(e);
                    }
                }
                writer.printf(Locale.ROOT,
                        "%s,%d,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g%n",
                        shapeCase.name, shape, nVectors,
                        flat[0], flat[1], flat[2], flat[3], flat[4], flat[5], flat[6], flat[7], flat[8]);
            }
        }
    }

    private static ShapeCase[] caseCatalogue() {
        return new ShapeCase[]{
                new ShapeCase("point-duplicate-2ch", new double[][]{{1.0, 2.0, 3.0}, {1.0, 2.0, 3.0}}),
                new ShapeCase("two-ch-diagonal", new double[][]{{0.0, 0.0, 0.0}, {1.0, 2.0, 0.5}}),
                new ShapeCase("line-3ch-y", new double[][]{{0.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {0.0, 7.5, 0.0}}),
                new ShapeCase("line-4ch-negative-x", new double[][]{
                        {0.0, 0.0, 0.0}, {-2.0, 0.0, 0.0}, {-5.0, 0.0, 0.0}, {-9.0, 0.0, 0.0}}),
                new ShapeCase("plane-3ch-xy", new double[][]{{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 3.0, 0.0}}),
                new ShapeCase("plane-4ch-rect", new double[][]{
                        {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {2.0, 3.0, 0.0}}),
                new ShapeCase("plane-4ch-tilted", new double[][]{
                        {0.0, 0.0, 0.0}, {2.0, 0.0, 1.0}, {0.0, 3.0, 0.5}, {2.0, 3.0, 1.5}}),
                new ShapeCase("volume-4ch-tetrahedron", new double[][]{
                        {0.0, 0.0, 0.0}, {2.5, 0.0, 0.0}, {0.0, 2.5, 0.0}, {0.0, 0.0, 2.5}}),
                new ShapeCase("volume-5ch-towed-cluster", new double[][]{
                        {0.0, 0.0, 0.0}, {0.05, 3.0, 0.02}, {-0.03, 6.0, 0.04}, {0.02, 9.0, 0.5}, {0.5, 12.0, 0.1}}),
        };
    }

    private static PamVector[] toVectors(double[][] positions) {
        PamVector[] vectors = new PamVector[positions.length];
        for (int i = 0; i < positions.length; i++) {
            vectors[i] = new PamVector(positions[i]);
        }
        return vectors;
    }

    /** ArrayManager.getSpatiallyUniquePhones transcription (single streamer): last duplicate wins. */
    private static PamVector[] spatiallyUnique(PamVector[] positions) {
        boolean[] unique = new boolean[positions.length];
        int count = 0;
        for (int i = 0; i < positions.length; i++) {
            unique[i] = true;
            for (int j = i + 1; j < positions.length; j++) {
                if (positions[i].equals(positions[j])) {
                    unique[i] = false;
                }
            }
            if (unique[i]) {
                count++;
            }
        }
        PamVector[] result = new PamVector[count];
        int index = 0;
        for (int i = 0; i < positions.length; i++) {
            if (unique[i]) {
                result[index++] = positions[i];
            }
        }
        return result;
    }

    /** ArrayManager.getArrayShape transcription over explicit positions. */
    private static int getArrayShape(PamVector[] positions) {
        if (positions.length == 0) {
            return ARRAY_TYPE_NONE;
        }
        PamVector[] uniquePositions = spatiallyUnique(positions);
        int nPhones = uniquePositions.length;
        if (nPhones <= 1) {
            return ARRAY_TYPE_POINT;
        }

        PamVector[] phoneVectors = new PamVector[nPhones];
        int uniquePhones = 0;
        for (int i = 0; i < nPhones; i++) {
            PamVector phoneVec = uniquePositions[i];
            boolean matches = false;
            for (int j = 0; j < uniquePhones; j++) {
                if (phoneVec.equals(phoneVectors[j])) {
                    matches = true;
                    break;
                }
            }
            if (matches) {
                continue;
            }
            phoneVectors[uniquePhones++] = phoneVec;
        }
        nPhones = uniquePhones;
        phoneVectors = Arrays.copyOf(phoneVectors, nPhones);

        int nPairs = nPhones * (nPhones - 1) / 2;
        int iPair = 0;
        PamVector[] pairVectors = new PamVector[nPairs];
        for (int i = 0; i < nPhones; i++) {
            for (int j = i + 1; j < nPhones; j++) {
                pairVectors[iPair++] = phoneVectors[j].sub(phoneVectors[i]);
            }
        }
        if (nPhones == 2) {
            return ARRAY_TYPE_LINE;
        }
        if (areInLine(pairVectors)) {
            return ARRAY_TYPE_LINE;
        }
        if (nPhones == 3) {
            return ARRAY_TYPE_PLANE;
        }
        if (areOnPlane(pairVectors)) {
            return ARRAY_TYPE_PLANE;
        }
        return ARRAY_TYPE_VOLUME;
    }

    /** ArrayManager.getArrayDirections transcription over explicit positions. */
    private static PamVector[] getArrayDirections(PamVector[] positions) {
        PamVector[] uniquePositions = spatiallyUnique(positions);
        if (uniquePositions.length <= 0) {
            return null;
        }
        int arrayType = getArrayShape(positions);
        switch (arrayType) {
            case ARRAY_TYPE_POINT:
                return null;
            case ARRAY_TYPE_LINE:
                return getLineArrayVector(uniquePositions);
            case ARRAY_TYPE_PLANE:
                return getPlaneArrayVectors(uniquePositions);
            case ARRAY_TYPE_VOLUME:
                return getVolumeArrayVectors();
            default:
                return null;
        }
    }

    private static PamVector[] getLineArrayVector(PamVector[] arrayVectors) {
        PamVector[] vectors = new PamVector[1];
        vectors[0] = arrayVectors[1].sub(arrayVectors[0]);
        int ax = vectors[0].getPrincipleAxis();
        if (vectors[0].dotProd(PamVector.getCartesianAxes(ax)) < 0) {
            vectors[0] = vectors[0].times(-1);
        }
        vectors[0] = vectors[0].getUnitVector();
        return vectors;
    }

    private static PamVector[] getPlaneArrayVectors(PamVector[] arrayVectors) {
        PamVector[] vectors = new PamVector[2];
        PamVector[] vectorPairs = PamVector.getVectorPairs(arrayVectors);
        int nPairs = vectorPairs.length;
        int[] closestAxis = new int[nPairs];
        double[] closestAngle = new double[nPairs];
        for (int i = 0; i < nPairs; i++) {
            closestAxis[i] = vectorPairs[i].getPrincipleAxis();
            closestAngle[i] = vectorPairs[i].absAngle(PamVector.getCartesianAxes(closestAxis[i]));
        }
        PamVector planePerpendicular = null;
        for (int i = 0; i < nPairs; i++) {
            for (int j = (i + 1); j < nPairs; j++) {
                if (!vectorPairs[i].isParallel(vectorPairs[j])) {
                    planePerpendicular = vectorPairs[i].vecProd(vectorPairs[j]);
                    break;
                }
            }
            if (planePerpendicular == null) {
                break;
            }
        }
        if (planePerpendicular == null) {
            planePerpendicular = PamVector.getZAxis().clone();
        }

        int[] closestPair = new int[3];
        for (int ax = 0; ax < 3; ax++) {
            closestPair[ax] = -1;
            double closest = Double.MAX_VALUE;
            for (int i = 0; i < nPairs; i++) {
                if (closestAxis[i] != ax) {
                    continue;
                }
                if (closestAngle[i] < closest) {
                    closest = closestAngle[i];
                    closestPair[ax] = i;
                }
            }
        }
        int startPair;
        if (closestPair[1] >= 0) {
            startPair = closestPair[1];
        }
        else if (closestPair[0] >= 0) {
            startPair = closestPair[0];
        }
        else {
            startPair = closestPair[2];
        }
        if (startPair < 0) {
            return null;
        }
        vectors[0] = vectorPairs[startPair];
        if (vectors[0].angle(PamVector.getCartesianAxes(closestAxis[startPair])) > Math.PI / 2) {
            vectors[0] = vectors[0].times(-1);
        }
        vectors[1] = vectors[0].vecProd(planePerpendicular);
        int closestAx = vectors[1].getPrincipleAxis();
        if (vectors[1].angle(PamVector.getCartesianAxes(closestAx)) > Math.PI / 2) {
            vectors[1] = vectors[1].times(-1);
        }
        vectors[0] = vectors[0].getUnitVector();
        vectors[1] = vectors[1].getUnitVector();
        return vectors;
    }

    private static PamVector[] getVolumeArrayVectors() {
        PamVector[] vectors = new PamVector[3];
        for (int i = 0; i < 3; i++) {
            vectors[i] = PamVector.getCartesianAxes(i).clone();
        }
        return vectors;
    }

    private static boolean areInLine(PamVector[] pvs) {
        int nPairs = pvs.length;
        for (int i = 0; i < nPairs; i++) {
            for (int j = i + 1; j < nPairs; j++) {
                if (!pvs[i].isInLine(pvs[j])) {
                    return false;
                }
            }
        }
        return true;
    }

    private static boolean areOnPlane(PamVector[] pvs) {
        return getMaxVolume(pvs) == 0;
    }

    private static double getMaxVolume(PamVector[] pvs) {
        int nPairs = pvs.length;
        double maxVol = 0;
        for (int i = 0; i < nPairs; i++) {
            for (int j = i + 1; j < nPairs; j++) {
                for (int k = j + 1; k < nPairs; k++) {
                    double vol = pvs[i].tripleDotProduct(pvs[j], pvs[k]);
                    maxVol = Math.max(maxVol, vol);
                }
            }
        }
        return maxVol;
    }
}
