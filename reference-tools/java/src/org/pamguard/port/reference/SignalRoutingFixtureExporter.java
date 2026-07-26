package org.pamguard.port.reference;

import amplifier.AmpParameters;
import patchPanel.PatchPanelParameters;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Exports amplifier.AmpProcess and patchPanel.PatchPanelProcess equations from
 * PAMGuard 2.02.18e parameter objects. The Patch Panel cases include the
 * checkbox-only canonical routes and one direct gain-matrix case supported by
 * PatchPanelProcess but not exposed by PatchPanelDialog.
 */
public final class SignalRoutingFixtureExporter {

    private static final int CHANNELS = 4;
    private static final int FRAMES = 9;
    private static final int INPUT_BITMAP = (1 << CHANNELS) - 1;

    private SignalRoutingFixtureExporter() {
    }

    private static final class OutputCase {
        final String name;
        final String module;
        final int outputBitmap;
        final double[][] output;

        OutputCase(
                String name,
                String module,
                int outputBitmap,
                double[][] output) {
            this.name = name;
            this.module = module;
            this.outputBitmap = outputBitmap;
            this.output = output;
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println(
                    "Usage: SignalRoutingFixtureExporter <output.csv>");
            System.exit(2);
        }
        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println(
                    "case,module,inputBitmap,outputBitmap,channel,frame,value");
            for (OutputCase fixtureCase : fixtureCases()) {
                for (int channel = 0; channel < 32; channel++) {
                    if ((fixtureCase.outputBitmap & (1 << channel)) == 0) {
                        continue;
                    }
                    for (int frame = 0; frame < FRAMES; frame++) {
                        writer.printf(
                                Locale.ROOT,
                                "%s,%s,%d,%d,%d,%d,%.17g%n",
                                fixtureCase.name,
                                fixtureCase.module,
                                INPUT_BITMAP,
                                fixtureCase.outputBitmap,
                                channel,
                                frame,
                                fixtureCase.output[channel][frame]);
                    }
                }
            }
        }
    }

    private static List<OutputCase> fixtureCases() throws Exception {
        List<OutputCase> cases = new ArrayList<>();

        AmpParameters defaultAmplifier = new AmpParameters();
        requireAmplifierDefaults(defaultAmplifier);
        cases.add(new OutputCase(
                "amplifier-default",
                "amplifier",
                INPUT_BITMAP,
                amplify(defaultAmplifier)));

        AmpParameters configuredAmplifier = new AmpParameters();
        configuredAmplifier.gain[0] = Math.pow(10.0, 6.0 / 20.0);
        configuredAmplifier.gain[1] = -Math.pow(10.0, -3.0 / 20.0);
        configuredAmplifier.gain[2] = Math.pow(10.0, 12.5 / 20.0);
        configuredAmplifier.gain[3] = -1.0;
        cases.add(new OutputCase(
                "amplifier-db-invert",
                "amplifier",
                INPUT_BITMAP,
                amplify(configuredAmplifier)));

        PatchPanelParameters identity = new PatchPanelParameters();
        double[][] identityMatrix = patchMatrix(identity);
        requirePatchDefaults(identityMatrix);
        identity.configureSummary(INPUT_BITMAP);
        cases.add(new OutputCase(
                "patch-identity",
                "patch-panel",
                identity.getOutputChannels(),
                patch(identityMatrix)));

        PatchPanelParameters routed = new PatchPanelParameters();
        double[][] routeMatrix = patchMatrix(routed);
        clear(routeMatrix);
        routeMatrix[0][2] = 1.0;
        routeMatrix[1][0] = 1.0;
        routeMatrix[1][2] = 1.0;
        routeMatrix[2][2] = 1.0;
        routeMatrix[3][1] = 1.0;
        routeMatrix[3][7] = 1.0;
        routed.configureSummary(INPUT_BITMAP);
        cases.add(new OutputCase(
                "patch-route-mix-duplicate",
                "patch-panel",
                routed.getOutputChannels(),
                patch(routeMatrix)));

        PatchPanelParameters advanced = new PatchPanelParameters();
        double[][] advancedMatrix = patchMatrix(advanced);
        clear(advancedMatrix);
        advancedMatrix[0][0] = 0.5;
        advancedMatrix[1][0] = -0.25;
        advancedMatrix[2][3] = 2.0;
        advancedMatrix[3][3] = 0.125;
        advanced.configureSummary(INPUT_BITMAP);
        cases.add(new OutputCase(
                "patch-advanced-gains",
                "patch-panel",
                advanced.getOutputChannels(),
                patch(advancedMatrix)));

        return cases;
    }

    private static void requireAmplifierDefaults(AmpParameters parameters) {
        if (parameters.gain.length != 32) {
            throw new IllegalStateException(
                    "AmpParameters no longer has 32 channels");
        }
        for (double gain : parameters.gain) {
            if (gain != 1.0) {
                throw new IllegalStateException(
                        "AmpParameters default gain is no longer unity");
            }
        }
    }

    private static double[][] patchMatrix(
            PatchPanelParameters parameters) throws Exception {
        Field field =
                PatchPanelParameters.class.getDeclaredField("patches");
        field.setAccessible(true);
        return (double[][]) field.get(parameters);
    }

    private static void requirePatchDefaults(double[][] matrix) {
        if (matrix.length != 32) {
            throw new IllegalStateException(
                    "PatchPanelParameters no longer has 32 input rows");
        }
        for (int input = 0; input < 32; input++) {
            if (matrix[input].length != 32) {
                throw new IllegalStateException(
                        "PatchPanelParameters no longer has 32 outputs");
            }
            for (int output = 0; output < 32; output++) {
                double expected = input == output ? 1.0 : 0.0;
                if (matrix[input][output] != expected) {
                    throw new IllegalStateException(
                            "PatchPanelParameters default is no longer identity");
                }
            }
        }
    }

    private static void clear(double[][] matrix) {
        for (double[] row : matrix) {
            java.util.Arrays.fill(row, 0.0);
        }
    }

    private static double input(int channel, int frame) {
        double stepped =
                (double) (((frame + channel) % 3) - 1) * 0.05;
        return (frame + 1) * 0.125 + channel * 0.3 + stepped;
    }

    private static double[][] amplify(AmpParameters parameters) {
        double[][] output = new double[32][FRAMES];
        for (int channel = 0; channel < CHANNELS; channel++) {
            for (int frame = 0; frame < FRAMES; frame++) {
                // amplifier.AmpProcess#newData
                output[channel][frame] =
                        input(channel, frame) * parameters.gain[channel];
            }
        }
        return output;
    }

    private static double[][] patch(double[][] matrix) {
        double[][] output = new double[32][FRAMES];
        for (int inputChannel = 0;
             inputChannel < CHANNELS;
             inputChannel++) {
            for (int outputChannel = 0;
                 outputChannel < 32;
                 outputChannel++) {
                double gain = matrix[inputChannel][outputChannel];
                if (gain == 0.0) {
                    continue;
                }
                for (int frame = 0; frame < FRAMES; frame++) {
                    // patchPanel.PatchPanelProcess#addChannelData
                    output[outputChannel][frame] +=
                            input(inputChannel, frame) * gain;
                }
            }
        }
        return output;
    }
}
