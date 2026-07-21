package org.pamguard.port.reference;

import Jama.Matrix;
import pamMaths.PamVector;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Method;
import java.util.Locale;

/**
 * Exports AbstractLocalisation.getWorldVectors fixtures.
 *
 * That class hangs off a PamDataUnit and reaches GPS data, so
 * getPlanarVector/getCoordinateMatrix/linearCoordinateMatrix/getWorldVectors
 * are transcribed here over explicit array axes and angles, while the vector
 * maths (fromHeadAndSlantR, vecProd) is the real pamMaths.PamVector and the
 * matrix inverse and multiply are the real Jama.Matrix.
 *
 * Array axes come from ArrayShapeFixtureExporter's transcription of
 * ArrayManager.getArrayDirections, reached by reflection so the two exporters
 * cannot drift apart.
 *
 * Case names, positions, and angles are shared with the C++ fixture check
 * (cpp-engine/tools/world_vector_fixture_check.cpp).
 */
public final class WorldVectorFixtureExporter {

    private static final int ARRAY_TYPE_LINE = 2;
    private static final int ARRAY_TYPE_PLANE = 3;
    private static final int ARRAY_TYPE_VOLUME = 4;

    private static final double[][] LINEAR_ARRAY_GEOMETRY = {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}};

    private static final class VectorCase {
        String name;
        double[][] positions;
        double[] angles;
        /** When true the axes are dropped, exercising the fixed-geometry fallback. */
        boolean dropAxes;

        VectorCase(String name, double[][] positions, double[] angles) {
            this(name, positions, angles, false);
        }

        VectorCase(String name, double[][] positions, double[] angles, boolean dropAxes) {
            this.name = name;
            this.positions = positions;
            this.angles = angles;
            this.dropAxes = dropAxes;
        }
    }

    private WorldVectorFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: WorldVectorFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,arrayType,nAxes,nVectors,cone,v0x,v0y,v0z,v1x,v1y,v1z");
            for (VectorCase vectorCase : caseCatalogue()) {
                PamVector[] positions = new PamVector[vectorCase.positions.length];
                for (int i = 0; i < positions.length; i++) {
                    positions[i] = new PamVector(vectorCase.positions[i]);
                }
                int arrayType = arrayShape(positions);
                PamVector[] axes = vectorCase.dropAxes ? new PamVector[0] : arrayDirections(positions);

                PamVector[] vectors = getWorldVectors(arrayType, axes, vectorCase.angles);
                boolean cone = vectors.length > 0 && vectors[0].isCone();

                double[] flat = new double[6];
                for (int i = 0; i < vectors.length && i < 2; i++) {
                    for (int e = 0; e < 3; e++) {
                        flat[i * 3 + e] = vectors[i].getElement(e);
                    }
                }
                writer.printf(Locale.ROOT, "%s,%d,%d,%d,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g%n",
                        vectorCase.name, arrayType, axes.length, vectors.length, cone ? 1 : 0,
                        flat[0], flat[1], flat[2], flat[3], flat[4], flat[5]);
            }
        }
    }

    /** AbstractLocalisation.getPlanarVector. */
    private static PamVector getPlanarVector(double[] angles) {
        PamVector planeVec;
        if (angles.length >= 2) {
            planeVec = PamVector.fromHeadAndSlantR(Math.PI / 2. - angles[0], angles[1]);
        }
        else {
            planeVec = PamVector.fromHeadAndSlantR(Math.PI / 2. - angles[0], 0);
            planeVec.setCone(true);
        }
        return planeVec;
    }

    /** AbstractLocalisation.getCoordinateMatrix. */
    private static Matrix getCoordinateMatrix(PamVector[] arrayAxis, boolean flipZ) {
        if (arrayAxis == null || arrayAxis.length < 1) {
            return new Matrix(LINEAR_ARRAY_GEOMETRY);
        }
        if (arrayAxis.length == 1) {
            return linearCoordinateMatrix(arrayAxis, flipZ);
        }
        double[][] m = new double[3][];
        m[0] = arrayAxis[0].getVector();
        m[1] = arrayAxis[1].getVector();
        if (flipZ) {
            m[2] = arrayAxis[1].vecProd(arrayAxis[0]).getVector();
        }
        else {
            m[2] = arrayAxis[0].vecProd(arrayAxis[1]).getVector();
        }
        return new Matrix(m);
    }

    /** AbstractLocalisation.linearCoordinateMatrix. */
    private static Matrix linearCoordinateMatrix(PamVector[] arrayAxis, boolean flipZ) {
        double[][] m = new double[3][];
        m[0] = arrayAxis[0].getVector();
        PamVector m1;
        if (m[0][0] == 0 && m[0][1] == 0) {
            m1 = PamVector.getXAxis();
        }
        else {
            m1 = PamVector.getZAxis().vecProd(arrayAxis[0]);
        }
        m[1] = m1.getVector();
        if (flipZ) {
            m[2] = m1.vecProd(arrayAxis[0]).getVector();
        }
        else {
            m[2] = arrayAxis[0].vecProd(m1).getVector();
        }
        return new Matrix(m);
    }

    /** AbstractLocalisation.getWorldVectors. */
    private static PamVector[] getWorldVectors(int arrayType, PamVector[] arrayAxis, double[] angles) {
        PamVector singleVec = getPlanarVector(angles);
        Matrix pointer = new Matrix(3, 1);
        for (int i = 0; i < 3; i++) {
            pointer.set(i, 0, singleVec.getElement(i));
        }
        Matrix invCoordMatrix;
        Matrix rotatedPointer;
        if (arrayType == ARRAY_TYPE_VOLUME) {
            PamVector[] vecs = new PamVector[1];
            invCoordMatrix = getCoordinateMatrix(arrayAxis, false).inverse();
            rotatedPointer = invCoordMatrix.times(pointer);
            vecs[0] = new PamVector(rotatedPointer.getColumnPackedCopy());
            return vecs;
        }
        if (arrayType == ARRAY_TYPE_PLANE) {
            PamVector[] vecs = new PamVector[2];
            invCoordMatrix = getCoordinateMatrix(arrayAxis, false).inverse();
            rotatedPointer = invCoordMatrix.times(pointer);
            vecs[0] = new PamVector(rotatedPointer.getColumnPackedCopy());
            invCoordMatrix = getCoordinateMatrix(arrayAxis, true).inverse();
            rotatedPointer = invCoordMatrix.times(pointer);
            vecs[1] = new PamVector(rotatedPointer.getColumnPackedCopy());
            return vecs;
        }
        PamVector[] vecs = new PamVector[2];
        invCoordMatrix = getCoordinateMatrix(arrayAxis, false).inverse();
        rotatedPointer = invCoordMatrix.times(pointer);
        vecs[0] = new PamVector(rotatedPointer.getColumnPackedCopy());
        vecs[0].setCone(true);
        pointer.set(1, 0, -pointer.get(1, 0));
        rotatedPointer = invCoordMatrix.times(pointer);
        vecs[1] = new PamVector(rotatedPointer.getColumnPackedCopy());
        vecs[1].setCone(true);
        return vecs;
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

    private static VectorCase[] caseCatalogue() {
        double[][] plane = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {2.0, 3.0, 0.0}};
        double[][] volume = {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {0.5, 1.0, 2.5}};
        double[][] lineY = {{0.0, 0.0, 0.0}, {0.0, 2.0, 0.0}, {0.0, 4.0, 0.0}};
        double[][] lineVertical = {{0.0, 0.0, 0.0}, {0.0, 0.0, 2.0}, {0.0, 0.0, 4.0}};
        double[][] lineDiagonal = {{0.0, 0.0, 0.0}, {1.0, 1.0, 0.5}, {2.0, 2.0, 1.0}};

        return new VectorCase[]{
                new VectorCase("plane-zero-angles", plane, new double[]{0.0, 0.0}),
                new VectorCase("plane-quarter-turn", plane, new double[]{Math.PI / 2., 0.0}),
                new VectorCase("plane-elevated", plane, new double[]{0.6, 0.4}),
                new VectorCase("plane-negative-elevation", plane, new double[]{-1.2, -0.35}),
                new VectorCase("volume-zero-angles", volume, new double[]{0.0, 0.0}),
                new VectorCase("volume-oblique", volume, new double[]{2.1, -0.6}),
                new VectorCase("line-y-axis", lineY, new double[]{0.7}),
                new VectorCase("line-y-axis-broadside", lineY, new double[]{Math.PI / 2.}),
                new VectorCase("line-vertical", lineVertical, new double[]{0.9}),
                new VectorCase("line-diagonal", lineDiagonal, new double[]{1.4}),
                // No axes at all: the reference falls back to a fixed frame.
                new VectorCase("line-no-axes-fallback", lineY, new double[]{0.7}, true),
                new VectorCase("plane-no-axes-fallback", plane, new double[]{0.6, 0.4}, true),
        };
    }
}
