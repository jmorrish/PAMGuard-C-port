package org.pamguard.port.reference;

import IshmaelDetector.IshDataBlock;
import IshmaelDetector.IshDetFnDataUnit;
import IshmaelDetector.IshDetection;
import IshmaelDetector.IshPeakProcess;
import IshmaelDetector.MatchFiltControl;
import IshmaelDetector.MatchFiltParams;
import IshmaelDetector.MatchFiltProcess2;
import PamDetection.RawDataUnit;
import PamView.GroupedSourceParameters;
import PamguardMVC.PamDataBlock;
import PamguardMVC.PamDataUnit;

import javax.sound.sampled.AudioFileFormat;
import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioInputStream;
import javax.sound.sampled.AudioSystem;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Array;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Random;

/**
 * Exports Ishmael matched-filter fixtures by driving the REAL
 * MatchFiltProcess2 (the live implementation; v1 is deprecated in the
 * reference): the kernel is written to a real 16-bit WAV file and read
 * back through the reference's own AudioSystem path in prepareKernel, so
 * the quantisation, the FFT-length choice, the packed-spectrum conjTimes
 * cross-correlation, the scaled real inverse, and the sliding-energy
 * normalisation are all PAMGuard bytecode. The detection-function BLOCKS
 * (fftLength - kernelLength samples each) then feed the REAL
 * IshPeakProcess, pinning its per-sample walk over multi-sample units for
 * the first time.
 *
 * The CSV emits the READ-BACK kernel (16-bit quantised) so the C++ check
 * replays identical doubles. Read by
 * cpp-engine/tools/match_filt_fixture_check.cpp.
 */
public final class MatchFiltFixtureExporter {

    /** Detection-function output block capture. */
    public static final class FnCaptureBlock extends PamDataBlock<PamDataUnit> {
        static final List<IshDetFnDataUnit> captured = new ArrayList<>();

        public FnCaptureBlock() {
            super(PamDataUnit.class, "fn-capture", null, 1);
        }

        @Override
        public void addPamData(PamDataUnit pamDataUnit) {
            captured.add((IshDetFnDataUnit) pamDataUnit);
        }
    }

    /** Peak-picker output block capture. */
    public static final class DetCaptureBlock extends IshDataBlock {
        static final List<IshDetection> captured = new ArrayList<>();
        static float blockSampleRate = 8000.0F;

        public DetCaptureBlock() {
            super("det-capture", null, 1);
        }

        @Override
        public void addPamData(IshDetection pamDataUnit) {
            captured.add(pamDataUnit);
        }

        @Override
        public float getSampleRate() {
            return blockSampleRate;
        }
    }

    private static final class MfCase {
        String name;
        float sampleRate;
        int kernelLength;
        double kernelFreqHz;
        MatchFiltParams params;

        MfCase(String name, float sampleRate, int kernelLength, double kernelFreqHz,
               MatchFiltParams params) {
            this.name = name;
            this.sampleRate = sampleRate;
            this.kernelLength = kernelLength;
            this.kernelFreqHz = kernelFreqHz;
            this.params = params;
        }
    }

    private MatchFiltFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: MatchFiltFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        Field sessionStart = Class.forName("PamUtils.PamCalendar").getDeclaredField("sessionStartTime");
        sessionStart.setAccessible(true);
        sessionStart.set(null, 0L);

        Random random = new Random(20260727L);
        int totalIn = 0;
        int totalBlocks = 0;
        int totalDet = 0;

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("# case,<name>,<sampleRate>,<kernelLen>,<thresh>,<minTime>,<maxTime>,<refractoryTime>");
            writer.println("# kernelwave,<values...> (as read back through AudioSystem, 16-bit quantised)");
            writer.println("# prep,<fftLength>,<usefulSamples>,<normConst>");
            writer.println("# in,<startSample>,<values...>");
            writer.println("# fnblock,<startSample>,<values...>");
            writer.println("# detection,<channel>,<startSample>,<durationSam>,<peakTimeSam>,<peakHeight>,<startMsec>,<lowFreq>,<highFreq>");

            for (MfCase mfCase : caseCatalogue()) {
                MatchFiltParams p = mfCase.params;
                writer.printf(Locale.ROOT, "case,%s,%.17g,%d,%.17g,%.17g,%.17g,%.17g%n",
                        mfCase.name, mfCase.sampleRate, mfCase.kernelLength,
                        p.thresh, p.minTime, p.maxTime, p.refractoryTime);
                int[] counts = runCase(mfCase, random, writer);
                totalIn += counts[0];
                totalBlocks += counts[1];
                totalDet += counts[2];
            }
        }
        System.out.println("MatchFilt fixture: in=" + totalIn + " blocks=" + totalBlocks
                + " detections=" + totalDet);
    }

    private static int[] runCase(MfCase mfCase, Random random, PrintWriter writer) throws Exception {
        int[] counts = new int[3];

        // ---- Write a real 16-bit WAV kernel and reference it. ----
        File kernelFile = File.createTempFile("mf-kernel-" + mfCase.name, ".wav");
        kernelFile.deleteOnExit();
        writeKernelWav(kernelFile, mfCase, random);
        mfCase.params.groupedSourceParmas = new GroupedSourceParameters();
        mfCase.params.groupedSourceParmas.setChanOrSeqBitmap(1);
        Field listField = MatchFiltParams.class.getDeclaredField("kernelFilenameList");
        listField.setAccessible(true);
        ArrayList<String> names = new ArrayList<>();
        names.add(kernelFile.getAbsolutePath());
        listField.set(mfCase.params, names);

        // ---- The real control and process. ----
        MatchFiltControl control = newWithoutConstructor(MatchFiltControl.class);
        setField(control, Class.forName("IshmaelDetector.IshDetControl"), "ishDetParams", mfCase.params);

        MatchFiltProcess2 process = newWithoutConstructor(MatchFiltProcess2.class);
        setField(process, Class.forName("IshmaelDetector.IshDetFnProcess"), "ishDetControl", control);
        setField(process, Class.forName("IshmaelDetector.IshDetFnProcess"), "outputDataBlock",
                newWithoutConstructor(FnCaptureBlock.class));
        setField(process, Class.forName("PamguardMVC.PamProcess"), "sampleRate", mfCase.sampleRate);
        setField(control, Class.forName("IshmaelDetector.IshDetControl"), "ishDetFnProcess", process);

        // prepareKernel for real: reads the WAV through AudioSystem, builds
        // the packed kernel spectrum, usefulSamples, and normConst.
        Method prepareKernel = MatchFiltProcess2.class.getDeclaredMethod("prepareKernel", MatchFiltParams.class);
        prepareKernel.setAccessible(true);
        boolean kernelOK = (Boolean) prepareKernel.invoke(process, mfCase.params);
        if (!kernelOK) {
            throw new IllegalStateException("prepareKernel failed for " + mfCase.name);
        }

        Field kernelField = MatchFiltProcess2.class.getDeclaredField("kernel");
        kernelField.setAccessible(true);
        double[] kernel = (double[]) kernelField.get(process);
        writeValues(writer, "kernelwave", kernel);
        Field fftLenField = MatchFiltProcess2.class.getDeclaredField("fftLength");
        fftLenField.setAccessible(true);
        Field usefulField = MatchFiltProcess2.class.getDeclaredField("usefulSamples");
        usefulField.setAccessible(true);
        Field normField = MatchFiltProcess2.class.getDeclaredField("normConst");
        normField.setAccessible(true);
        writer.printf(Locale.ROOT, "prep,%d,%d,%.17g%n", fftLenField.getInt(process),
                usefulField.getInt(process), normField.getDouble(process));

        // ---- The real per-channel detector (channel 0, the reference's
        // no-groups default). ----
        Class<?> channelDetectorClass = Class.forName("IshmaelDetector.MatchFiltProcess2$ChannelDetector");
        Constructor<?> cdCtor = channelDetectorClass.getDeclaredConstructor(MatchFiltProcess2.class, int.class);
        cdCtor.setAccessible(true);
        Object channelDetector = cdCtor.newInstance(process, 0);
        Method cdNewData = channelDetectorClass.getDeclaredMethod("newData", RawDataUnit.class);
        cdNewData.setAccessible(true);

        // ---- The real IshPeakProcess. ----
        IshPeakProcess peakProcess = newWithoutConstructor(IshPeakProcess.class);
        setField(peakProcess, IshPeakProcess.class, "ishDetControl", control);
        DetCaptureBlock.blockSampleRate = mfCase.sampleRate;
        setField(peakProcess, IshPeakProcess.class, "outputDataBlock",
                newWithoutConstructor(DetCaptureBlock.class));
        setField(peakProcess, Class.forName("PamguardMVC.PamProcess"), "sampleRate", mfCase.sampleRate);
        Class<?> peakChannelClass = Class.forName("IshmaelDetector.IshPeakProcess$PerChannelInfo");
        setField(peakProcess, IshPeakProcess.class, "perChannelInfo",
                Array.newInstance(peakChannelClass, PamguardMVC.PamConstants.MAX_CHANNELS));
        Method peakPrepare = IshPeakProcess.class.getDeclaredMethod("prepareMyParams");
        peakPrepare.setAccessible(true);
        peakPrepare.invoke(peakProcess);
        peakProcess.prepareForRun();
        Method peakNewData = IshPeakProcess.class.getDeclaredMethod("newData",
                Class.forName("PamguardMVC.PamObservable"), PamDataUnit.class);
        peakNewData.setAccessible(true);
        FnCaptureBlock.captured.clear();
        DetCaptureBlock.captured.clear();

        // ---- Drive: noise with scaled kernel copies embedded, in odd
        // 1000-sample raw chunks. ----
        int totalSamples = 12000;
        double[] signal = new double[totalSamples];
        for (int i = 0; i < totalSamples; i++) {
            signal[i] = (random.nextDouble() * 2 - 1) * 0.03;
        }
        int[] offsets = {2500, 5200, 8800};
        double[] scales = {0.5, 0.9, 0.25};
        for (int e = 0; e < offsets.length; e++) {
            for (int i = 0; i < kernel.length && offsets[e] + i < totalSamples; i++) {
                signal[offsets[e] + i] += scales[e] * kernel[i];
            }
        }

        int chunkLen = 1000;
        long startSample = 0;
        for (int pos = 0; pos < totalSamples; pos += chunkLen) {
            int n = Math.min(chunkLen, totalSamples - pos);
            double[] raw = new double[n];
            System.arraycopy(signal, pos, raw, 0, n);
            long timeMillis = (long) (startSample * 1000.0 / mfCase.sampleRate);
            RawDataUnit unit = new RawDataUnit(timeMillis, 1, startSample, n);
            unit.setRawData(raw);

            writeValues(writer, "in," + startSample, raw);
            counts[0]++;

            cdNewData.invoke(channelDetector, unit);
            for (IshDetFnDataUnit fnUnit : FnCaptureBlock.captured) {
                writeValues(writer, "fnblock," + fnUnit.getStartSample(), fnUnit.getDetData()[0]);
                counts[1]++;
                peakNewData.invoke(peakProcess, null, fnUnit);
                for (IshDetection det : DetCaptureBlock.captured) {
                    writer.printf(Locale.ROOT, "detection,%d,%d,%d,%d,%.17g,%d,%.17g,%.17g%n",
                            Integer.numberOfTrailingZeros(det.getChannelBitmap()),
                            det.getStartSample(), det.getSampleDuration(),
                            (long) det.getPeakTimeSam(), det.getPeakHeight(),
                            det.getTimeMilliseconds(),
                            (double) det.getFrequency()[0], (double) det.getFrequency()[1]);
                    counts[2]++;
                }
                DetCaptureBlock.captured.clear();
            }
            FnCaptureBlock.captured.clear();
            startSample += n;
        }
        return counts;
    }

    private static void writeKernelWav(File file, MfCase mfCase, Random random) throws Exception {
        double[] kernel = new double[mfCase.kernelLength];
        for (int i = 0; i < kernel.length; i++) {
            kernel[i] = 0.8 * Math.exp(-3.0 * i / kernel.length)
                    * Math.sin(2 * Math.PI * mfCase.kernelFreqHz * i / mfCase.sampleRate)
                    + 0.02 * (random.nextDouble() * 2 - 1);
        }
        byte[] bytes = new byte[kernel.length * 2];
        for (int i = 0; i < kernel.length; i++) {
            int value = (int) Math.round(kernel[i] * 32767.0);
            value = Math.max(-32768, Math.min(32767, value));
            bytes[2 * i] = (byte) (value & 0xFF);
            bytes[2 * i + 1] = (byte) ((value >> 8) & 0xFF);
        }
        AudioFormat format = new AudioFormat(mfCase.sampleRate, 16, 1, true, false);
        AudioInputStream stream = new AudioInputStream(new ByteArrayInputStream(bytes), format, kernel.length);
        AudioSystem.write(stream, AudioFileFormat.Type.WAVE, file);
        stream.close();
    }

    private static void writeValues(PrintWriter writer, String prefix, double[] values) {
        StringBuilder row = new StringBuilder(prefix);
        for (double value : values) {
            row.append(String.format(Locale.ROOT, ",%.17g", value));
        }
        writer.println(row);
    }

    @SuppressWarnings("unchecked")
    private static <T> T newWithoutConstructor(Class<T> type) throws Exception {
        Constructor<?> objectConstructor = Object.class.getDeclaredConstructor();
        Constructor<?> allocator = sun.reflect.ReflectionFactory.getReflectionFactory()
                .newConstructorForSerialization(type, objectConstructor);
        return (T) allocator.newInstance();
    }

    private static void setField(Object target, Class<?> declaringClass, String fieldName, Object value)
            throws Exception {
        Field field = declaringClass.getDeclaredField(fieldName);
        field.setAccessible(true);
        field.set(target, value);
    }

    private static MfCase[] caseCatalogue() {
        // 300-sample chirpy kernel at 8 kHz: bufLen = max(800, 600) -> fft
        // 1024, useful 724.
        MatchFiltParams a = new MatchFiltParams();
        a.thresh = 0.4;
        a.minTime = 0.01;
        a.refractoryTime = 0.05;

        // 150-sample kernel at 2 kHz: bufLen = max(200, 300) -> fft 512,
        // useful 362.
        MatchFiltParams b = new MatchFiltParams();
        b.thresh = 0.4;
        b.minTime = 0.02;
        b.refractoryTime = 0.1;

        return new MfCase[]{
                new MfCase("kernel300-8k", 8000.0F, 300, 1200.0, a),
                new MfCase("kernel150-2k", 2000.0F, 150, 400.0, b),
        };
    }
}
