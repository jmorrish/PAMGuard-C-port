package org.pamguard.port.reference;

import clickDetector.TriggerFilter;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public final class ClickTriggerFixtureExporter {
    private enum ClickStatus {
        CLICK_ON, CLICK_ENDING, CLICK_OFF
    }

    private static final class ClickRow {
        int index;
        long startSample;
        int duration;
        int channelBitmap;
        int triggerBitmap;
        double signalExcessDb;
        long timeMs;
    }

    private ClickTriggerFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 13) {
            System.err.println("Usage: ClickTriggerFixtureExporter <channelBitmap> <triggerBitmap> <thresholdDb> <shortFilter> <longFilter> <preSample> <postSample> <minSep> <maxLength> <minTriggerChannels> <sampleRate> <chunkLength> <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        int arg = 0;
        int channelBitmap = Integer.decode(args[arg++]);
        int triggerBitmap = Integer.decode(args[arg++]);
        double thresholdDb = Double.parseDouble(args[arg++]);
        double shortFilter = Double.parseDouble(args[arg++]);
        double longFilter = Double.parseDouble(args[arg++]);
        int preSample = Integer.parseInt(args[arg++]);
        int postSample = Integer.parseInt(args[arg++]);
        int minSep = Integer.parseInt(args[arg++]);
        int maxLength = Integer.parseInt(args[arg++]);
        int minTriggerChannels = Integer.parseInt(args[arg++]);
        int sampleRate = Integer.parseInt(args[arg++]);
        int chunkLength = Integer.parseInt(args[arg++]);
        File output = new File(args[arg]);

        List<Integer> channels = channelList(channelBitmap);
        TriggerFilter[] shortFilters = new TriggerFilter[channels.size()];
        TriggerFilter[] longFilters = new TriggerFilter[channels.size()];
        for (int i = 0; i < channels.size(); i++) {
            shortFilters[i] = new TriggerFilter(shortFilter, 0);
            longFilters[i] = new TriggerFilter(longFilter, 1);
        }

        double threshold = Math.pow(10.0, thresholdDb / 20.0);
        double[][] triggerData = new double[channels.size()][chunkLength];
        for (int iChan = 0; iChan < channels.size(); iChan++) {
            int channel = channels.get(iChan);
            for (int i = 0; i < chunkLength; i++) {
                triggerData[iChan][i] = syntheticSample(channel, i);
            }
        }

        for (int iChan = 0; iChan < channels.size(); iChan++) {
            int channel = channels.get(iChan);
            if ((1 << channel & triggerBitmap) == 0) {
                continue;
            }
            double shortVal = 0.0;
            double longVal = 0.0;
            for (int i = 0; i < chunkLength; i++) {
                shortVal += Math.abs(triggerData[iChan][i]);
                longVal += Math.abs(triggerData[iChan][i]);
            }
            shortFilters[iChan].setMemory(shortVal / chunkLength * threshold);
            longFilters[iChan].setMemory(longVal / chunkLength);
        }

        ClickStatus clickStatus = ClickStatus.CLICK_OFF;
        long blockStartSample = 0;
        long samplesProcessed = 0;
        long clickStartSample = 0;
        long clickEndSample = 0;
        double maxSignalExcess = 0.0;
        int clickTriggers = 0;
        int overThreshold = 0;
        int downCount = 0;
        int upCount = 0;
        List<ClickRow> detections = new ArrayList<>();

        for (int iSamp = 0; iSamp < chunkLength; iSamp++) {
            double maxSE = -10000.0;
            for (int iChan = 0; iChan < channels.size(); iChan++) {
                int channel = channels.get(iChan);
                if ((1 << channel & triggerBitmap) == 0) {
                    continue;
                }
                double shortVal = shortFilters[iChan].runFilter(triggerData[iChan][iSamp], false);
                double longVal = longFilters[iChan].runFilter(triggerData[iChan][iSamp], overThreshold != 0);
                overThreshold = setBit(overThreshold, channel, shortVal > longVal * threshold);
                double dB = longVal > 0.0 ? 20.0 * Math.log10(shortVal / longVal) : -100.0;
                maxSE = Math.max(maxSE, dB);
            }

            if (clickStatus == ClickStatus.CLICK_OFF && overThreshold != 0) {
                clickStatus = ClickStatus.CLICK_ON;
                clickStartSample = Math.max(0, blockStartSample + iSamp - preSample);
                clickEndSample = blockStartSample + iSamp;
                clickTriggers = overThreshold;
                maxSignalExcess = maxSE;
                downCount = 0;
                upCount = 1;
            }
            else if (clickStatus == ClickStatus.CLICK_ENDING) {
                if (overThreshold != 0) {
                    clickStatus = ClickStatus.CLICK_ON;
                    downCount = 0;
                    upCount++;
                    clickEndSample = blockStartSample + iSamp;
                }
                else if (++downCount > minSep) {
                    clickStatus = ClickStatus.CLICK_OFF;
                    int clickDuration = (int) Math.min(clickEndSample + postSample - clickStartSample + 1, maxLength);
                    if (Integer.bitCount(clickTriggers) >= minTriggerChannels) {
                        ClickRow row = new ClickRow();
                        row.index = detections.size();
                        row.startSample = clickStartSample;
                        row.duration = clickDuration;
                        row.channelBitmap = channelBitmap;
                        row.triggerBitmap = clickTriggers;
                        row.signalExcessDb = maxSignalExcess;
                        row.timeMs = (long) (clickStartSample * 1000.0 / sampleRate);
                        detections.add(row);
                    }
                }
            }
            else if (clickStatus == ClickStatus.CLICK_ON) {
                if (overThreshold == 0) {
                    clickStatus = ClickStatus.CLICK_ENDING;
                }
                else {
                    upCount++;
                    clickTriggers |= overThreshold;
                    clickEndSample = blockStartSample + iSamp;
                    maxSignalExcess = Math.max(maxSignalExcess, maxSE);
                }
            }

            samplesProcessed++;
        }

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("index,startSample,duration,channelBitmap,triggerBitmap,signalExcessDb,timeMs");
            for (ClickRow row : detections) {
                writer.printf(Locale.ROOT, "%d,%d,%d,%d,%d,%.17g,%d%n",
                        row.index,
                        row.startSample,
                        row.duration,
                        row.channelBitmap,
                        row.triggerBitmap,
                        row.signalExcessDb,
                        row.timeMs);
            }
        }
    }

    private static List<Integer> channelList(int bitmap) {
        List<Integer> channels = new ArrayList<>();
        for (int i = 0; i < 32; i++) {
            if ((bitmap & (1 << i)) != 0) {
                channels.add(i);
            }
        }
        return channels;
    }

    private static int setBit(int bitmap, int bitNumber, boolean bitSet) {
        if (bitSet) {
            return bitmap | (1 << bitNumber);
        }
        return bitmap & ~(1 << bitNumber);
    }

    private static double syntheticSample(int channel, int sample) {
        double background = 0.01 * Math.sin(sample * 0.13 + channel * 0.31);
        if (sample >= 80 && sample <= 86) {
            double sign = (sample & 1) == 0 ? 1.0 : -1.0;
            double scale = channel == 0 ? 1.0 : 0.82;
            return background + sign * scale;
        }
        return background;
    }
}
