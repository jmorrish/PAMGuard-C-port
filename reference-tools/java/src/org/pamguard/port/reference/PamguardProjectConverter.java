package org.pamguard.port.reference;

import Acquisition.AcquisitionParameters;
import Array.Hydrophone;
import Array.PamArray;
import Array.Streamer;
import PamController.PSFXReadWriter;
import PamController.PamControlledUnitSettings;
import PamController.PamSettingsGroup;
import Spectrogram.WindowFunction;
import clickDetector.ClickParameters;
import fftManager.FFTParameters;
import whistlesAndMoans.WhistleToneParameters;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Locale;

/**
 * PAMGuard project import: converts a .psfx settings file into an engine
 * session-create JSON document.
 *
 * The .psfx payload is Java-serialised object graphs, so this must run on the
 * JVM with PAMGuard's own classes (docs/182 establishes why C++ cannot do it).
 * Reading uses the real PSFXReadWriter.loadFileSettings; settings objects are
 * matched by their real class, not by unit-name strings, so renamed modules
 * still convert.
 *
 * The pinned PAMGuard version is the local source tree this compiles against —
 * the same tree every parity fixture treats as the oracle. `write-sample`
 * produces a .psfx *from that version* using the real PamArray, Hydrophone,
 * Streamer, AcquisitionParameters, FFTParameters, ClickParameters, and
 * WhistleToneParameters classes and the real PSFXReadWriter.writePSFX, so the
 * converter has a genuine file to be validated against without waiting for an
 * external one. A .psfx from a different PAMGuard build may still fail to
 * deserialise; that is Java serialisation's version-brittleness, reported as
 * exit 3 exactly as PamguardSettingsInspector does.
 *
 * Unmapped settings are REPORTED, never silently dropped: every unit that did
 * not contribute to the JSON is listed on stdout with its class.
 *
 * Usage:
 *   PamguardProjectConverter convert <settings.psfx> <session.json>
 *   PamguardProjectConverter write-sample <sample.psfx>
 */
public final class PamguardProjectConverter {

    private PamguardProjectConverter() {
    }

    public static void main(String[] args) throws Exception {
        Locale.setDefault(Locale.ROOT);
        if (args.length == 3 && args[0].equals("convert")) {
            convert(new File(args[1]), new File(args[2]));
            return;
        }
        if (args.length == 2 && args[0].equals("write-sample")) {
            writeSample(new File(args[1]));
            return;
        }
        System.err.println("Usage: PamguardProjectConverter convert <settings.psfx> <session.json>");
        System.err.println("       PamguardProjectConverter write-sample <sample.psfx>");
        System.exit(2);
    }

    // ------------------------------------------------------------------
    // convert
    // ------------------------------------------------------------------

    private static void convert(File psfxFile, File jsonFile) throws Exception {
        if (!psfxFile.exists()) {
            System.err.println("Settings file not found: " + psfxFile.getAbsolutePath());
            System.exit(2);
        }
        PamSettingsGroup group;
        try {
            group = PSFXReadWriter.getInstance().loadFileSettings(psfxFile);
        }
        catch (Exception e) {
            if (hasCause(e, java.io.InvalidClassException.class)) {
                System.err.println("Settings file is not loadable by this PAMGuard build.");
                System.err.println("Serialisation incompatibility: " + e.getMessage());
                System.exit(3);
            }
            throw e;
        }
        if (group == null || group.getUnitSettings() == null || group.getUnitSettings().isEmpty()) {
            System.err.println("No unit settings found in " + psfxFile.getAbsolutePath());
            System.exit(1);
            return;
        }

        AcquisitionParameters acquisition = null;
        PamArray array = null;
        FFTParameters fft = null;
        ClickParameters click = null;
        WhistleToneParameters whistle = null;

        for (PamControlledUnitSettings unit : group.getUnitSettings()) {
            Object settings;
            try {
                settings = unit.getSettings();
            }
            catch (Throwable t) {
                System.out.printf("skipped: %s / %s (unreadable: %s)%n",
                        unit.getUnitType(), unit.getUnitName(), t.getClass().getSimpleName());
                continue;
            }
            if (settings instanceof AcquisitionParameters && acquisition == null) {
                acquisition = (AcquisitionParameters) settings;
            }
            else if (settings instanceof PamArray && array == null) {
                array = (PamArray) settings;
            }
            else if (settings instanceof FFTParameters && fft == null) {
                fft = (FFTParameters) settings;
            }
            else if (settings instanceof ClickParameters && click == null) {
                click = (ClickParameters) settings;
            }
            else if (settings instanceof WhistleToneParameters && whistle == null) {
                whistle = (WhistleToneParameters) settings;
            }
            else {
                System.out.printf("skipped: %s / %s (%s)%n", unit.getUnitType(), unit.getUnitName(),
                        settings == null ? "<null>" : settings.getClass().getName());
            }
        }

        if (acquisition == null) {
            System.err.println("No AcquisitionParameters in the settings file: sample rate and channel count are required.");
            System.exit(1);
            return;
        }

        StringBuilder json = new StringBuilder();
        json.append("{\n");
        json.append("  \"sessionId\": \"").append(escape(stripExtension(psfxFile.getName()))).append("-import\",\n");
        json.append("  \"sampleRateHz\": ").append(format(acquisition.sampleRate)).append(",\n");
        json.append("  \"channelCount\": ").append(acquisition.nChannels);

        if (array != null) {
            json.append(",\n  \"array\": {\n");
            json.append("    \"id\": \"").append(escape(psfxImportArrayName(array))).append("\",\n");
            json.append("    \"speedOfSoundMps\": ").append(format(array.getSpeedOfSound())).append(",\n");
            json.append("    \"speedOfSoundErrorMps\": ").append(format(array.getSpeedOfSoundError())).append(",\n");
            json.append("    \"streamers\": [");
            int streamerCount = array.getNumStreamers();
            for (int i = 0; i < streamerCount; i++) {
                Streamer streamer = array.getStreamer(i);
                if (i > 0) {
                    json.append(",");
                }
                json.append("\n      { \"id\": ").append(streamer.getStreamerIndex());
                json.append(", \"xM\": ").append(format(streamer.getCoordinate(0)));
                json.append(", \"yM\": ").append(format(streamer.getCoordinate(1)));
                json.append(", \"zM\": ").append(format(streamer.getCoordinate(2)));
                json.append(", \"headingDegrees\": ").append(format(zeroIfNull(streamer.getHeading())));
                json.append(", \"pitchDegrees\": ").append(format(zeroIfNull(streamer.getPitch())));
                json.append(", \"rollDegrees\": ").append(format(zeroIfNull(streamer.getRoll())));
                json.append(" }");
            }
            json.append("\n    ],\n");
            json.append("    \"hydrophones\": [");
            ArrayList<Hydrophone> hydrophones = array.getHydrophoneArray();
            for (int i = 0; i < hydrophones.size(); i++) {
                Hydrophone hydrophone = hydrophones.get(i);
                double[] coordinates = hydrophone.getCoordinates();
                double[] errors = coordinateErrors(hydrophone);
                if (i > 0) {
                    json.append(",");
                }
                // Engine channels are hydrophone indices; PAMGuard's channel-
                // to-hydrophone mapping lives in acquisition settings and is
                // identity in every configuration the engine supports.
                json.append("\n      { \"channel\": ").append(i);
                json.append(", \"xM\": ").append(format(coordinates[0]));
                json.append(", \"yM\": ").append(format(coordinates[1]));
                json.append(", \"zM\": ").append(format(coordinates[2]));
                json.append(", \"xErrorM\": ").append(format(errors[0]));
                json.append(", \"yErrorM\": ").append(format(errors[1]));
                json.append(", \"zErrorM\": ").append(format(errors[2]));
                json.append(", \"sensitivityDb\": ").append(format(hydrophone.getSensitivity()));
                json.append(", \"streamerId\": ").append(hydrophone.getStreamerId());
                json.append(" }");
            }
            json.append("\n    ]\n  }");
        }

        if (fft != null) {
            json.append(",\n  \"fft\": {\n");
            json.append("    \"length\": ").append(fft.fftLength).append(",\n");
            json.append("    \"hop\": ").append(fft.fftHop).append(",\n");
            // The engine accepts PAMGuard's WindowFunction integer directly.
            json.append("    \"windowType\": ").append(fft.windowFunction).append(",\n");
            json.append("    \"channels\": [").append(bitmapChannels(fft.channelMap)).append("]\n  }");
        }

        if (click != null) {
            json.append(",\n  \"click\": {\n");
            json.append("    \"enabled\": true,\n");
            json.append("    \"localisation\": ").append(array != null).append(",\n");
            json.append("    \"channelBitmap\": ").append(clickChannelBitmap(click)).append(",\n");
            json.append("    \"triggerBitmap\": ").append(click.triggerBitmap).append(",\n");
            json.append("    \"minTriggerChannels\": ").append(click.minTriggerChannels).append(",\n");
            json.append("    \"thresholdDb\": ").append(format(click.dbThreshold)).append(",\n");
            json.append("    \"longFilter\": ").append(format(click.longFilter)).append(",\n");
            json.append("    \"shortFilter\": ").append(format(click.shortFilter)).append(",\n");
            json.append("    \"preSample\": ").append(click.preSample).append(",\n");
            json.append("    \"postSample\": ").append(click.postSample).append(",\n");
            json.append("    \"minSep\": ").append(click.minSep).append(",\n");
            json.append("    \"maxLength\": ").append(click.maxLength).append("\n  }");
        }

        if (whistle != null) {
            if (fft == null) {
                // Frequency limits convert to FFT bins, so a whistle module
                // without an FFT module cannot be mapped faithfully.
                System.out.println("skipped: whistle settings present but no FFT settings to derive search bins from");
            }
            else {
                double sampleRate = acquisition.sampleRate;
                long bin0 = Math.round(whistle.getMinFrequency() / sampleRate * fft.fftLength);
                long bin1 = Math.round(whistle.getMaxFrequency(sampleRate) / sampleRate * fft.fftLength);
                json.append(",\n  \"whistle\": {\n");
                json.append("    \"enabled\": true,\n");
                json.append("    \"regionEnabled\": true,\n");
                json.append("    \"searchBin0\": ").append(bin0).append(",\n");
                json.append("    \"searchBin1\": ").append(bin1).append(",\n");
                json.append("    \"minPixels\": ").append(whistle.minPixels).append(",\n");
                json.append("    \"minLength\": ").append(whistle.minLength).append(",\n");
                json.append("    \"maxCrossLength\": ").append(whistle.maxCrossLength).append(",\n");
                json.append("    \"connectType\": ").append(whistle.getConnectType()).append(",\n");
                json.append("    \"fragmentationMethod\": ").append(whistle.fragmentationMethod).append(",\n");
                json.append("    \"keepShapeStubs\": ").append(whistle.keepShapeStubs).append("\n  }");
            }
        }

        json.append("\n}\n");

        jsonFile.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(jsonFile)) {
            writer.print(json);
        }
        System.out.printf("converted: acquisition=%s array=%s fft=%s click=%s whistle=%s -> %s%n",
                acquisition != null, array != null, fft != null, click != null, whistle != null,
                jsonFile.getPath());
    }

    // ------------------------------------------------------------------
    // write-sample
    // ------------------------------------------------------------------

    private static void writeSample(File psfxFile) throws Exception {
        // AcquisitionParameters' constructor reaches PamController.getInstance()
        // — the usual PamController wall. Deserialisation never calls
        // constructors, which is why *reading* real files works headlessly, so
        // allocate the sample instance the same way serialisation itself does.
        AcquisitionParameters acquisition = newWithoutConstructor(AcquisitionParameters.class);
        acquisition.sampleRate = 96000;
        acquisition.nChannels = 4;

        // PamArray's constructor reaches HydrophoneLocators.getInstance() and
        // through it PamController, so it gets the same serialisation-style
        // allocation; its list fields are initialisers the allocation skips,
        // so they are seeded reflectively before use.
        PamArray array = newWithoutConstructor(PamArray.class);
        setPrivateField(array, "arrayName", "converter-sample-array");
        setPrivateField(array, "streamers", new ArrayList<Streamer>());
        setPrivateField(array, "hydrophoneArray", new ArrayList<Hydrophone>());
        array.setSpeedOfSound(1502.0);
        array.setSpeedOfSoundError(3.0);
        Streamer streamer = new Streamer(0, 10.0, 5.0, 0.0, 0.01, 0.01, 0.01);
        streamer.setHeading(15.0);
        streamer.setPitch(-3.0);
        streamer.setRoll(2.0);
        array.addStreamer(streamer);
        double[][] positions = {
                {0.0, 0.0, 0.0},
                {2.0, 0.0, 0.0},
                {0.0, 3.0, 0.0},
                {0.5, 1.0, 2.5},
        };
        for (int i = 0; i < positions.length; i++) {
            Hydrophone hydrophone = new Hydrophone(i, positions[i][0], positions[i][1], positions[i][2],
                    0.05, 0.05, 0.05, "sample", -201.0, new double[]{100.0, 48000.0}, 20.0);
            hydrophone.setStreamerId(0);
            array.addHydrophone(hydrophone);
        }

        FFTParameters fft = new FFTParameters();
        fft.fftLength = 512;
        fft.fftHop = 256;
        fft.windowFunction = WindowFunction.HANNING;
        fft.channelMap = 0xF;

        ClickParameters click = new ClickParameters();
        click.dbThreshold = 12.0;
        click.longFilter = 0.00002;
        click.shortFilter = 0.2;
        click.preSample = 30;
        click.postSample = 50;
        click.minSep = 80;
        click.maxLength = 512;
        click.triggerBitmap = 0xF;
        click.minTriggerChannels = 2;
        setPrivateInt(click, "channelBitmap", 0xF);

        WhistleToneParameters whistle = new WhistleToneParameters();
        whistle.minPixels = 25;
        whistle.minLength = 12;
        whistle.maxCrossLength = 6;
        whistle.keepShapeStubs = true;
        whistle.setMinFrequency(2000.0);
        whistle.setMaxFrequency(20000.0);

        PamSettingsGroup group = new PamSettingsGroup(1782950400000L);
        group.addSettings(new PamControlledUnitSettings("Data Acquisition", "Sound Acquisition",
                AcquisitionParameters.class.getName(), 1, acquisition));
        group.addSettings(new PamControlledUnitSettings("Array Manager", "Array Manager",
                PamArray.class.getName(), 1, array));
        group.addSettings(new PamControlledUnitSettings("FFT Engine", "FFT Engine",
                FFTParameters.class.getName(), 1, fft));
        group.addSettings(new PamControlledUnitSettings("Click Detector", "Click Detector",
                ClickParameters.class.getName(), 1, click));
        group.addSettings(new PamControlledUnitSettings("WhistlesMoans", "Whistle and Moan Detector",
                WhistleToneParameters.class.getName(), 1, whistle));
        // A module the engine has no equivalent for, so the converter's
        // skip reporting always has something real to report.
        group.addSettings(new PamControlledUnitSettings("Spectrogram", "User Display",
                String.class.getName(), 1, "not-a-detector-module"));

        psfxFile.getParentFile().mkdirs();
        if (!PSFXReadWriter.getInstance().writePSFX(psfxFile.getPath(), group)) {
            System.err.println("writePSFX failed for " + psfxFile.getPath());
            System.exit(1);
        }
        System.out.println("wrote sample psfx: " + psfxFile.getPath());
    }

    // ------------------------------------------------------------------
    // helpers
    // ------------------------------------------------------------------

    /**
     * Allocate an instance without running its constructor, exactly as
     * ObjectInputStream does for a Serializable class. Needed only for sample
     * generation, where a settings class's constructor is PamController-coupled.
     */
    @SuppressWarnings("unchecked")
    private static <T> T newWithoutConstructor(Class<T> type) throws Exception {
        java.lang.reflect.Constructor<?> objectConstructor = Object.class.getDeclaredConstructor();
        java.lang.reflect.Constructor<?> allocator = sun.reflect.ReflectionFactory.getReflectionFactory()
                .newConstructorForSerialization(type, objectConstructor);
        return (T) allocator.newInstance();
    }

    private static boolean hasCause(Throwable error, Class<? extends Throwable> type) {
        while (error != null) {
            if (type.isInstance(error)) {
                return true;
            }
            error = error.getCause();
        }
        return false;
    }

    private static String psfxImportArrayName(PamArray array) {
        String name = array.getArrayName();
        return name == null || name.isEmpty() ? "imported-array" : name;
    }

    /** Hydrophone.getCoordinateErrors is protected, so reach it reflectively. */
    private static double[] coordinateErrors(Hydrophone hydrophone) throws Exception {
        Method method = Hydrophone.class.getDeclaredMethod("getCoordinateErrors");
        method.setAccessible(true);
        double[] errors = (double[]) method.invoke(hydrophone);
        return errors == null ? new double[3] : errors;
    }

    private static int clickChannelBitmap(ClickParameters click) throws Exception {
        Field field = ClickParameters.class.getDeclaredField("channelBitmap");
        field.setAccessible(true);
        return field.getInt(click);
    }

    private static void setPrivateInt(Object target, String fieldName, int value) throws Exception {
        Field field = target.getClass().getDeclaredField(fieldName);
        field.setAccessible(true);
        field.setInt(target, value);
    }

    private static void setPrivateField(Object target, String fieldName, Object value) throws Exception {
        Field field = target.getClass().getDeclaredField(fieldName);
        field.setAccessible(true);
        field.set(target, value);
    }

    private static String bitmapChannels(int bitmap) {
        StringBuilder channels = new StringBuilder();
        for (int channel = 0; channel < 32; channel++) {
            if ((bitmap & (1 << channel)) != 0) {
                if (channels.length() > 0) {
                    channels.append(", ");
                }
                channels.append(channel);
            }
        }
        return channels.toString();
    }

    private static double zeroIfNull(Double value) {
        return value == null ? 0.0 : value;
    }

    private static String format(double value) {
        if (value == Math.rint(value) && Math.abs(value) < 1.0e15) {
            return String.format(Locale.ROOT, "%.1f", value);
        }
        return String.format(Locale.ROOT, "%.17g", value);
    }

    private static String escape(String text) {
        return text.replace("\\", "\\\\").replace("\"", "\\\"");
    }

    private static String stripExtension(String name) {
        int dot = name.lastIndexOf('.');
        return dot > 0 ? name.substring(0, dot) : name;
    }
}
