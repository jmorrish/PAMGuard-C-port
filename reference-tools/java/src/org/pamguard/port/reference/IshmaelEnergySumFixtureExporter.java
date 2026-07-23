package org.pamguard.port.reference;

import IshmaelDetector.EnergySumControl;
import IshmaelDetector.EnergySumParams;
import IshmaelDetector.EnergySumProcess;
import IshmaelDetector.IshDataBlock;
import IshmaelDetector.IshDetFnDataUnit;
import IshmaelDetector.IshDetection;
import IshmaelDetector.IshPeakProcess;
import PamUtils.complex.ComplexArray;
import PamView.GroupedSourceParameters;
import PamguardMVC.PamDataBlock;
import PamguardMVC.PamDataUnit;
import fftManager.FFTDataBlock;
import fftManager.FFTDataUnit;

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
 * Exports Ishmael energy-sum fixtures by driving the REAL EnergySumProcess
 * and IshPeakProcess end to end.
 *
 * Both processes are PamController-coupled through their constructors, so
 * they are allocated the way deserialisation allocates and the fields their
 * numeric paths read are set reflectively; the output data blocks are
 * capture subclasses whose addPamData collects the emitted units instead of
 * entering the observer machinery. EnergySumProcess.prepareMyParams runs
 * for real (the source FFT block is given a last unit and its internal list
 * fields), so the bin mapping, the sums, the smoothing quirk, the adaptive
 * noise floor, and the whole peak-picking state machine are PAMGuard's own
 * bytecode.
 *
 * The CSV interleaves inputs with the detection-function values and picked
 * detections in emission order for the C++ check
 * (cpp-engine/tools/ishmael_fixture_check.cpp). Deliberately pinned:
 * minTime/refractoryTime converted to FFT-slice units but compared against
 * RAW-sample durations and intervals, endMsec's multiply-by-detection-rate,
 * the write-once smoothing state, and the shared-across-channels noise
 * floor.
 */
public final class IshmaelEnergySumFixtureExporter {

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

    /** Peak-picker output block capture; also supplies the sample rate that
     * IshDetection's constructor asks the block for. */
    public static final class DetCaptureBlock extends IshDataBlock {
        static final List<IshDetection> captured = new ArrayList<>();
        static float blockSampleRate = 48000.0F;

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

    private static final class IshCase {
        String name;
        float sampleRate;
        int fftLength;      // gram height = fftLength/2
        int fftHop;
        int[] channels;     // interleaved channel pattern per slice
        EnergySumParams params;

        IshCase(String name, float sampleRate, int fftLength, int fftHop, int[] channels,
                EnergySumParams params) {
            this.name = name;
            this.sampleRate = sampleRate;
            this.fftLength = fftLength;
            this.fftHop = fftHop;
            this.channels = channels;
            this.params = params;
        }
    }

    private IshmaelEnergySumFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: IshmaelEnergySumFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        // Deterministic absolute times.
        Field sessionStart = Class.forName("PamUtils.PamCalendar").getDeclaredField("sessionStartTime");
        sessionStart.setAccessible(true);
        sessionStart.set(null, 0L);

        Random random = new Random(20260724L);
        int totalIn = 0;
        int totalDet = 0;

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("# case,<name>,<sampleRate>,<fftLength>,<fftHop>,<f0>,<f1>,<ratiof0>,<ratiof1>,"
                    + "<useRatio>,<useLog>,<adaptive>,<longFilter>,<spikeDecay>,<smoothing>,<shortFilter>,"
                    + "<thresh>,<minTime>,<maxTime>,<refractoryTime>");
            writer.println("# in,<channel>,<startSample>,<burst>,<re0>,<im0>,...");
            writer.println("# fn,<detValue>[,<noiseFloor>,<rawValue>]");
            writer.println("# detection,<channel>,<startSample>,<durationSam>,<peakTimeSam>,<peakHeight>,<startMsec>,<lowFreq>,<highFreq>");
            for (IshCase ishCase : caseCatalogue()) {
                EnergySumParams p = ishCase.params;
                writer.printf(Locale.ROOT,
                        "case,%s,%.17g,%d,%d,%.17g,%.17g,%.17g,%.17g,%d,%d,%d,%.17g,%.17g,%d,%.17g,%.17g,%.17g,%.17g,%.17g%n",
                        ishCase.name, ishCase.sampleRate, ishCase.fftLength, ishCase.fftHop,
                        p.f0, p.f1, p.ratiof0, p.ratiof1,
                        p.useRatio ? 1 : 0, p.useLog ? 1 : 0, p.adaptiveThreshold ? 1 : 0,
                        p.longFilter, p.spikeDecay, p.outPutSmoothing ? 1 : 0, p.shortFilter,
                        p.thresh, p.minTime, p.maxTime, p.refractoryTime);

                runCase(ishCase, random, writer);
                totalIn += counts[0];
                totalDet += counts[1];
            }
        }
        System.out.println("Ishmael fixture: in=" + totalIn + " detections=" + totalDet);
    }

    private static final int[] counts = new int[2];

    private static void runCase(IshCase ishCase, Random random, PrintWriter writer) throws Exception {
        counts[0] = 0;
        counts[1] = 0;
        int half = ishCase.fftLength / 2;

        // ---- The real EnergySumControl with real params. ----
        EnergySumControl control = newWithoutConstructor(EnergySumControl.class);
        ishCase.params.groupedSourceParmas = new GroupedSourceParameters();
        int bitmap = 0;
        for (int chan : ishCase.channels) {
            bitmap |= 1 << chan;
        }
        ishCase.params.groupedSourceParmas.setChanOrSeqBitmap(bitmap);
        setField(control, Class.forName("IshmaelDetector.IshDetControl"), "ishDetParams", ishCase.params);

        // ---- Source FFT block with enough internals for prepareMyParams
        // (getLastUnit) and getDetSampleRate (fftHop). ----
        FFTDataBlock sourceBlock = newWithoutConstructor(FFTDataBlock.class);
        setField(sourceBlock, FFTDataBlock.class, "fftHop", ishCase.fftHop);
        FFTDataUnit seed = new FFTDataUnit(0, 1, 0, ishCase.fftLength, new ComplexArray(half), 0);
        List<FFTDataUnit> units = new ArrayList<>();
        units.add(seed);
        setField(sourceBlock, Class.forName("PamguardMVC.PamDataBlock"), "pamDataUnits", units);
        setField(sourceBlock, Class.forName("PamguardMVC.PamDataBlock"), "synchronizationLock", new Object());

        // ---- The real EnergySumProcess. ----
        EnergySumProcess energyProcess = newWithoutConstructor(EnergySumProcess.class);
        // Constructor-skipping allocation bypasses field initialisers, so
        // restore the -Double.MIN_VALUE "unset" sentinels a normally
        // constructed process starts with.
        setField(energyProcess, EnergySumProcess.class, "noisefloor", -Double.MIN_VALUE);
        setField(energyProcess, EnergySumProcess.class, "smoothResult", -Double.MIN_VALUE);
        setField(energyProcess, Class.forName("IshmaelDetector.IshDetFnProcess"), "ishDetControl", control);
        setField(energyProcess, Class.forName("IshmaelDetector.IshDetFnProcess"), "outputDataBlock",
                newWithoutConstructor(FnCaptureBlock.class));
        setField(energyProcess, Class.forName("PamguardMVC.PamProcess"), "parentDataBlock", sourceBlock);
        setField(energyProcess, Class.forName("PamguardMVC.PamProcess"), "sampleRate", ishCase.sampleRate);
        setField(control, Class.forName("IshmaelDetector.IshDetControl"), "ishDetFnProcess", energyProcess);

        // prepareMyParams for real: bin mapping from the seed unit's length.
        Method prepare = EnergySumProcess.class.getDeclaredMethod("prepareMyParams");
        prepare.setAccessible(true);
        prepare.invoke(energyProcess);

        // ---- The real IshPeakProcess. ----
        IshPeakProcess peakProcess = newWithoutConstructor(IshPeakProcess.class);
        setField(peakProcess, IshPeakProcess.class, "ishDetControl", control);
        DetCaptureBlock.blockSampleRate = ishCase.sampleRate;
        setField(peakProcess, IshPeakProcess.class, "outputDataBlock",
                newWithoutConstructor(DetCaptureBlock.class));
        setField(peakProcess, Class.forName("PamguardMVC.PamProcess"), "sampleRate", ishCase.sampleRate);
        Class<?> perChannelClass = Class.forName("IshmaelDetector.IshPeakProcess$PerChannelInfo");
        setField(peakProcess, IshPeakProcess.class, "perChannelInfo",
                Array.newInstance(perChannelClass, PamguardMVC.PamConstants.MAX_CHANNELS));
        Method peakPrepare = IshPeakProcess.class.getDeclaredMethod("prepareMyParams");
        peakPrepare.setAccessible(true);
        peakPrepare.invoke(peakProcess);
        peakProcess.prepareForRun();

        Method energyNewData = EnergySumProcess.class.getDeclaredMethod("newData",
                Class.forName("PamguardMVC.PamObservable"), PamDataUnit.class);
        energyNewData.setAccessible(true);
        Method peakNewData = IshPeakProcess.class.getDeclaredMethod("newData",
                Class.forName("PamguardMVC.PamObservable"), PamDataUnit.class);
        peakNewData.setAccessible(true);
        FnCaptureBlock.captured.clear();
        DetCaptureBlock.captured.clear();

        // ---- Drive the chain: background noise with burst windows that
        // boost only the lower quarter of the spectrum (inside the sum
        // bands, outside the ratio bands) well over threshold. ----
        long startSample = 0;
        int slice = 0;
        int nSlices = 160;
        for (int k = 0; k < nSlices; k++) {
            int chan = ishCase.channels[k % ishCase.channels.length];
            boolean burst = (k >= 40 && k < 44) || (k >= 60 && k < 76) || (k >= 90 && k < 92)
                    || (k >= 100 && k < 104) || (k >= 108 && k < 112) || (k >= 140 && k < 150);
            double[] complexData = new double[half * 2];
            for (int i = 0; i < complexData.length; i++) {
                double scale = (burst && i < half / 2) ? 4.0 : 0.05;
                complexData[i] = (random.nextDouble() * 2.0 - 1.0) * scale;
            }
            long timeMillis = (long) (startSample * 1000.0 / ishCase.sampleRate);
            FFTDataUnit unit = new FFTDataUnit(timeMillis, 1 << chan, startSample,
                    ishCase.fftLength, new ComplexArray(complexData), slice++);

            StringBuilder inRow = new StringBuilder();
            inRow.append(String.format(Locale.ROOT, "in,%d,%d,%d", chan, startSample, burst ? 1 : 0));
            for (double value : complexData) {
                inRow.append(String.format(Locale.ROOT, ",%.17g", value));
            }
            writer.println(inRow);
            counts[0]++;

            energyNewData.invoke(energyProcess, null, unit);
            for (IshDetFnDataUnit fnUnit : FnCaptureBlock.captured) {
                double[][] detData = fnUnit.getDetData();
                StringBuilder fnRow = new StringBuilder();
                fnRow.append(String.format(Locale.ROOT, "fn,%.17g", detData[0][0]));
                if (detData.length == 3) {
                    fnRow.append(String.format(Locale.ROOT, ",%.17g,%.17g", detData[1][0], detData[2][0]));
                }
                writer.println(fnRow);
                // Feed the same unit into the real peak picker, exactly as
                // the observer wiring would.
                peakNewData.invoke(peakProcess, null, fnUnit);
                for (IshDetection det : DetCaptureBlock.captured) {
                    writer.printf(Locale.ROOT, "detection,%d,%d,%d,%d,%.17g,%d,%.17g,%.17g%n",
                            Integer.numberOfTrailingZeros(det.getChannelBitmap()),
                            det.getStartSample(), det.getSampleDuration(),
                            (long) det.getPeakTimeSam(), det.getPeakHeight(),
                            det.getTimeMilliseconds(),
                            (double) det.getFrequency()[0], (double) det.getFrequency()[1]);
                    counts[1]++;
                }
                DetCaptureBlock.captured.clear();
            }
            FnCaptureBlock.captured.clear();
            startSample += ishCase.fftHop;
        }
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

    private static EnergySumParams params(double f0, double f1, double thresh) {
        EnergySumParams p = new EnergySumParams();
        p.f0 = f0;
        p.f1 = f1;
        p.thresh = thresh;
        return p;
    }

    private static IshCase[] caseCatalogue() {
        // Linear sums, plain threshold, single channel.
        EnergySumParams plain = params(200.0, 2000.0, 1.0);
        plain.minTime = 0.02;
        plain.refractoryTime = 0.05;

        // dB sums with the ratio band subtraction.
        EnergySumParams dbRatio = params(500.0, 4000.0, 1.5);
        dbRatio.useLog = true;
        dbRatio.useRatio = true;
        dbRatio.ratiof0 = 8000.0;
        dbRatio.ratiof1 = 16000.0;
        dbRatio.minTime = 0.01;

        // Adaptive noise floor with spike decay, plus the write-once
        // smoothing quirk.
        EnergySumParams adaptive = params(200.0, 3000.0, 0.5);
        adaptive.adaptiveThreshold = true;
        adaptive.longFilter = 0.01;
        adaptive.spikeDecay = 20.0;
        adaptive.outPutSmoothing = true;
        adaptive.shortFilter = 0.3;
        adaptive.minTime = 0.02;
        adaptive.refractoryTime = 0.1;

        // Max-time cap and two interleaved channels sharing the noise floor.
        EnergySumParams capped = params(100.0, 1500.0, 1.0);
        capped.maxTime = 0.06;
        capped.minTime = 0.01;
        capped.refractoryTime = 0.08;
        capped.adaptiveThreshold = true;
        capped.longFilter = 0.005;

        return new IshCase[]{
                new IshCase("linear-basic", 48000.0F, 512, 256, new int[]{0}, plain),
                new IshCase("db-ratio", 48000.0F, 1024, 512, new int[]{0}, dbRatio),
                new IshCase("adaptive-smooth", 24000.0F, 512, 128, new int[]{2}, adaptive),
                new IshCase("max-time-two-chan", 48000.0F, 512, 256, new int[]{0, 1}, capped),
        };
    }
}
