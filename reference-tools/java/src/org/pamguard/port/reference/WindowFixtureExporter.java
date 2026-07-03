package org.pamguard.port.reference;

import Spectrogram.WindowFunction;

/**
 * Exports PAMGuard WindowFunction values for C++ parity tests.
 *
 * Expected classpath includes the compiled PAMGuard classes.
 */
public final class WindowFixtureExporter {

    private WindowFixtureExporter() {
    }

    public static void main(String[] args) {
        if (args.length < 2) {
            System.err.println("Usage: WindowFixtureExporter <windowType> <length>");
            System.err.println("windowType: 0 Rectangular, 1 Hamming, 2 Hann, 3 Bartlett, 4 Blackman, 5 Blackman-Harris");
            System.exit(2);
        }

        int windowType = Integer.parseInt(args[0]);
        int length = Integer.parseInt(args[1]);

        double[] window = WindowFunction.getWindowFunc(windowType, length);
        double gain = WindowFunction.getWindowGain(window);

        System.out.println("index,value");
        for (int i = 0; i < window.length; i++) {
            System.out.printf("%d,%.17g%n", i, window[i]);
        }
        System.out.printf("#gain,%.17g%n", gain);
    }
}

