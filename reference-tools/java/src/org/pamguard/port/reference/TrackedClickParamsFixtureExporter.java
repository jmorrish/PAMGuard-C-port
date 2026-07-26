package org.pamguard.port.reference;

import clickDetector.localisation.ClickLocParams;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.Locale;

/**
 * Exports the lazy selected-algorithm array and defaults from the pinned
 * PAMGuard ClickLocParams authority.
 */
public final class TrackedClickParamsFixtureExporter {

    private TrackedClickParamsFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println(
                    "Usage: TrackedClickParamsFixtureExporter <output.csv>");
            System.exit(2);
        }
        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        ClickLocParams params = new ClickLocParams();
        Field selectedField =
                ClickLocParams.class.getDeclaredField("isSelected");
        selectedField.setAccessible(true);
        if (selectedField.get(params) != null) {
            throw new IllegalStateException(
                    "ClickLocParams isSelected is no longer initially null");
        }

        // GeneralGroupLocaliser has four stable algorithm indexes across all
        // modes; getIsSelected grows the private array lazily and enables
        // index zero on first access.
        for (int index = 0; index < 4; index++) {
            params.getIsSelected(index);
        }
        boolean[] selected = (boolean[]) selectedField.get(params);
        if (selected.length != 4 ||
                !selected[0] ||
                selected[1] ||
                selected[2] ||
                selected[3]) {
            throw new IllegalStateException(
                    "ClickLocParams default selection ordering changed");
        }

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println(
                    "stage,isSelected,maxRange,maxHeight,minHeight," +
                    "maxTime,limitLocPoints,maxLocPoints," +
                    "maxLocalisationPoints");
            write(writer, "default", params, selected);
            params.setIsSelected(2, true);
            write(
                    writer,
                    "least-squares-and-3d-simplex",
                    params,
                    (boolean[]) selectedField.get(params));
            params.limitLocPoints = true;
            params.maxLocPoints = 17;
            write(
                    writer,
                    "limited",
                    params,
                    (boolean[]) selectedField.get(params));
        }
    }

    private static void write(
            PrintWriter writer,
            String stage,
            ClickLocParams params,
            boolean[] selected) {
        writer.printf(
                Locale.ROOT,
                "%s,%s,%.17g,%.17g,%.17g,%d,%s,%d,%d%n",
                stage,
                flags(selected),
                params.maxRange,
                params.maxHeight,
                params.minHeight,
                params.maxTime,
                Boolean.toString(params.limitLocPoints),
                params.maxLocPoints,
                params.getMaxLocalisationPoints());
    }

    private static String flags(boolean[] selected) {
        StringBuilder value = new StringBuilder();
        for (int index = 0; index < selected.length; index++) {
            if (index > 0) {
                value.append(';');
            }
            value.append(Boolean.toString(selected[index]));
        }
        return value.toString();
    }
}
