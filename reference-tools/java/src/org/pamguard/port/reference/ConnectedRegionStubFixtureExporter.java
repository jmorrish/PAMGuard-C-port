package org.pamguard.port.reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public final class ConnectedRegionStubFixtureExporter {
    private static final int MIN_PIXELS = 6;
    private static final int CONNECT_TYPE = 8;

    private static final class Slice {
        int sliceNumber;
        boolean[] pixels = new boolean[16];
        double[] magnitude = new double[16];
        int[][] peakInfo;
        int nPeaks;
    }

    private ConnectedRegionStubFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: ConnectedRegionStubFixtureExporter <output.csv>");
            System.exit(2);
        }
        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        List<Slice> slices = new ArrayList<>();
        slices.add(slice(20, 5, 6, 11));
        slices.add(slice(21, 5, 6, 12));
        slices.add(slice(22, 5, 6));
        slices.add(slice(23, 5, 6));

        Slice previous = null;
        for (Slice slice : slices) {
            condenseSlice(slice, previous);
            previous = slice;
        }
        removeStubs(slices);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("peaks");
            writer.println(peakInfo(slices));
        }
    }

    private static Slice slice(int sliceNumber, int... bins) {
        Slice slice = new Slice();
        slice.sliceNumber = sliceNumber;
        for (int i = 0; i < slice.magnitude.length; i++) {
            slice.magnitude[i] = 1.0 + i;
        }
        for (int bin : bins) {
            slice.pixels[bin] = true;
        }
        return slice;
    }

    private static void condenseSlice(Slice slice, Slice previous) {
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
        slice.nPeaks = count;
        int peakIndex = 0;
        boolean on = false;
        int maxIndex = 0;
        double maxValue = 0.0;
        for (int i = 0; i < slice.pixels.length; i++) {
            if (!slice.pixels[i]) {
                continue;
            }
            double value = slice.magnitude[i];
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

    private static int overlappingPeak(int[] peak, Slice previous) {
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

    private static void removeStubs(List<Slice> sliceData) {
        int nSlice = sliceData.size();
        if (nSlice < 2) {
            return;
        }
        int diagGap = CONNECT_TYPE == 4 ? 0 : 1;

        for (int i = 0; i < nSlice; i++) {
            Slice slice = sliceData.get(i);
            if (slice.nPeaks > 1) {
                int[] sizes = new int[slice.nPeaks];
                for (int p = 0; p < slice.nPeaks; p++) {
                    sizes[p] = slice.peakInfo[p][2] - slice.peakInfo[p][0] + 1;
                    sizes[p] = searchStubSize(sliceData, i, p, 1, diagGap, sizes[p]);
                }
                removeSmallStubs(slice, sizes);
            }
        }

        for (int i = nSlice - 1; i >= 0; i--) {
            Slice slice = sliceData.get(i);
            if (slice.nPeaks > 1) {
                int[] sizes = new int[slice.nPeaks];
                for (int p = 0; p < slice.nPeaks; p++) {
                    sizes[p] = slice.peakInfo[p][2] - slice.peakInfo[p][0] + 1;
                    sizes[p] = searchStubSize(sliceData, i, p, -1, diagGap, sizes[p]);
                }
                removeSmallStubs(slice, sizes);
            }
        }
    }

    private static int searchStubSize(List<Slice> sliceData, int currentSlice, int peakInd, int searchDir, int diagGap, int currentSize) {
        int nSlice = sliceData.size();
        int nextSliceInd = currentSlice + searchDir;
        if (nextSliceInd < 0 || nextSliceInd >= nSlice - 1 || currentSize > MIN_PIXELS) {
            return currentSize;
        }
        Slice nextSlice = sliceData.get(nextSliceInd);
        boolean endSlice = nextSliceInd <= 0 || nextSliceInd >= nSlice - 1;
        int[] thisPeak = sliceData.get(currentSlice).peakInfo[peakInd];
        for (int p = 0; p < nextSlice.nPeaks; p++) {
            int[] nextPeak = nextSlice.peakInfo[p];
            if (nextPeak[0] - thisPeak[2] > diagGap || thisPeak[0] - nextPeak[2] > diagGap) {
                continue;
            }
            currentSize += nextPeak[2] - nextPeak[0] + 1;
            if (!endSlice) {
                currentSize += searchStubSize(sliceData, nextSliceInd, p, searchDir, diagGap, currentSize);
            }
        }
        return currentSize;
    }

    private static void removeSmallStubs(Slice slice, int[] sizes) {
        int biggestSize = sizes[0];
        for (int i = 1; i < sizes.length; i++) {
            if (sizes[i] > biggestSize) {
                biggestSize = sizes[i];
            }
        }
        int nKeep = 0;
        boolean[] keep = new boolean[sizes.length];
        for (int i = 0; i < sizes.length; i++) {
            if (keep[i] = (sizes[i] >= MIN_PIXELS || sizes[i] == biggestSize)) {
                nKeep++;
            }
        }
        int[][] newPeakData = new int[nKeep][];
        int ik = 0;
        for (int i = 0; i < sizes.length; i++) {
            if (keep[i]) {
                newPeakData[ik++] = slice.peakInfo[i];
            }
        }
        slice.peakInfo = newPeakData;
        slice.nPeaks = nKeep;
    }

    private static String peakInfo(List<Slice> slices) {
        List<String> parts = new ArrayList<>();
        for (Slice slice : slices) {
            for (int[] peak : slice.peakInfo) {
                parts.add(slice.sliceNumber + ":" + peak[0] + "-" + peak[1] + "-" + peak[2] + "-" + peak[3]);
            }
        }
        return String.join("|", parts);
    }
}
