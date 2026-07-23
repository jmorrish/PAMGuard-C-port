package org.pamguard.port.reference;

import IshmaelDetector.IshDataBlock;
import IshmaelDetector.IshDetFnDataUnit;
import IshmaelDetector.IshDetection;
import IshmaelDetector.IshPeakProcess;
import IshmaelDetector.SgramCorrControl;
import IshmaelDetector.SgramCorrParams;
import IshmaelDetector.SgramCorrProcess;
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
 * Exports Ishmael spectrogram-correlation fixtures by driving the REAL
 * SgramCorrProcess (kernel construction via prepareMyParams against a
 * seeded source block, per-channel circular gram buffers, the kernel/gram
 * dot product) chained into the REAL IshPeakProcess, with the same
 * allocation and capture techniques as IshmaelEnergySumFixtureExporter.
 *
 * The kernel itself is exported row by row so the C++ port's kernel is
 * compared directly, not just through the detection function.
 *
 * CSV is read by cpp-engine/tools/sgram_corr_fixture_check.cpp.
 */
public final class SgramCorrFixtureExporter {

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

    private static final class SgramCase {
        String name;
        float sampleRate;
        int fftLength;
        int fftHop;
        int[] channels;
        SgramCorrParams params;

        SgramCase(String name, float sampleRate, int fftLength, int fftHop, int[] channels,
                  SgramCorrParams params) {
            this.name = name;
            this.sampleRate = sampleRate;
            this.fftLength = fftLength;
            this.fftHop = fftHop;
            this.channels = channels;
            this.params = params;
        }
    }

    private SgramCorrFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: SgramCorrFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        Field sessionStart = Class.forName("PamUtils.PamCalendar").getDeclaredField("sessionStartTime");
        sessionStart.setAccessible(true);
        sessionStart.set(null, 0L);

        Random random = new Random(20260726L);
        int totalIn = 0;
        int totalFn = 0;
        int totalDet = 0;

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("# case,<name>,<sampleRate>,<fftLength>,<fftHop>,<spread>,<useLog>,<thresh>,<minTime>,<maxTime>,<refractoryTime>");
            writer.println("# segment,<t0>,<f0>,<t1>,<f1>");
            writer.println("# kernel,<binOffset>,<durN>,<nBin>,<loFreq>,<hiFreq>  then kernelrow,<i>,<values...>");
            writer.println("# in,<channel>,<startSample>,<re0>,<im0>,...");
            writer.println("# fn,<value>");
            writer.println("# detection,<channel>,<startSample>,<durationSam>,<peakTimeSam>,<peakHeight>,<startMsec>,<lowFreq>,<highFreq>");

            for (SgramCase sgramCase : caseCatalogue()) {
                SgramCorrParams p = sgramCase.params;
                writer.printf(Locale.ROOT, "case,%s,%.17g,%d,%d,%.17g,%d,%.17g,%.17g,%.17g,%.17g%n",
                        sgramCase.name, sgramCase.sampleRate, sgramCase.fftLength, sgramCase.fftHop,
                        p.spread, p.useLog ? 1 : 0, p.thresh, p.minTime, p.maxTime, p.refractoryTime);
                for (double[] seg : p.segment) {
                    writer.printf(Locale.ROOT, "segment,%.17g,%.17g,%.17g,%.17g%n",
                            seg[0], seg[1], seg[2], seg[3]);
                }

                int[] counts = runCase(sgramCase, random, writer);
                totalIn += counts[0];
                totalFn += counts[1];
                totalDet += counts[2];
            }
        }
        System.out.println("SgramCorr fixture: in=" + totalIn + " fn=" + totalFn + " detections=" + totalDet);
    }

    private static int[] runCase(SgramCase sgramCase, Random random, PrintWriter writer) throws Exception {
        int half = sgramCase.fftLength / 2;
        int[] counts = new int[3];

        // ---- Real control with real params. ----
        SgramCorrControl control = newWithoutConstructor(SgramCorrControl.class);
        sgramCase.params.groupedSourceParmas = new GroupedSourceParameters();
        int bitmap = 0;
        for (int chan : sgramCase.channels) {
            bitmap |= 1 << chan;
        }
        sgramCase.params.groupedSourceParmas.setChanOrSeqBitmap(bitmap);
        setField(control, Class.forName("IshmaelDetector.IshDetControl"), "ishDetParams", sgramCase.params);

        // ---- Seeded source block so prepareMyParams runs for real. ----
        FFTDataBlock sourceBlock = newWithoutConstructor(FFTDataBlock.class);
        setField(sourceBlock, FFTDataBlock.class, "fftHop", sgramCase.fftHop);
        FFTDataUnit seed = new FFTDataUnit(0, 1, 0, sgramCase.fftLength, new ComplexArray(half), 0);
        List<FFTDataUnit> units = new ArrayList<>();
        units.add(seed);
        setField(sourceBlock, Class.forName("PamguardMVC.PamDataBlock"), "pamDataUnits", units);
        setField(sourceBlock, Class.forName("PamguardMVC.PamDataBlock"), "synchronizationLock", new Object());

        // ---- The real SgramCorrProcess. ----
        SgramCorrProcess sgramProcess = newWithoutConstructor(SgramCorrProcess.class);
        setField(sgramProcess, Class.forName("IshmaelDetector.IshDetFnProcess"), "ishDetControl", control);
        setField(sgramProcess, Class.forName("IshmaelDetector.IshDetFnProcess"), "outputDataBlock",
                newWithoutConstructor(FnCaptureBlock.class));
        setField(sgramProcess, Class.forName("PamguardMVC.PamProcess"), "parentDataBlock", sourceBlock);
        setField(sgramProcess, Class.forName("PamguardMVC.PamProcess"), "sampleRate", sgramCase.sampleRate);
        // Constructor-skipping allocation left savedGramHeight 0 and the
        // per-channel array null; restore both before prepareMyParams runs
        // renewPerChannelInfo.
        setField(sgramProcess, SgramCorrProcess.class, "savedGramHeight", -1);
        Class<?> perChannelClass = Class.forName("IshmaelDetector.SgramCorrProcess$PerChannelInfo");
        setField(sgramProcess, SgramCorrProcess.class, "perChannelInfo",
                Array.newInstance(perChannelClass, PamguardMVC.PamConstants.MAX_CHANNELS));
        setField(control, Class.forName("IshmaelDetector.IshDetControl"), "ishDetFnProcess", sgramProcess);

        Method prepare = SgramCorrProcess.class.getDeclaredMethod("prepareMyParams");
        prepare.setAccessible(true);
        prepare.invoke(sgramProcess);

        // Export the real kernel.
        Field kernelField = SgramCorrProcess.class.getDeclaredField("kernel");
        kernelField.setAccessible(true);
        double[][] kernel = (double[][]) kernelField.get(sgramProcess);
        Field binOffsetField = SgramCorrProcess.class.getDeclaredField("binOffset");
        binOffsetField.setAccessible(true);
        int binOffset = binOffsetField.getInt(sgramProcess);
        writer.printf(Locale.ROOT, "kernel,%d,%d,%d,%.17g,%.17g%n", binOffset, kernel.length,
                kernel[0].length, (double) sgramProcess.getLoFreq(), (double) sgramProcess.getHiFreq());
        for (int i = 0; i < kernel.length; i++) {
            StringBuilder row = new StringBuilder(String.format(Locale.ROOT, "kernelrow,%d", i));
            for (double value : kernel[i]) {
                row.append(String.format(Locale.ROOT, ",%.17g", value));
            }
            writer.println(row);
        }

        // ---- The real IshPeakProcess (as in the energy-sum exporter). ----
        IshPeakProcess peakProcess = newWithoutConstructor(IshPeakProcess.class);
        setField(peakProcess, IshPeakProcess.class, "ishDetControl", control);
        DetCaptureBlock.blockSampleRate = sgramCase.sampleRate;
        setField(peakProcess, IshPeakProcess.class, "outputDataBlock",
                newWithoutConstructor(DetCaptureBlock.class));
        setField(peakProcess, Class.forName("PamguardMVC.PamProcess"), "sampleRate", sgramCase.sampleRate);
        Class<?> peakChannelClass = Class.forName("IshmaelDetector.IshPeakProcess$PerChannelInfo");
        setField(peakProcess, IshPeakProcess.class, "perChannelInfo",
                Array.newInstance(peakChannelClass, PamguardMVC.PamConstants.MAX_CHANNELS));
        Method peakPrepare = IshPeakProcess.class.getDeclaredMethod("prepareMyParams");
        peakPrepare.setAccessible(true);
        peakPrepare.invoke(peakProcess);
        peakProcess.prepareForRun();

        Method sgramNewData = SgramCorrProcess.class.getDeclaredMethod("newData",
                Class.forName("PamguardMVC.PamObservable"), PamDataUnit.class);
        sgramNewData.setAccessible(true);
        Method peakNewData = IshPeakProcess.class.getDeclaredMethod("newData",
                Class.forName("PamguardMVC.PamObservable"), PamDataUnit.class);
        peakNewData.setAccessible(true);
        FnCaptureBlock.captured.clear();
        DetCaptureBlock.captured.clear();

        // ---- Drive the chain: background noise, with the matching sweep
        // painted into the kernel band over some slice windows. ----
        long startSample = 0;
        int slice = 0;
        int nSlices = 120;
        double binBW = (sgramCase.sampleRate / 2.0) / half;
        for (int k = 0; k < nSlices; k++) {
            int chan = sgramCase.channels[k % sgramCase.channels.length];
            // Sweep-shaped energy: during a burst window the segment's
            // frequency track (of the FIRST segment) gets a strong bin.
            boolean burst = (k >= 30 && k < 30 + kernel.length) || (k >= 80 && k < 80 + kernel.length);
            double[] complexData = new double[half * 2];
            for (int i = 0; i < complexData.length; i++) {
                complexData[i] = (random.nextDouble() * 2.0 - 1.0) * 0.05;
            }
            if (burst) {
                int burstStart = k >= 80 ? 80 : 30;
                double frameRate = sgramCase.sampleRate / sgramCase.fftHop;
                double t = (k - burstStart) / frameRate;
                double[] seg = sgramCase.params.segment[0];
                if (t >= seg[0] && t <= seg[2]) {
                    double f = (t - seg[0]) / (seg[2] - seg[0]) * (seg[3] - seg[1]) + seg[1];
                    int bin = (int) Math.round(f / binBW);
                    if (bin >= 0 && bin < half) {
                        complexData[2 * bin] += 3.0;
                        complexData[2 * bin + 1] += 1.5;
                    }
                }
            }
            long timeMillis = (long) (startSample * 1000.0 / sgramCase.sampleRate);
            FFTDataUnit unit = new FFTDataUnit(timeMillis, 1 << chan, startSample,
                    sgramCase.fftLength, new ComplexArray(complexData), slice++);

            StringBuilder inRow = new StringBuilder();
            inRow.append(String.format(Locale.ROOT, "in,%d,%d", chan, startSample));
            for (double value : complexData) {
                inRow.append(String.format(Locale.ROOT, ",%.17g", value));
            }
            writer.println(inRow);
            counts[0]++;

            sgramNewData.invoke(sgramProcess, null, unit);
            for (IshDetFnDataUnit fnUnit : FnCaptureBlock.captured) {
                writer.printf(Locale.ROOT, "fn,%.17g%n", fnUnit.getDetData()[0][0]);
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
            startSample += sgramCase.fftHop;
        }
        return counts;
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

    private static SgramCase[] caseCatalogue() {
        // Upsweep 800->1600 Hz over 0.25 s, linear power.
        SgramCorrParams upsweep = new SgramCorrParams();
        upsweep.segment = new double[][]{{0.0, 800.0, 0.25, 1600.0}};
        upsweep.spread = 60.0;
        upsweep.thresh = 0.02;
        upsweep.minTime = 0.05;
        upsweep.refractoryTime = 0.2;

        // Two-segment contour (up then down), LOG power, two channels.
        SgramCorrParams contour = new SgramCorrParams();
        contour.segment = new double[][]{
                {0.0, 600.0, 0.15, 1200.0},
                {0.15, 1200.0, 0.3, 700.0}};
        contour.spread = 80.0;
        contour.useLog = true;
        contour.thresh = 0.5;
        contour.minTime = 0.05;
        contour.refractoryTime = 0.3;

        // Wide spread pushing the kernel to the gramHeight/2 top clamp.
        SgramCorrParams clamped = new SgramCorrParams();
        clamped.segment = new double[][]{{0.0, 1800.0, 0.1, 1950.0}};
        clamped.spread = 200.0;
        clamped.thresh = 0.05;
        clamped.minTime = 0.02;

        return new SgramCase[]{
                new SgramCase("upsweep-linear", 8000.0F, 256, 128, new int[]{0}, upsweep),
                new SgramCase("contour-log-two-chan", 8000.0F, 256, 128, new int[]{0, 1}, contour),
                new SgramCase("top-clamp", 8000.0F, 256, 128, new int[]{2}, clamped),
        };
    }
}
