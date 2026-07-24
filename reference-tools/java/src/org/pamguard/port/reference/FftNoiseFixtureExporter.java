package org.pamguard.port.reference;

import Acquisition.NoiseFixtureAcquisitionProcess;
import PamUtils.complex.ComplexArray;
import fftManager.FFTDataBlock;
import fftManager.FFTDataUnit;
import noiseMonitor.NoiseControl;
import noiseMonitor.NoiseDataBlock;
import noiseMonitor.NoiseDataUnit;
import noiseMonitor.NoiseMeasurementBand;
import noiseMonitor.NoiseProcess;
import noiseMonitor.NoiseSettings;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** Drives the real NoiseProcess band integration and statistics bytecode. */
public final class FftNoiseFixtureExporter {
    public static final class CaptureBlock extends NoiseDataBlock {
        static final List<NoiseDataUnit> captured = new ArrayList<>();
        public CaptureBlock() { super("capture", null, 3); }
        @Override public void addPamData(NoiseDataUnit unit) {
            captured.add(unit);
        }
    }

    private FftNoiseFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        Locale.setDefault(Locale.ROOT);
        if (args.length != 1) {
            System.err.println("Usage: FftNoiseFixtureExporter <output.csv>");
            System.exit(2);
        }
        NoiseSettings settings = new NoiseSettings();
        settings.channelBitmap = 3;
        settings.measurementIntervalSeconds = 1;
        settings.nMeasures = 100;
        settings.useAll = true;
        NoiseMeasurementBand fractional = new NoiseMeasurementBand(null);
        fractional.name = "fractional";
        fractional.f1 = 0.5;
        fractional.f2 = 2.5;
        settings.addNoiseMeasurementBand(fractional);
        NoiseMeasurementBand full = new NoiseMeasurementBand(null);
        full.name = "full";
        full.f1 = 0.0;
        full.f2 = 4.0;
        settings.addNoiseMeasurementBand(full);

        NoiseControl control = allocate(NoiseControl.class);
        setField(control, NoiseControl.class, "noiseSettings", settings);
        FFTDataBlock source = new FFTDataBlock("fixture", null, 3, 2, 8);
        source.setWindowGain(1.0);
        CaptureBlock output = allocate(CaptureBlock.class);
        output.setStatisticTypes(0x3f);
        NoiseProcess process = allocate(NoiseProcess.class);
        setField(process, NoiseProcess.class, "noiseControl", control);
        setField(process, NoiseProcess.class, "fftDataSource", source);
        setField(process, NoiseProcess.class, "fftLength", 8);
        setField(process, NoiseProcess.class, "fftHop", 2);
        setField(process, NoiseProcess.class, "noiseDataBlock", output);
        setField(process, NoiseProcess.class, "daqProcess",
                allocate(NoiseFixtureAcquisitionProcess.class));
        setField(process, NoiseProcess.class, "processGains", new double[32]);
        process.setSampleRate(8.0f, false);
        process.pamStart();
        Method newFft = NoiseProcess.class.getDeclaredMethod(
                "newFFTData", FFTDataUnit.class);
        newFft.setAccessible(true);
        CaptureBlock.captured.clear();

        try (PrintWriter writer = new PrintWriter(new File(args[0]))) {
            writer.println("# config,8,8,2,1,0,1,2,0.5,2.5,fractional,0,4,full");
            writer.println("# in,timeMillis,startSample,channel,re0,im0,...");
            writer.println("# out,channel,startSample,band0mean,...band1max");
            int slice = 0;
            for (long time = 1000; time <= 3250; time += 250) {
                long startSample = (time - 1000) / 125;
                for (int channel = 0; channel < 2; channel++) {
                    double[] complex = new double[8];
                    for (int bin = 0; bin < 4; bin++) {
                        complex[bin * 2] = 1.0 + slice * 0.2 +
                                channel * 0.35 + bin * 0.1;
                        complex[bin * 2 + 1] = (bin - channel) * 0.07;
                    }
                    writer.printf("in,%d,%d,%d", time, startSample, channel);
                    for (double value : complex) {
                        writer.printf(",%.17g", value);
                    }
                    writer.println();
                    FFTDataUnit unit = new FFTDataUnit(time, 1 << channel,
                            startSample, 8, new ComplexArray(complex), slice);
                    newFft.invoke(process, unit);
                    drain(writer);
                }
                slice++;
            }
        }
    }

    private static void drain(PrintWriter writer) {
        for (NoiseDataUnit unit : CaptureBlock.captured) {
            writer.printf("out,%d,%d", Integer.numberOfTrailingZeros(
                    unit.getChannelBitmap()), unit.getStartSample());
            for (double[] band : unit.getNoiseBandData()) {
                for (double value : band) {
                    writer.printf(",%.17g", value);
                }
            }
            writer.println();
        }
        CaptureBlock.captured.clear();
    }

    @SuppressWarnings("unchecked")
    private static <T> T allocate(Class<T> type) throws Exception {
        Constructor<?> objectConstructor = Object.class.getDeclaredConstructor();
        Constructor<?> allocator = sun.reflect.ReflectionFactory
                .getReflectionFactory()
                .newConstructorForSerialization(type, objectConstructor);
        return (T) allocator.newInstance();
    }

    private static void setField(Object target, Class<?> owner,
            String name, Object value) throws Exception {
        Field field = owner.getDeclaredField(name);
        field.setAccessible(true);
        field.set(target, value);
    }
}
