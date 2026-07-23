package org.pamguard.port.reference;

import PamUtils.complex.ComplexArray;
import Spectrogram.WindowFunction;
import clickDetector.ClickLength;
import fftManager.FastFFT;
import matchedTemplateClassifer.MTClassifier;
import matchedTemplateClassifer.MTProcess;
import matchedTemplateClassifer.MatchTemplate;
import matchedTemplateClassifer.MatchedTemplateResult;
import PamguardMVC.RawDataHolder;
import PamguardMVC.RawDataTransforms;

import java.io.File;
import java.io.PrintWriter;
import java.util.Locale;
import java.util.Random;

/**
 * Exports matched-template classifier fixtures by driving the REAL
 * MTClassifier.calcCorrelationMatch (with the real FastFFT/JTransforms
 * stack, template interpolation, and normalisation), the real
 * ClickLength.createLengthData (Hilbert envelope + smoothing peak search),
 * the real MTProcess.createRestrictedLenghtWave windowing, and the real
 * MTClassifier.normaliseWaveform. Only the per-click channel-aggregation
 * loop is transcribed from MTProcess.newClickData (15 lines, mirrored in
 * the C++ port), because MTProcess itself is display/annotation coupled.
 *
 * Deliberately pinned: the template FFT freezing at the FIRST click's FFT
 * length (the reference caches by sample rate only), the JTransforms packed
 * bin 0 multiplied as an ordinary complex value, odd- and even-length
 * non-power-of-two FFTs, template upsampling via PamInterp.interpWaveform,
 * the signed-maximum peak normalisation, and the NaN reject path from a
 * zeroed reject template.
 *
 * CSV is read by cpp-engine/tools/matched_template_fixture_check.cpp.
 */
public final class MatchedTemplateFixtureExporter {

    private static final class MtCase {
        String name;
        float sampleRate;
        int normType;
        boolean peakSearch;
        int peakSmoothing = 5;
        double lengthdB = 6;
        int restrictedBins = 128;
        int chanClass; // 0 = require all, 1 = require one
        MTClassifier[] classifiers;
        double[][][] clicks; // clicks[click][channel][sample]

        MtCase(String name, float sampleRate, int normType, boolean peakSearch, int chanClass) {
            this.name = name;
            this.sampleRate = sampleRate;
            this.normType = normType;
            this.peakSearch = peakSearch;
            this.chanClass = chanClass;
        }
    }

    /** Minimal RawDataHolder so the real ClickLength can run. */
    private static final class WaveHolder implements RawDataHolder {
        private final double[][] waveData;

        WaveHolder(double[][] waveData) {
            this.waveData = waveData;
        }

        @Override
        public double[][] getWaveData() {
            return waveData;
        }

        @Override
        public RawDataTransforms getDataTransforms() {
            return null;
        }
    }

    private MatchedTemplateFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: MatchedTemplateFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        int totalClicks = 0;
        int totalResults = 0;

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("# case,<name>,<sampleRate>,<normType>,<peakSearch>,<peakSmoothing>,<lengthdB>,<restrictedBins>,<chanClass>");
            writer.println("# template,<idx>,<thresholdToAccept>,<matchSr>,<rejectSr>");
            writer.println("# tmatch/treject,<idx>,<values...>");
            writer.println("# click,<nChan>  then wave,<chan>,<values...> rows");
            writer.println("# result,<classifierIdx>,<threshold>,<matchCorr>,<rejectCorr> (best across channels)");
            writer.println("# classified,<0|1>");

            for (MtCase mtCase : caseCatalogue()) {
                writer.printf(Locale.ROOT, "case,%s,%.17g,%d,%d,%d,%.17g,%d,%d%n",
                        mtCase.name, mtCase.sampleRate, mtCase.normType, mtCase.peakSearch ? 1 : 0,
                        mtCase.peakSmoothing, mtCase.lengthdB, mtCase.restrictedBins, mtCase.chanClass);
                for (int i = 0; i < mtCase.classifiers.length; i++) {
                    MTClassifier c = mtCase.classifiers[i];
                    writer.printf(Locale.ROOT, "template,%d,%.17g,%.17g,%.17g%n", i,
                            c.thresholdToAccept, (double) c.waveformMatch.sR, (double) c.waveformReject.sR);
                    writeValues(writer, "tmatch," + i, c.waveformMatch.waveform);
                    writeValues(writer, "treject," + i, c.waveformReject.waveform);
                }

                ClickLength clickLength = new ClickLength();
                FastFFT fft = new FastFFT();
                double[] window = WindowFunction.hann(mtCase.restrictedBins);

                for (double[][] click : mtCase.clicks) {
                    writer.printf(Locale.ROOT, "click,%d%n", click.length);
                    for (int chan = 0; chan < click.length; chan++) {
                        writeValues(writer, "wave," + chan, click[chan]);
                    }
                    totalClicks++;

                    // ---- Transcribed MTProcess.newClickData aggregation;
                    // every numeric step below is a real PAMGuard class. ----
                    int[][] lengthData = null;
                    if (mtCase.peakSearch) {
                        lengthData = clickLength.createLengthData(new WaveHolder(click), mtCase.sampleRate,
                                mtCase.lengthdB, mtCase.peakSmoothing, false, null);
                    }

                    MatchedTemplateResult[] best = new MatchedTemplateResult[mtCase.classifiers.length];
                    boolean classify = false;
                    int classifyCount = 0;

                    for (int chan = 0; chan < click.length; chan++) {
                        double[] wave;
                        if (mtCase.peakSearch) {
                            wave = MTProcess.createRestrictedLenghtWave(click[chan], lengthData[chan],
                                    mtCase.restrictedBins, window);
                        }
                        else {
                            wave = click[chan];
                        }
                        wave = MTClassifier.normaliseWaveform(wave, mtCase.normType);

                        ComplexArray clickFFT = fft.rfft(wave, wave.length);

                        boolean[] channelClassify = new boolean[mtCase.classifiers.length];
                        for (int j = 0; j < mtCase.classifiers.length; j++) {
                            MatchedTemplateResult result =
                                    mtCase.classifiers[j].calcCorrelationMatch(clickFFT, mtCase.sampleRate);
                            if (best[j] == null || result.threshold > best[j].threshold) {
                                best[j] = result;
                            }
                            channelClassify[j] = result.threshold > mtCase.classifiers[j].thresholdToAccept;
                        }
                        boolean any = false;
                        for (boolean value : channelClassify) {
                            any |= value;
                        }
                        if (mtCase.chanClass == 1 && any) {
                            classify = true;
                            break;
                        }
                        else if (any) {
                            classifyCount++;
                        }
                    }
                    if (classifyCount == click.length && mtCase.chanClass == 0) {
                        classify = true;
                    }

                    for (int j = 0; j < mtCase.classifiers.length; j++) {
                        writer.printf(Locale.ROOT, "result,%d,%.17g,%.17g,%.17g%n", j,
                                best[j].threshold, best[j].matchCorr, best[j].rejectCorr);
                        totalResults++;
                    }
                    writer.printf(Locale.ROOT, "classified,%d%n", classify ? 1 : 0);
                }
            }
        }
        System.out.println("Matched-template fixture: clicks=" + totalClicks + " results=" + totalResults);
    }

    private static void writeValues(PrintWriter writer, String prefix, double[] values) {
        StringBuilder row = new StringBuilder(prefix);
        for (double value : values) {
            row.append(String.format(Locale.ROOT, ",%.17g", value));
        }
        writer.println(row);
    }

    private static double[] burst(Random random, int length, double freqHz, double srHz,
                                  double decay, double amp, double noise) {
        double[] wave = new double[length];
        for (int i = 0; i < length; i++) {
            wave[i] = amp * Math.exp(-decay * i) * Math.sin(2 * Math.PI * freqHz * i / srHz)
                    + noise * (random.nextDouble() * 2 - 1);
        }
        return wave;
    }

    /** A click: noise floor with a burst embedded at an offset. */
    private static double[] clickWave(Random random, int length, int offset, double[] burst, double noise) {
        double[] wave = new double[length];
        for (int i = 0; i < length; i++) {
            wave[i] = noise * (random.nextDouble() * 2 - 1);
        }
        for (int i = 0; i < burst.length && offset + i < length; i++) {
            wave[offset + i] += burst[i];
        }
        return wave;
    }

    private static MTClassifier classifier(double threshold, int normType,
                                           MatchTemplate match, MatchTemplate reject) {
        MTClassifier c = new MTClassifier();
        c.thresholdToAccept = threshold;
        c.normalisation = normType;
        c.waveformMatch = match;
        c.waveformReject = reject;
        return c;
    }

    private static MtCase[] caseCatalogue() {
        Random random = new Random(20260725L);

        double[] match48 = burst(random, 96, 8000, 48000, 0.04, 1.0, 0.0);
        double[] reject48 = burst(random, 96, 3000, 48000, 0.03, 1.0, 0.0);
        double[] match24 = burst(random, 60, 5000, 24000, 0.05, 1.0, 0.0);

        // 1: RMS norm, peak search, one classifier; the second click is
        // noise-only, the third carries the reject shape.
        MtCase rms = new MtCase("rms-peak-search", 48000, 1, true, 0);
        rms.restrictedBins = 128;
        rms.classifiers = new MTClassifier[]{
                classifier(0.05, 1, new MatchTemplate("m8k", match48, 48000),
                        new MatchTemplate("r3k", reject48, 48000))};
        rms.clicks = new double[][][]{
                {clickWave(random, 400, 150, match48, 0.02)},
                {clickWave(random, 400, 150, new double[0], 0.02)},
                {clickWave(random, 400, 150, reject48, 0.02)},
        };

        // 2: no peak search, PEAK norm, non-power-of-two lengths — an even
        // 300, an odd 301, and a shorter 260 against the then-frozen
        // 300-length template FFT.
        MtCase raw = new MtCase("no-peak-arbitrary-len", 48000, 0, false, 0);
        raw.classifiers = new MTClassifier[]{
                classifier(0.05, 0, new MatchTemplate("m8k", match48, 48000),
                        new MatchTemplate("r3k", reject48, 48000))};
        raw.clicks = new double[][][]{
                {clickWave(random, 300, 100, match48, 0.02)},
                {clickWave(random, 301, 90, match48, 0.02)},
                {clickWave(random, 260, 80, reject48, 0.02)},
        };

        // 3: the match template lives at 24 kHz and is upsampled to the
        // 48 kHz session by the ported PamInterp.interpWaveform.
        MtCase upsample = new MtCase("upsample-template", 48000, 1, true, 0);
        upsample.restrictedBins = 256;
        upsample.classifiers = new MTClassifier[]{
                classifier(0.05, 1, new MatchTemplate("m5k24", match24, 24000),
                        new MatchTemplate("r3k", reject48, 48000))};
        upsample.clicks = new double[][][]{
                {clickWave(random, 512, 200, burst(random, 120, 5000, 48000, 0.025, 1.0, 0.0), 0.02)},
                {clickWave(random, 512, 200, new double[0], 0.02)},
        };

        // 4/5: two channels, two classifiers (the second with a zeroed
        // reject template driving the NaN reject path), aggregated
        // require-one vs require-all.
        double[][] chanA = {clickWave(random, 400, 120, match48, 0.02), clickWave(random, 400, 140, match48, 0.02)};
        double[][] chanB = {clickWave(random, 400, 120, match48, 0.02), clickWave(random, 400, 140, new double[0], 0.02)};
        MTClassifier[] two = new MTClassifier[]{
                classifier(0.05, 1, new MatchTemplate("m8k", match48, 48000),
                        new MatchTemplate("r3k", reject48, 48000)),
                classifier(0.5, 1, new MatchTemplate("m8k", match48, 48000),
                        new MatchTemplate("none", new double[64], 48000))};
        MTClassifier[] twoCopy = new MTClassifier[]{
                classifier(0.05, 1, new MatchTemplate("m8k", match48.clone(), 48000),
                        new MatchTemplate("r3k", reject48.clone(), 48000)),
                classifier(0.5, 1, new MatchTemplate("m8k", match48.clone(), 48000),
                        new MatchTemplate("none", new double[64], 48000))};
        MtCase requireOne = new MtCase("two-chan-require-one", 48000, 1, true, 1);
        requireOne.classifiers = two;
        requireOne.clicks = new double[][][]{chanA, chanB};
        MtCase requireAll = new MtCase("two-chan-require-all", 48000, 1, true, 0);
        requireAll.classifiers = twoCopy;
        requireAll.clicks = new double[][][]{chanA, chanB};

        // 6: no normalisation at all.
        MtCase none = new MtCase("none-norm", 48000, 2, false, 0);
        none.classifiers = new MTClassifier[]{
                classifier(0.05, 2, new MatchTemplate("m8k", match48, 48000),
                        new MatchTemplate("r3k", reject48, 48000))};
        none.clicks = new double[][][]{
                {clickWave(random, 256, 80, match48, 0.02)},
        };

        return new MtCase[]{rms, raw, upsample, requireOne, requireAll, none};
    }
}
