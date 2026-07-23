package org.pamguard.port.reference;

import PamUtils.complex.ComplexArray;
import fftManager.FFTDataBlock;
import fftManager.FFTDataUnit;
import ltsa.LtsaControl;
import ltsa.LtsaDataBlock;
import ltsa.LtsaDataUnit;
import ltsa.LtsaParameters;
import ltsa.LtsaProcess;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Random;

/**
 * Exports LTSA fixtures by driving the REAL LtsaProcess.ChannelProcess.
 *
 * LtsaProcess is PamController-coupled through its constructor, but
 * ChannelProcess itself reads only ltsaControl.ltsaParameters, the source
 * block's channel/sequence maps, and the output block's addPamData. So the
 * control and process are allocated the way deserialisation allocates (no
 * constructor), those fields are set reflectively, and the output block is
 * a subclass whose addPamData captures the emitted LtsaDataUnit instead of
 * entering PamDataBlock's observer machinery. Every accumulation, boundary
 * decision, and sqrt(mean) that runs is PAMGuard's own bytecode.
 *
 * The CSV interleaves the inputs (complex FFT slices, timestamps) with the
 * outputs in emission order, so the C++ check
 * (cpp-engine/tools/ltsa_fixture_check.cpp) replays the identical stream.
 * Deliberately covered: the epoch alignment of the first period (a
 * non-aligned start makes it partial), and the closePeriod single-interval
 * advance, which after a time gap files slices into stale windows until the
 * window catches up.
 */
public final class LtsaFixtureExporter {

    /** Output block whose addPamData captures instead of publishing. */
    public static final class CaptureBlock extends LtsaDataBlock {
        static final List<FFTDataUnit> captured = new ArrayList<>();

        public CaptureBlock() {
            super("capture", null, false);
        }

        @Override
        public void addPamData(FFTDataUnit pamDataUnit) {
            captured.add(pamDataUnit);
        }
    }

    private static final class LtsaCase {
        String name;
        int intervalSeconds;
        int fftLength;
        int channel;
        long[] timesMillis;

        LtsaCase(String name, int intervalSeconds, int fftLength, int channel, long[] timesMillis) {
            this.name = name;
            this.intervalSeconds = intervalSeconds;
            this.fftLength = fftLength;
            this.channel = channel;
            this.timesMillis = timesMillis;
        }
    }

    private LtsaFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: LtsaFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        Random random = new Random(20260723L);
        int totalIn = 0;
        int totalOut = 0;

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("# case,<name>,<intervalSeconds>,<fftLength>,<channel>");
            writer.println("# in,<timeMillis>,<startSample>,<duration>,<re0>,<im0>,...");
            writer.println("# out,<startMillis>,<endMillis>,<nFFT>,<startSample>,<duration>,<mag0>,...");
            for (LtsaCase ltsaCase : caseCatalogue()) {
                writer.printf(Locale.ROOT, "case,%s,%d,%d,%d%n",
                        ltsaCase.name, ltsaCase.intervalSeconds, ltsaCase.fftLength, ltsaCase.channel);

                LtsaProcess process = buildProcess(ltsaCase.intervalSeconds);
                Object channelProcess = newChannelProcess(process, ltsaCase.channel);
                Method newData = channelProcess.getClass().getDeclaredMethod("newData", FFTDataUnit.class);
                newData.setAccessible(true);
                Method closePeriod = channelProcess.getClass().getDeclaredMethod("closePeriod");
                closePeriod.setAccessible(true);
                CaptureBlock.captured.clear();

                int half = ltsaCase.fftLength / 2;
                long startSample = 1000;
                int slice = 0;
                for (long timeMillis : ltsaCase.timesMillis) {
                    double[] complexData = new double[half * 2];
                    for (int i = 0; i < complexData.length; i++) {
                        complexData[i] = random.nextDouble() * 2.0 - 1.0;
                    }
                    FFTDataUnit unit = new FFTDataUnit(timeMillis, 1 << ltsaCase.channel,
                            startSample, ltsaCase.fftLength, new ComplexArray(complexData), slice++);

                    StringBuilder inRow = new StringBuilder();
                    inRow.append(String.format(Locale.ROOT, "in,%d,%d,%d", timeMillis, startSample,
                            ltsaCase.fftLength));
                    for (double value : complexData) {
                        inRow.append(String.format(Locale.ROOT, ",%.17g", value));
                    }
                    writer.println(inRow);
                    totalIn++;

                    newData.invoke(channelProcess, unit);
                    totalOut += drainCaptured(writer);
                    startSample += ltsaCase.fftLength / 2;
                }
                // flushDataBlockBuffers path: close the trailing period.
                closePeriod.invoke(channelProcess);
                totalOut += drainCaptured(writer);
            }
        }
        System.out.println("LTSA fixture: in=" + totalIn + " out=" + totalOut);
    }

    private static int drainCaptured(PrintWriter writer) {
        int drained = 0;
        for (FFTDataUnit unit : CaptureBlock.captured) {
            LtsaDataUnit ltsaUnit = (LtsaDataUnit) unit;
            StringBuilder outRow = new StringBuilder();
            outRow.append(String.format(Locale.ROOT, "out,%d,%d,%d,%d,%d",
                    ltsaUnit.getTimeMilliseconds(), ltsaUnit.getEndMilliseconds(), ltsaUnit.getnFFT(),
                    ltsaUnit.getStartSample(), ltsaUnit.getSampleDuration()));
            double[] data = ltsaUnit.getFftData().getData();
            for (int i = 0; i < data.length; i += 2) {
                outRow.append(String.format(Locale.ROOT, ",%.17g", data[i]));
            }
            writer.println(outRow);
            drained++;
        }
        CaptureBlock.captured.clear();
        return drained;
    }

    private static LtsaProcess buildProcess(int intervalSeconds) throws Exception {
        LtsaParameters params = new LtsaParameters();
        params.intervalSeconds = intervalSeconds;

        LtsaControl control = newWithoutConstructor(LtsaControl.class);
        setField(control, LtsaControl.class, "ltsaParameters", params);

        LtsaProcess process = newWithoutConstructor(LtsaProcess.class);
        setField(process, LtsaProcess.class, "ltsaControl", control);
        // closePeriod reads the source block's channel map (0 when allocated
        // without a constructor) and null sequence map — sortOutputMaps
        // handles both.
        setField(process, LtsaProcess.class, "sourceDataBlock",
                newWithoutConstructor(FFTDataBlock.class));
        setField(process, LtsaProcess.class, "ltsaDataBlock",
                newWithoutConstructor(CaptureBlock.class));
        return process;
    }

    private static Object newChannelProcess(LtsaProcess process, int channel) throws Exception {
        Class<?> channelProcessClass = Class.forName("ltsa.LtsaProcess$ChannelProcess");
        Constructor<?> constructor = channelProcessClass.getDeclaredConstructor(LtsaProcess.class, int.class);
        constructor.setAccessible(true);
        return constructor.newInstance(process, channel);
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

    private static LtsaCase[] caseCatalogue() {
        return new LtsaCase[]{
                // Aligned start: 2 s periods, slices every 250 ms.
                new LtsaCase("aligned-2s", 2, 16, 0, stepTimes(0, 250, 20)),
                // Non-aligned start: the first period is partial because the
                // window aligns to absolute interval boundaries (12000 here).
                new LtsaCase("unaligned-start", 2, 8, 1, stepTimes(12345, 250, 12)),
                // A 7 s gap in 2 s periods: closePeriod advances one interval
                // per close, so post-gap slices land in stale windows until
                // the window catches up. Deliberately pinned.
                new LtsaCase("gap-stale-windows", 2, 8, 0, gapTimes()),
                // A single slice, closed only by the flush.
                new LtsaCase("one-unit-flush", 60, 32, 3, stepTimes(99999, 0, 1)),
                // Odd step against a 5 s interval.
                new LtsaCase("odd-step-5s", 5, 32, 2, stepTimes(777, 617, 30)),
        };
    }

    private static long[] stepTimes(long start, long step, int count) {
        long[] times = new long[count];
        for (int i = 0; i < count; i++) {
            times[i] = start + step * i;
        }
        return times;
    }

    private static long[] gapTimes() {
        long[] before = stepTimes(500, 250, 6);
        long[] after = stepTimes(500 + 250 * 5 + 7000, 250, 8);
        long[] times = new long[before.length + after.length];
        System.arraycopy(before, 0, times, 0, before.length);
        System.arraycopy(after, 0, times, before.length, after.length);
        return times;
    }
}
