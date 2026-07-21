package org.pamguard.port.reference;

import pamMaths.PamQuaternion;
import pamMaths.PamVector;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

/**
 * Exports streamer orientation fixtures by driving the real PAMGuard
 * pamMaths.PamQuaternion and PamVector.rotateVector, which
 * HydrophoneLocator.getPhoneLatLong uses to orient hydrophone coordinates by
 * a streamer's heading, pitch, and roll.
 *
 * Case angles (degrees) and vectors are shared by name with the C++ fixture
 * check (cpp-engine/tools/streamer_orientation_fixture_check.cpp).
 */
public final class StreamerOrientationFixtureExporter {

    private static final class OrientationCase {
        String name;
        double headingDeg;
        double pitchDeg;
        double rollDeg;
        double[] vector;

        OrientationCase(String name, double headingDeg, double pitchDeg, double rollDeg, double[] vector) {
            this.name = name;
            this.headingDeg = headingDeg;
            this.pitchDeg = pitchDeg;
            this.rollDeg = rollDeg;
            this.vector = vector;
        }
    }

    private StreamerOrientationFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: StreamerOrientationFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,headingDeg,pitchDeg,rollDeg,vx,vy,vz,rx,ry,rz");
            for (OrientationCase orientationCase : caseCatalogue()) {
                PamQuaternion quaternion = new PamQuaternion(
                        Math.toRadians(orientationCase.headingDeg),
                        Math.toRadians(orientationCase.pitchDeg),
                        Math.toRadians(orientationCase.rollDeg));
                PamVector rotated = PamVector.rotateVector(new PamVector(orientationCase.vector), quaternion);
                writer.printf(Locale.ROOT, "%s,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g%n",
                        orientationCase.name,
                        orientationCase.headingDeg, orientationCase.pitchDeg, orientationCase.rollDeg,
                        orientationCase.vector[0], orientationCase.vector[1], orientationCase.vector[2],
                        rotated.getElement(0), rotated.getElement(1), rotated.getElement(2));
            }
        }
    }

    private static OrientationCase[] caseCatalogue() {
        double[] alongY = {0.0, 10.0, 0.0};
        double[] alongX = {10.0, 0.0, 0.0};
        double[] skew = {1.5, -4.0, 2.5};
        return new OrientationCase[]{
                new OrientationCase("identity", 0.0, 0.0, 0.0, alongY),
                new OrientationCase("heading-90", 90.0, 0.0, 0.0, alongY),
                new OrientationCase("heading-180", 180.0, 0.0, 0.0, alongY),
                new OrientationCase("heading-45-x", 45.0, 0.0, 0.0, alongX),
                new OrientationCase("pitch-30", 0.0, 30.0, 0.0, alongY),
                new OrientationCase("roll-30", 0.0, 0.0, 30.0, alongX),
                new OrientationCase("combined", 35.0, -12.0, 7.0, skew),
                new OrientationCase("combined-negative-heading", -70.0, 15.0, -25.0, skew),
        };
    }
}
