package org.pamguard.port.reference;

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public final class ConnectedRegionRejoinFixtureExporter {
    private static final int MIN_PIXELS = 2;
    private static final int MIN_LENGTH = 2;
    private static final int CONNECT_TYPE = 8;
    private static final int GRAD_LENGTH = 20;
    private static final int MAX_CROSS_LENGTH = 5;

    private static final class Slice {
        int sliceNumber;
        int[][] peakInfo;
    }

    private static final class Fragment {
        List<Slice> slices = new ArrayList<>();
        int totalPixels;
        int nJoinedStart;
        int nJoinedEnd;
    }

    private ConnectedRegionRejoinFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 1 || args.length > 2) {
            System.err.println("Usage: ConnectedRegionRejoinFixtureExporter <output.csv> [merge|split|cross]");
            System.exit(2);
        }
        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        String scenario = args.length == 2 ? args[1] : "merge";

        List<Slice> mother = motherForScenario(scenario);

        List<Fragment> fragments = fragment(mother);
        rejoin(fragments);
        clean(fragments);
        removeShortFragments(fragments);

        output.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("fragments");
            writer.println(fragmentInfo(fragments));
        }
    }

    private static List<Slice> motherForScenario(String scenario) {
        List<Slice> mother = new ArrayList<>();
        if ("merge".equals(scenario)) {
            mother.add(slice(30, peak(5, 6), peak(11, 11)));
            mother.add(slice(31, peak(5, 6), peak(11, 11)));
            mother.add(slice(32, peak(5, 11)));
            mother.add(slice(33, peak(5, 11)));
            return mother;
        }
        if ("split".equals(scenario)) {
            mother.add(slice(30, peak(5, 11)));
            mother.add(slice(31, peak(5, 11)));
            mother.add(slice(32, peak(5, 6), peak(11, 11)));
            mother.add(slice(33, peak(5, 6), peak(11, 11)));
            return mother;
        }
        if ("cross".equals(scenario)) {
            mother.add(slice(30, peak(4, 4), peak(12, 12)));
            mother.add(slice(31, peak(5, 5), peak(11, 11)));
            mother.add(slice(32, peak(5, 11)));
            mother.add(slice(33, peak(5, 5), peak(11, 11)));
            mother.add(slice(34, peak(4, 4), peak(12, 12)));
            return mother;
        }
        throw new IllegalArgumentException("unknown rejoin fixture scenario: " + scenario);
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
                    thisSliceRegions[i] = newFragment(thisSlice, i, 0);
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
                            close(fragments, lastSliceRegions[iP], nForwardLinks[iP]);
                        }
                    }
                    for (int iT = 0; iT < thisSlice.peakInfo.length; iT++) {
                        if (nBackLinks[iT] == 1 && nForwardLinks[backLinks[iT]] == 1) {
                            continue;
                        }
                        thisSliceRegions[iT] = newFragment(thisSlice, iT, nBackLinks[iT]);
                    }
                }
            }
            prevSlice = thisSlice;
            lastSliceRegions = thisSliceRegions;
        }
        Slice lastSlice = mother.get(mother.size() - 1);
        for (int i = 0; i < lastSlice.peakInfo.length; i++) {
            close(fragments, lastSliceRegions[i], 0);
        }
        return fragments;
    }

    private static Fragment newFragment(Slice slice, int peak, int nJoinedStart) {
        Fragment fragment = new Fragment();
        fragment.nJoinedStart = nJoinedStart;
        extend(fragment, slice, peak);
        return fragment;
    }

    private static void extend(Fragment fragment, Slice slice, int peak) {
        Slice out = slice(slice.sliceNumber, slice.peakInfo[peak].clone());
        out.peakInfo[0][3] = 0;
        fragment.slices.add(out);
        fragment.totalPixels += out.peakInfo[0][2] - out.peakInfo[0][0] + 1;
    }

    private static void close(List<Fragment> fragments, Fragment fragment, int nJoinedEnd) {
        fragment.nJoinedEnd = nJoinedEnd;
        fragments.add(fragment);
    }

    private static void rejoin(List<Fragment> fragments) {
        if (fragments.size() < 3) {
            return;
        }
        for (int i = 0; i < fragments.size(); i++) {
            Fragment fragment = fragments.get(i);
            if (isCross(fragment)) {
                if (jumpCross(fragments, i)) {
                    i--;
                }
            }
            else {
                if (isMerge(fragment)) {
                    Fragment merged = merge(fragments, i);
                    if (merged != null) {
                        fragment = merged;
                        i = fragments.indexOf(fragment);
                    }
                }
                if (isSplit(fragment)) {
                    branch(fragments, i);
                }
            }
        }
    }

    private static Fragment merge(List<Fragment> fragments, int mergeIndex) {
        Fragment mergeRegion = fragments.get(mergeIndex);
        int nInOut = mergeRegion.nJoinedStart;
        int[] ins = new int[nInOut];
        int nIns = 0;
        Slice mergeSlice = mergeRegion.slices.get(0);
        for (int i = 0; i < mergeIndex; i++) {
            if (matchPeak(fragments.get(i).slices.get(fragments.get(i).slices.size() - 1), mergeSlice)) {
                ins[nIns++] = i;
            }
            if (nIns == nInOut) {
                break;
            }
        }
        if (nIns == 0) {
            return null;
        }

        double mergeGradient = startGradient(mergeRegion, GRAD_LENGTH);
        Fragment test = fragments.get(ins[0]);
        double bestGrad = Math.abs(mergeGradient - endGradient(test, GRAD_LENGTH)) + shortPenalty(test.slices.size());
        int bestInd = 0;
        for (int i = 1; i < nIns; i++) {
            test = fragments.get(ins[i]);
            double newGrad = Math.abs(mergeGradient - endGradient(test, GRAD_LENGTH)) + shortPenalty(test.slices.size());
            if (newGrad < bestGrad) {
                bestGrad = newGrad;
                bestInd = i;
            }
        }
        test = fragments.get(ins[bestInd]);
        mergeWhistles(test, mergeRegion);
        fragments.remove(mergeIndex);
        return test;
    }

    private static boolean branch(List<Fragment> fragments, int branchIndex) {
        Fragment branchRegion = fragments.get(branchIndex);
        int nInOut = branchRegion.nJoinedEnd;
        int[] outs = new int[nInOut];
        int nOuts = 0;
        Slice branchSlice = branchRegion.slices.get(branchRegion.slices.size() - 1);
        for (int i = branchIndex + 1; i < fragments.size(); i++) {
            if (matchPeak(branchSlice, fragments.get(i).slices.get(0))) {
                outs[nOuts++] = i;
            }
            if (nOuts == nInOut) {
                break;
            }
        }
        if (nOuts == 0) {
            return false;
        }

        double branchGradient = endGradient(branchRegion, GRAD_LENGTH);
        Fragment test = fragments.get(outs[0]);
        double bestGrad = Math.abs(branchGradient - startGradient(test, GRAD_LENGTH)) + shortPenalty(test.slices.size());
        int bestInd = 0;
        for (int i = 1; i < nOuts; i++) {
            test = fragments.get(outs[i]);
            double newGrad = Math.abs(branchGradient - startGradient(test, GRAD_LENGTH)) + shortPenalty(test.slices.size());
            if (newGrad < bestGrad) {
                bestGrad = newGrad;
                bestInd = i;
            }
        }
        test = fragments.get(outs[bestInd]);
        mergeWhistles(branchRegion, test);
        fragments.remove(test);
        return true;
    }

    private static boolean jumpCross(List<Fragment> fragments, int crossIndex) {
        Fragment crossRegion = fragments.get(crossIndex);
        int nInOut = crossRegion.nJoinedEnd;
        int[] ins = new int[nInOut];
        int[] outs = new int[nInOut];
        int[] inFreq = new int[nInOut];
        int[] outFreq = new int[nInOut];
        int nIns = 0;
        int nOuts = 0;
        Slice crossSlice = crossRegion.slices.get(0);
        for (int i = 0; i < crossIndex; i++) {
            Fragment test = fragments.get(i);
            if (matchPeak(test.slices.get(test.slices.size() - 1), crossSlice)) {
                inFreq[nIns] = test.slices.get(test.slices.size() - 1).peakInfo[0][1];
                ins[nIns++] = i;
            }
            if (nIns == nInOut) {
                break;
            }
        }
        crossSlice = crossRegion.slices.get(crossRegion.slices.size() - 1);
        for (int i = crossIndex + 1; i < fragments.size(); i++) {
            Fragment test = fragments.get(i);
            if (matchPeak(crossSlice, test.slices.get(0))) {
                outFreq[nOuts] = test.slices.get(0).peakInfo[0][1];
                outs[nOuts++] = i;
            }
            if (nOuts == nInOut) {
                break;
            }
        }
        if (nOuts != nIns || nOuts != nInOut) {
            return false;
        }
        int[] sortedIns = sortedInds(inFreq);
        int[] sortedOuts = sortedInds(outFreq);
        int ind2 = nInOut;
        for (int i = 0; i < nInOut; i++) {
            ind2--;
            mergeWhistles(fragments.get(ins[sortedIns[i]]), fragments.get(outs[sortedOuts[ind2]]));
        }
        for (int i = nInOut - 1; i >= 0; i--) {
            fragments.remove(outs[i]);
        }
        fragments.remove(crossIndex);
        return true;
    }

    private static int[] sortedInds(int[] values) {
        Integer[] boxed = new Integer[values.length];
        for (int i = 0; i < values.length; i++) {
            boxed[i] = i;
        }
        java.util.Arrays.sort(boxed, (a, b) -> Integer.compare(values[a], values[b]));
        int[] out = new int[values.length];
        for (int i = 0; i < values.length; i++) {
            out[i] = boxed[i];
        }
        return out;
    }

    private static void mergeWhistles(Fragment first, Fragment second) {
        first.slices.addAll(second.slices);
        first.totalPixels += second.totalPixels;
        first.nJoinedEnd = second.nJoinedEnd;
    }

    private static void clean(List<Fragment> fragments) {
        for (Fragment fragment : fragments) {
            for (Slice slice : fragment.slices) {
                slice.peakInfo[0][3] = 0;
            }
        }
    }

    private static void removeShortFragments(List<Fragment> fragments) {
        if (fragments.size() < 2) {
            return;
        }
        for (int i = fragments.size() - 1; i >= 0; i--) {
            Fragment fragment = fragments.get(i);
            if (fragment.totalPixels < MIN_PIXELS || fragment.slices.size() < MIN_LENGTH) {
                fragments.remove(i);
            }
        }
    }

    private static boolean matchPeak(Slice first, Slice second) {
        return second.sliceNumber - first.sliceNumber == 1 && matchPeak(first.peakInfo[0], second.peakInfo[0]);
    }

    private static boolean matchPeak(int[] peak1, int[] peak2) {
        if (CONNECT_TYPE == 4) {
            return !(peak1[0] > peak2[2] || peak2[0] > peak1[2]);
        }
        return !(peak1[0] > peak2[2] + 1 || peak2[0] > peak1[2] + 1);
    }

    private static double startGradient(Fragment fragment, int nBins) {
        nBins = Math.min(nBins, fragment.slices.size());
        return ((double) (fragment.slices.get(nBins - 1).peakInfo[0][1] - fragment.slices.get(0).peakInfo[0][1])) /
            ((double) (nBins - 1));
    }

    private static double endGradient(Fragment fragment, int nBins) {
        int totalBins = fragment.slices.size();
        nBins = Math.min(nBins, totalBins);
        return ((double) (fragment.slices.get(totalBins - 1).peakInfo[0][1] - fragment.slices.get(totalBins - nBins).peakInfo[0][1])) /
            ((double) (nBins - 1));
    }

    private static double shortPenalty(int fragLength) {
        if (fragLength > 10) {
            return 0;
        }
        return (10 - fragLength) / 5;
    }

    private static boolean isCross(Fragment fragment) {
        return fragment.nJoinedEnd == fragment.nJoinedStart && fragment.nJoinedStart > 1 && fragment.slices.size() <= MAX_CROSS_LENGTH;
    }

    private static boolean isMerge(Fragment fragment) {
        return !isCross(fragment) && fragment.nJoinedStart > 1;
    }

    private static boolean isSplit(Fragment fragment) {
        return !isCross(fragment) && fragment.nJoinedEnd > 1;
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
