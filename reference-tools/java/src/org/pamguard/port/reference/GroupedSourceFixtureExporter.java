package org.pamguard.port.reference;

import whistlesAndMoans.WhistleToneParameters;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;

/** Exports real GroupedSourceParameters group ordering and bitmaps. */
public final class GroupedSourceFixtureExporter {
    private GroupedSourceFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        Locale.setDefault(Locale.ROOT);
        if (args.length != 1) {
            System.err.println("Usage: GroupedSourceFixtureExporter <output.csv>");
            System.exit(2);
        }
        File output = new File(args[0]);
        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("case,channelBitmap,channelGroups,groupIndex,groupBitmap");
            writeCase(writer, "two-pairs", 15, new int[]{0, 0, 1, 1});
            writeCase(writer, "selected-only", 10, new int[]{5, 3, 5, 3});
            writeCase(writer, "sparse-group-ids", 15, new int[]{4, 4, 1, 7});
            writeCase(writer, "sparse-channels", 21, new int[]{2, 9, 2, 9, 5});
        }
    }

    private static void writeCase(PrintWriter writer, String name,
            int channelBitmap, int[] groups) {
        WhistleToneParameters params = new WhistleToneParameters();
        params.setChanOrSeqBitmap(channelBitmap);
        params.setChannelGroups(groups);
        StringBuilder groupList = new StringBuilder();
        for (int i = 0; i < groups.length; i++) {
            if (i > 0) {
                groupList.append(';');
            }
            groupList.append(groups[i]);
        }
        for (int group = 0; group < params.countChannelGroups(); group++) {
            writer.printf(Locale.ROOT, "%s,%d,%s,%d,%d%n",
                    name, channelBitmap, groupList, group,
                    params.getGroupChannels(group));
        }
    }
}
