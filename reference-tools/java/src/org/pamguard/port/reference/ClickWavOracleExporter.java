package org.pamguard.port.reference;

import Acquisition.DCFilter;
import Filters.Filter;
import Filters.FilterMethod;
import clickDetector.ClickParameters;
import clickDetector.TriggerFilter;
import wavFiles.ByteConverter;

import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioInputStream;
import javax.sound.sampled.AudioSystem;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Runs a mono WAV through the pinned PAMGuard click detector's real input
 * converter, pre-filter, trigger filter, and trigger average implementation.
 *
 * <p>The state machine below is a deliberately direct transcription of
 * {@code ClickDetector.ChannelGroupDetector.lookForClicks}. Keeping this small
 * harness outside PAMGuard avoids constructing a controller/GUI while still
 * exercising the authoritative numerical classes.</p>
 */
public final class ClickWavOracleExporter {
    private enum ClickStatus {
        CLICK_ON, CLICK_ENDING, CLICK_OFF
    }

    private static final class ClickRow {
        long triggerStartSample;
        long startSample;
        int duration;
        int triggerBitmap;
        double signalExcessDb;
        long completionSample;
    }

    private ClickWavOracleExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 2 || args.length > 5) {
            System.err.println(
                    "Usage: ClickWavOracleExporter <input.wav> <clicks.csv> "
                            + "[trace.csv] [acquisitionDcSeconds] [blockSamples]");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File inputFile = new File(args[0]);
        File clickOutput = new File(args[1]);
        File traceOutput = args.length >= 3 && !args[2].equals("-")
                ? new File(args[2])
                : null;
        double dcSeconds = args.length == 4 ? Double.parseDouble(args[3]) : 0.0;

        ClickParameters params = new ClickParameters();
        final int triggerBitmap = 1;
        final int channelBitmap = 1;

        List<ClickRow> completedClicks = new ArrayList<>();
        try (AudioInputStream input = AudioSystem.getAudioInputStream(inputFile)) {
            AudioFormat format = input.getFormat();
            if (format.getChannels() != 1) {
                throw new IllegalArgumentException("oracle currently requires a mono WAV");
            }
            if (format.getFrameSize() <= 0) {
                throw new IllegalArgumentException("WAV has no usable frame size");
            }

            final int sampleRate = Math.round(format.getSampleRate());
            final int blockSamples = args.length == 5
                    ? Integer.parseInt(args[4])
                    : Math.max(sampleRate / 10, 500);
            if (blockSamples <= 0) {
                throw new IllegalArgumentException("blockSamples must be positive");
            }
            final int blockBytes = blockSamples * format.getFrameSize();
            final double threshold = Math.pow(10.0, params.dbThreshold / 20.0);

            ByteConverter converter = ByteConverter.createByteConverter(format);
            if (converter == null) {
                throw new IllegalArgumentException(
                        "PAMGuard has no byte converter for " + format);
            }
            Method bytesToDouble = converter.getClass().getDeclaredMethod(
                    "bytesToDouble", byte[].class, double[][].class, int.class);
            bytesToDouble.setAccessible(true);

            Filter preFilter =
                    FilterMethod.createFilterMethod(sampleRate, params.preFilter).createFilter(0);
            Filter triggerFilter =
                    FilterMethod.createFilterMethod(sampleRate, params.triggerFilter).createFilter(0);
            final int firstFilterDelay = preFilter.getFilterDelay();
            final int secondFilterDelay = triggerFilter.getFilterDelay();
            DCFilter acquisitionDc = dcSeconds > 0.0
                    ? new DCFilter(sampleRate, dcSeconds, 1)
                    : null;

            TriggerFilter shortFilter = new TriggerFilter(params.shortFilter, 0.0);
            TriggerFilter longFilter = new TriggerFilter(params.longFilter, 1.0);
            boolean initialiseFilters = true;
            ClickStatus clickStatus = ClickStatus.CLICK_OFF;
            long samplesProcessed = 0;
            long blockStartSample = 0;
            long clickStartSample = 0;
            long clickEndSample = 0;
            double maxSignalExcess = 0.0;
            int clickTriggers = 0;
            int overThreshold = 0;
            int downCount = 0;
            ClickRow pendingClick = null;

            BufferedWriter trace = null;
            try {
                if (traceOutput != null) {
                    File traceParent = traceOutput.getParentFile();
                    if (traceParent != null) {
                        traceParent.mkdirs();
                    }
                    trace = new BufferedWriter(new FileWriter(traceOutput), 1 << 20);
                    trace.write(
                            "sample,raw,preFiltered,triggerFiltered,shortValue,"
                                    + "longValue,signalExcessDb,overThreshold\n");
                }

                byte[] byteData = new byte[blockBytes];
                while (true) {
                    int bytesRead = readBlock(input, byteData, blockBytes);
                    if (bytesRead <= 0) {
                        break;
                    }
                    int frameCount = bytesRead / format.getFrameSize();
                    double[][] channels = new double[1][frameCount];
                    bytesToDouble.invoke(converter, byteData, channels, bytesRead);
                    double[] raw = channels[0];
                    if (acquisitionDc != null) {
                        acquisitionDc.filterData(0, raw);
                    }
                    double[] preFiltered = new double[frameCount];
                    double[] triggerData = new double[frameCount];
                    preFilter.runFilter(raw, preFiltered);
                    triggerFilter.runFilter(preFiltered, triggerData);

                    if (initialiseFilters) {
                        initialiseFilters = false;
                        double mean = 0.0;
                        for (double sample : triggerData) {
                            mean += Math.abs(sample);
                        }
                        mean /= frameCount;
                        shortFilter.setMemory(mean * threshold);
                        longFilter.setMemory(mean);
                    }

                    for (int i = 0; i < frameCount; i++) {
                        double shortValue = shortFilter.runFilter(triggerData[i], false);
                        double longValue =
                                longFilter.runFilter(triggerData[i], overThreshold != 0);
                        overThreshold = setBit(
                                overThreshold,
                                0,
                                shortValue > longValue * threshold);
                        double signalExcess = longValue > 0.0
                                ? 20.0 * Math.log10(shortValue / longValue)
                                : -100.0;
                        long absoluteSample = blockStartSample + i;

                        if (trace != null) {
                            trace.write(Long.toString(absoluteSample));
                            trace.write(',');
                            trace.write(Double.toString(raw[i]));
                            trace.write(',');
                            trace.write(Double.toString(preFiltered[i]));
                            trace.write(',');
                            trace.write(Double.toString(triggerData[i]));
                            trace.write(',');
                            trace.write(Double.toString(shortValue));
                            trace.write(',');
                            trace.write(Double.toString(longValue));
                            trace.write(',');
                            trace.write(Double.toString(signalExcess));
                            trace.write(',');
                            trace.write(Integer.toString(overThreshold));
                            trace.write('\n');
                        }

                        if (clickStatus == ClickStatus.CLICK_OFF && overThreshold != 0) {
                            clickStatus = ClickStatus.CLICK_ON;
                            clickStartSample = Math.max(
                                    0, absoluteSample - params.preSample);
                            clickEndSample = absoluteSample;
                            clickTriggers = overThreshold;
                            maxSignalExcess = signalExcess;
                            downCount = 0;
                        }
                        else if (clickStatus == ClickStatus.CLICK_ENDING) {
                            if (overThreshold != 0) {
                                clickStatus = ClickStatus.CLICK_ON;
                                downCount = 0;
                                clickEndSample = absoluteSample;
                            }
                            else if (++downCount > params.minSep) {
                                clickStatus = ClickStatus.CLICK_OFF;
                                int duration = (int) Math.min(
                                        clickEndSample + params.postSample
                                                - clickStartSample + 1,
                                        params.maxLength);
                                if (Integer.bitCount(clickTriggers)
                                        >= params.minTriggerChannels) {
                                    ClickRow row = new ClickRow();
                                    row.triggerStartSample = clickStartSample;
                                    row.startSample = clickStartSample
                                            - firstFilterDelay - secondFilterDelay;
                                    row.duration = duration;
                                    row.triggerBitmap = clickTriggers;
                                    row.signalExcessDb = maxSignalExcess;
                                    row.completionSample = Math.max(
                                            samplesProcessed,
                                            clickStartSample + duration + 1L);
                                    pendingClick = row;
                                }
                            }
                        }
                        else if (clickStatus == ClickStatus.CLICK_ON) {
                            if (overThreshold == 0) {
                                clickStatus = ClickStatus.CLICK_ENDING;
                            }
                            else {
                                clickTriggers |= overThreshold;
                                clickEndSample = absoluteSample;
                                maxSignalExcess =
                                        Math.max(maxSignalExcess, signalExcess);
                            }
                        }

                        if (pendingClick != null
                                && pendingClick.completionSample == samplesProcessed) {
                            completedClicks.add(pendingClick);
                            pendingClick = null;
                        }
                        samplesProcessed++;
                    }
                    blockStartSample += frameCount;
                }
            }
            finally {
                if (trace != null) {
                    trace.close();
                }
            }

            writeClicks(
                    clickOutput,
                    completedClicks,
                    channelBitmap,
                    sampleRate,
                    blockSamples,
                    firstFilterDelay,
                    secondFilterDelay);
        }
    }

    private static int readBlock(
            AudioInputStream input, byte[] destination, int requested) throws Exception {
        int total = 0;
        while (total < requested) {
            int count = input.read(destination, total, requested - total);
            if (count < 0) {
                break;
            }
            if (count == 0) {
                continue;
            }
            total += count;
        }
        return total;
    }

    private static int setBit(int bitmap, int bitNumber, boolean bitSet) {
        int mask = 1 << bitNumber;
        return bitSet ? bitmap | mask : bitmap & ~mask;
    }

    private static void writeClicks(
            File output,
            List<ClickRow> clicks,
            int channelBitmap,
            int sampleRate,
            int blockSamples,
            int firstFilterDelay,
            int secondFilterDelay) throws Exception {
        File parent = output.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(output))) {
            writer.write(
                    "index,startSample,triggerStartSample,duration,channelBitmap,"
                            + "triggerBitmap,signalExcessDb,completionSample,sampleRate,"
                            + "blockSamples,firstFilterDelay,secondFilterDelay\n");
            for (int i = 0; i < clicks.size(); i++) {
                ClickRow row = clicks.get(i);
                writer.write(Integer.toString(i));
                writer.write(',');
                writer.write(Long.toString(row.startSample));
                writer.write(',');
                writer.write(Long.toString(row.triggerStartSample));
                writer.write(',');
                writer.write(Integer.toString(row.duration));
                writer.write(',');
                writer.write(Integer.toString(channelBitmap));
                writer.write(',');
                writer.write(Integer.toString(row.triggerBitmap));
                writer.write(',');
                writer.write(Double.toString(row.signalExcessDb));
                writer.write(',');
                writer.write(Long.toString(row.completionSample));
                writer.write(',');
                writer.write(Integer.toString(sampleRate));
                writer.write(',');
                writer.write(Integer.toString(blockSamples));
                writer.write(',');
                writer.write(Integer.toString(firstFilterDelay));
                writer.write(',');
                writer.write(Integer.toString(secondFilterDelay));
                writer.write('\n');
            }
        }
    }
}
