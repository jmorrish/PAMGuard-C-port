package org.pamguard.port.reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public final class ConnectedRegionFragmentFixtureExporter {
    private static final int MIN_PIXELS = 2;
    private static final int MIN_LENGTH = 2;
    private static final int CONNECT_TYPE = 8;

    private static final class Slice {
        int sliceNumber;
        int[][] peakInfo;
    }

    private static final class Fragment {
        List<Slice> slices = new ArrayList<>();
        int totalPixels;
    }

    private ConnectedRegionFragmentFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: ConnectedRegionFragmentFixtureExporter <output.csv>");
            System.exit(2);
        }
        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);

        List<Slice> mother = new ArrayList<>();
        mother.add(slice(30, peak(5, 11)));
        mother.add(slice(31, peak(5, 6), peak(11, 11)));
        mother.add(slice(32, peak(5, 6), peak(11, 11)));

        List<Fragment> fragments = fragment(mother);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("fragments");
            writer.println(fragmentInfo(fragments));
        }
    }

    private static Slice slice(int sliceNumber, int[]... peaks) {
        Slice slice = new Slice();
        slice.sliceNumber = sliceNumber;
        slice.peakInfo = peaks;
        return slice;
    }

    private static int[] peak(int low, int high) {
        return new int[]{low, high, high, -1};
    }

    private static List<Fragment> fragment(List<Slice> mother) {
        int maxPeaks = 2;
        List<Fragment> fragments = new ArrayList<>();
        Fragment[] thisSliceRegions = new Fragment[maxPeaks];
        Fragment[] lastSliceRegions = new Fragment[maxPeaks];
        Slice prevSlice = null;

        for (Slice thisSlice : mother) {
            thisSliceRegions = new Fragment[maxPeaks];
            if (prevSlice == null) {
                for (int i = 0; i < thisSlice.peakInfo.length; i++) {
                    thisSliceRegions[i] = newFragment(thisSlice, i);
                }
            }
            else {
                boolean matchAll = false;
                if (prevSlice.peakInfo.length == thisSlice.peakInfo.length) {
                    matchAll = true;
                    for (int i = 0; i < prevSlice.peakInfo.length; i++) {
                        if (!matchPeak(prevSlice.peakInfo[i], thisSlice.peakInfo[i])) {
                            matchAll = false;
                        }
                    }
                    if (matchAll) {
                        for (int i = 0; i < prevSlice.peakInfo.length; i++) {
                            extend(lastSliceRegions[i], thisSlice, i);
                            thisSliceRegions[i] = lastSliceRegions[i];
                        }
                    }
                }
                if (!matchAll) {
                    int[] forwardLinks = new int[maxPeaks];
                    int[] backLinks = new int[maxPeaks];
                    int[] nForwardLinks = new int[maxPeaks];
                    int[] nBackLinks = new int[maxPeaks];
                    for (int i = 0; i < maxPeaks; i++) {
                        forwardLinks[i] = -1;
                        backLinks[i] = -1;
                    }
                    for (int iP = 0; iP < prevSlice.peakInfo.length; iP++) {
                        for (int iT = 0; iT < thisSlice.peakInfo.length; iT++) {
                            if (matchPeak(prevSlice.peakInfo[iP], thisSlice.peakInfo[iT])) {
                                forwardLinks[iP] = iT;
                                nForwardLinks[iP]++;
                                backLinks[iT] = iP;
                                nBackLinks[iT]++;
                            }
                        }
                    }
                    for (int iP = 0; iP < prevSlice.peakInfo.length; iP++) {
                        if (nForwardLinks[iP] == 1 && nBackLinks[forwardLinks[iP]] == 1) {
                            extend(lastSliceRegions[iP], thisSlice, forwardLinks[iP]);
                            thisSliceRegions[forwardLinks[iP]] = lastSliceRegions[iP];
                        }
                        else {
                            close(fragments, lastSliceRegions[iP]);
                        }
                    }
                    for (int iT = 0; iT < thisSlice.peakInfo.length; iT++) {
                        if (nBackLinks[iT] == 1 && nForwardLinks[backLinks[iT]] == 1) {
                            continue;
                        }
                        thisSliceRegions[iT] = newFragment(thisSlice, iT);
                    }
                }
            }
            prevSlice = thisSlice;
            lastSliceRegions = thisSliceRegions;
        }
        Slice lastSlice = mother.get(mother.size() - 1);
        for (int i = 0; i < lastSlice.peakInfo.length; i++) {
            close(fragments, lastSliceRegions[i]);
        }
        return fragments;
    }

    private static Fragment newFragment(Slice slice, int peak) {
        Fragment fragment = new Fragment();
        extend(fragment, slice, peak);
        return fragment;
    }

    private static void extend(Fragment fragment, Slice slice, int peak) {
        Slice out = slice(slice.sliceNumber, slice.peakInfo[peak].clone());
        out.peakInfo[0][3] = 0;
        fragment.slices.add(out);
        fragment.totalPixels += out.peakInfo[0][2] - out.peakInfo[0][0] + 1;
    }

    private static void close(List<Fragment> fragments, Fragment fragment) {
        if (fragment.totalPixels < MIN_PIXELS || fragment.slices.size() < MIN_LENGTH) {
            return;
        }
        fragments.add(fragment);
    }

    private static boolean matchPeak(int[] peak1, int[] peak2) {
        if (CONNECT_TYPE == 4) {
            return !(peak1[0] > peak2[2] || peak2[0] > peak1[2]);
        }
        return !(peak1[0] > peak2[2] + 1 || peak2[0] > peak1[2] + 1);
    }

    private static String fragmentInfo(List<Fragment> fragments) {
        List<String> parts = new ArrayList<>();
        for (int i = 0; i < fragments.size(); i++) {
            Fragment fragment = fragments.get(i);
            List<String> peaks = new ArrayList<>();
            for (Slice slice : fragment.slices) {
                int[] peak = slice.peakInfo[0];
                peaks.add(slice.sliceNumber + ":" + peak[0] + "-" + peak[1] + "-" + peak[2] + "-" + peak[3]);
            }
            parts.add(i + "[" + fragment.totalPixels + "]=" + String.join("|", peaks));
        }
        return String.join(";", parts);
    }
}
