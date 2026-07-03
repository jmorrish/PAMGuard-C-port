package org.pamguard.port.reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

public final class ConnectedRegionFixtureExporter {
    private static final class Slice {
        int sliceNumber;
        long startSample;
        long timeMs;
        boolean[] pixels;
        double[] magnitude;
        int[][] peakInfo;
        int peakBin;
    }

    private static final class Region {
        int channel;
        int regionNumber;
        int firstSlice;
        int sliceHeight;
        int totalPixels;
        boolean growing;
        List<Slice> slices = new ArrayList<>();
        int[] freqRange;
        int[] peakFreqsBins;
        int[] timesBins;

        Region(int channel, int firstSlice, int regionNumber, int sliceHeight) {
            this.channel = channel;
            this.firstSlice = firstSlice;
            this.regionNumber = regionNumber;
            this.sliceHeight = sliceHeight;
        }

        void addPixel(int sliceNumber, int row, long startSample, long timeMs, double[] magnitude) {
            Slice slice = null;
            for (int i = slices.size() - 1; i >= 0; i--) {
                if (slices.get(i).sliceNumber == sliceNumber) {
                    slice = slices.get(i);
                    break;
                }
            }
            if (slice == null) {
                slice = new Slice();
                slice.sliceNumber = sliceNumber;
                slice.startSample = startSample;
                slice.timeMs = timeMs;
                slice.pixels = new boolean[sliceHeight];
                slice.magnitude = magnitude.clone();
                slices.add(slice);
            }
            if (!slice.pixels[row]) {
                slice.pixels[row] = true;
                totalPixels++;
            }
            growing = true;
        }

        void merge(Region other) {
            for (Slice slice : other.slices) {
                for (int i = 0; i < slice.pixels.length; i++) {
                    if (slice.pixels[i]) {
                        addPixel(slice.sliceNumber, i, slice.startSample, slice.timeMs, slice.magnitude);
                    }
                }
            }
        }

        void condense() {
            freqRange = new int[] {Integer.MAX_VALUE, 0};
            timesBins = new int[slices.size()];
            peakFreqsBins = new int[slices.size()];
            Slice previous = null;
            for (int i = 0; i < slices.size(); i++) {
                Slice slice = slices.get(i);
                condenseSlice(slice, previous);
                freqRange[0] = Math.min(freqRange[0], slice.peakInfo[0][0]);
                freqRange[1] = Math.max(freqRange[1], slice.peakInfo[slice.peakInfo.length - 1][2]);
                timesBins[i] = slice.sliceNumber;
                peakFreqsBins[i] = slice.peakBin;
                previous = slice;
            }
        }

        private void condenseSlice(Slice slice, Slice previous) {
            int count = 0;
            if (slice.pixels[0]) {
                count++;
            }
            for (int i = 1; i < slice.pixels.length; i++) {
                if (slice.pixels[i] && !slice.pixels[i - 1]) {
                    count++;
                }
            }

            slice.peakInfo = new int[count][4];
            int peakIndex = 0;
            boolean on = false;
            int maxIndex = 0;
            double maxValue = 0.0;
            double peakMaxValue = 0.0;
            for (int i = 0; i < slice.pixels.length; i++) {
                if (!slice.pixels[i]) {
                    continue;
                }
                double value = slice.magnitude[i];
                if (value > peakMaxValue) {
                    peakMaxValue = value;
                    slice.peakBin = i;
                }
                if (!on) {
                    slice.peakInfo[peakIndex][0] = i;
                    maxIndex = i;
                    maxValue = value;
                    on = true;
                }
                else if (value > maxValue) {
                    maxValue = value;
                    maxIndex = i;
                }
                if (i == slice.pixels.length - 1 || !slice.pixels[i + 1]) {
                    slice.peakInfo[peakIndex][1] = maxIndex;
                    slice.peakInfo[peakIndex][2] = i;
                    slice.peakInfo[peakIndex][3] = overlappingPeak(slice.peakInfo[peakIndex], previous);
                    peakIndex++;
                    on = false;
                }
            }
        }

        private int overlappingPeak(int[] peak, Slice previous) {
            if (previous == null || previous.peakInfo == null) {
                return -1;
            }
            for (int i = 0; i < previous.peakInfo.length; i++) {
                int[] other = previous.peakInfo[i];
                if (other[0] > peak[2] || other[2] < peak[0]) {
                    continue;
                }
                return i;
            }
            return -1;
        }
    }

    private ConnectedRegionFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: ConnectedRegionFixtureExporter <output.csv>");
            System.exit(2);
        }
        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        Region region = new Region(0, 10, 0, 16);
        addSlice(region, 10, 13000, new int[] {5, 6});
        addSlice(region, 11, 13064, new int[] {6, 7});
        addSlice(region, 12, 13128, new int[] {7});
        region.condense();

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("regionNumber,channel,firstSlice,numSlices,totalPixels,freqLow,freqHigh,times,peakBins,peaks");
            writer.printf(Locale.ROOT, "%d,%d,%d,%d,%d,%d,%d,%s,%s,%s%n",
                    region.regionNumber,
                    region.channel,
                    region.firstSlice,
                    region.slices.size(),
                    region.totalPixels,
                    region.freqRange[0],
                    region.freqRange[1],
                    join(region.timesBins),
                    join(region.peakFreqsBins),
                    peakInfo(region));
        }
    }

    private static void addSlice(Region region, int sliceNumber, long startSample, int[] bins) {
        double[] magnitude = new double[16];
        for (int i = 0; i < magnitude.length; i++) {
            magnitude[i] = 1.0 + i;
        }
        for (int bin : bins) {
            region.addPixel(sliceNumber, bin, startSample, 1000 + sliceNumber, magnitude);
        }
    }

    private static String join(int[] values) {
        StringBuilder builder = new StringBuilder();
        for (int i = 0; i < values.length; i++) {
            if (i > 0) {
                builder.append('|');
            }
            builder.append(values[i]);
        }
        return builder.toString();
    }

    private static String peakInfo(Region region) {
        List<String> parts = new ArrayList<>();
        for (Slice slice : region.slices) {
            for (int[] peak : slice.peakInfo) {
                parts.add(slice.sliceNumber + ":" + peak[0] + "-" + peak[1] + "-" + peak[2] + "-" + peak[3]);
            }
        }
        return String.join("|", parts);
    }
}
