package org.pamguard.port.reference;

import PamController.PSFXReadWriter;
import PamController.PamControlledUnitSettings;
import PamController.PamSettingsGroup;

import java.io.File;
import java.io.FileInputStream;
import java.io.ObjectInputStream;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Inspects a PAMGuard project settings file (.psf or .psfx) and reports the
 * modules it contains along with the Java class of each module's settings
 * object.
 *
 * This is the first step of PAMGuard project import. Both formats store each
 * module's settings as a Java-serialised object graph (.psf is one
 * ObjectInputStream stream; .psfx wraps the same serialised byte arrays in
 * PAMGuard's binary-store framing). Java serialisation encodes class graphs,
 * serialVersionUIDs, and private field layouts, so it cannot be read
 * faithfully from C++ — the import path has to run on the JVM, using
 * PAMGuard's own classes, and emit engine JSON. This tool proves that route
 * and enumerates what a converter would have to map.
 *
 * Usage: PamguardSettingsInspector &lt;settings.psf|.psfx&gt; [output.csv]
 */
public final class PamguardSettingsInspector {

    private PamguardSettingsInspector() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 2) {
            System.err.println("Usage: PamguardSettingsInspector <settings.psf|.psfx> [output.csv]");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File settingsFile = new File(args[0]);
        if (!settingsFile.exists()) {
            System.err.println("Settings file not found: " + settingsFile.getAbsolutePath());
            System.exit(2);
        }

        List<PamControlledUnitSettings> settings;
        try {
            settings = read(settingsFile);
        }
        catch (java.io.InvalidClassException e) {
            // Java serialisation is version-brittle: a settings file written
            // by a different PAMGuard build fails to load when any settings
            // class has bumped its serialVersionUID. PAMGuard itself does not
            // support importing legacy .psf files for this reason.
            System.err.println("Settings file is not loadable by this PAMGuard build.");
            System.err.println("Serialisation incompatibility: " + e.getMessage());
            System.err.println("This is a property of Java object serialisation, not of the file being corrupt.");
            System.exit(3);
            return;
        }
        if (settings == null) {
            System.err.println("Could not read settings from " + settingsFile.getAbsolutePath());
            System.exit(1);
        }

        StringBuilder report = new StringBuilder();
        report.append("unitType,unitName,versionNo,settingsClass\n");
        for (PamControlledUnitSettings unitSettings : settings) {
            Object settingsObject = null;
            String settingsClass;
            try {
                settingsObject = unitSettings.getSettings();
                settingsClass = settingsObject == null ? "<null>" : settingsObject.getClass().getName();
            }
            catch (Throwable t) {
                // A settings class the current build no longer has, or a
                // version mismatch: worth reporting rather than aborting.
                settingsClass = "<unreadable: " + t.getClass().getSimpleName() + ">";
            }
            report.append(String.format(Locale.ROOT, "%s,%s,%d,%s%n",
                    safe(unitSettings.getUnitType()),
                    safe(unitSettings.getUnitName()),
                    unitSettings.getVersionNo(),
                    settingsClass));
        }

        System.out.print(report);
        if (args.length == 2) {
            File output = new File(args[1]);
            if (output.getParentFile() != null) {
                output.getParentFile().mkdirs();
            }
            try (PrintWriter writer = new PrintWriter(output)) {
                writer.print(report);
            }
        }
        System.out.println("modules=" + settings.size());
    }

    private static String safe(String value) {
        if (value == null) {
            return "";
        }
        return value.replace(',', ';');
    }

    @SuppressWarnings("unchecked")
    private static List<PamControlledUnitSettings> read(File settingsFile) throws Exception {
        String name = settingsFile.getName().toLowerCase(Locale.ROOT);
        if (name.endsWith(".psfx")) {
            PamSettingsGroup group = PSFXReadWriter.getInstance().loadFileSettings(settingsFile);
            return group == null ? null : group.getUnitSettings();
        }
        // .psf: a single Java-serialised ArrayList of unit settings.
        try (ObjectInputStream input = new ObjectInputStream(new FileInputStream(settingsFile))) {
            Object first = input.readObject();
            if (first instanceof ArrayList) {
                return (ArrayList<PamControlledUnitSettings>) first;
            }
            ArrayList<PamControlledUnitSettings> settings = new ArrayList<>();
            Object next = first;
            while (next != null) {
                if (next instanceof PamControlledUnitSettings) {
                    settings.add((PamControlledUnitSettings) next);
                }
                next = input.readObject();
            }
            return settings;
        }
        catch (java.io.EOFException e) {
            return null;
        }
    }
}
