package org.pamguard.port.reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

public final class ConnectedRegionDiscardFixtureExporter {
    private ConnectedRegionDiscardFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: ConnectedRegionDiscardFixtureExporter <output.csv>");
            System.exit(2);
        }
        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        int maxPeaks = 2;
        int fragments = maxPeaks == 1 ? 1 : 0;

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("fragments");
            writer.println(fragments);
        }
    }
}
