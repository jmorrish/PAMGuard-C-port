package org.pamguard.port.reference;

import PamUtils.PamUtils;
import PamguardMVC.RawDataTransforms;
import clickDetector.ClickControl;
import clickDetector.ClickDetection;
import clickDetector.ClickDetector;
import clickDetector.ClickDetector.ChannelGroupDetector;
import clickDetector.ClickClassifiers.ClickIdInformation;
import clickDetector.ClickClassifiers.basicSweep.SweepClassifier;
import clickDetector.ClickClassifiers.basicSweep.SweepClassifierParameters;
import clickDetector.ClickClassifiers.basicSweep.SweepClassifierSet;
import clickDetector.ClickClassifiers.basicSweep.SweepClassifierWorker;
import fftFilter.FFTFilter;
import fftFilter.FFTFilterParams;
import signal.Hilbert;
import sun.misc.Unsafe;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.Arrays;
import java.util.Locale;

/**
 * Executes the real 2.02.18e SweepClassifierWorker against constructor-free
 * test doubles. Only the PAMGuard controller lifecycle is bypassed; waveform,
 * Hilbert, FFT filter, spectrum and classifier code are the production classes.
 */
public final class SweepClassifierFixtureExporter {
    private static final float SAMPLE_RATE = 48000.0f;

    private SweepClassifierFixtureExporter() {
    }

    private static final class StubDetector extends ClickDetector {
        StubDetector() { super((ClickControl) null); }
        @Override public float getSampleRate() { return SAMPLE_RATE; }
    }

    private static final class StubControl extends ClickControl {
        StubDetector detector;
        StubControl() { super("fixture"); }
        @Override public ClickDetector getClickDetector() { return detector; }
    }

    private static final class StubClick extends ClickDetection {
        double[][] wave;
        double[] amplitude;

        StubClick() {
            super(1, 0, 1, (ClickDetector) null, (ChannelGroupDetector) null, 1);
        }

        @Override public synchronized double[][] getWaveData() { return wave; }
        @Override public synchronized double[] getWaveData(int channel) { return wave[channel]; }
        @Override public double[][] getWaveData(boolean filtered, FFTFilterParams params) {
            if (!filtered) return wave;
            double[][] result = new double[wave.length][];
            for (int i = 0; i < wave.length; i++) result[i] = filter(wave[i], params);
            return result;
        }
        @Override public double[] getWaveData(int channel, boolean filtered, FFTFilterParams params) {
            return filtered ? filter(wave[channel], params) : wave[channel];
        }
        @Override public synchronized double[] getAnalyticWaveform(
                int channel, boolean filtered, FFTFilterParams params) {
            return new Hilbert().getHilbert(getWaveData(channel, filtered, params));
        }
        @Override public double[] getPowerSpectrum(int channel) {
            int fftLength = PamUtils.getMinFftLength(wave[channel].length);
            return RawDataTransforms.getComplexSpectrumHann(wave[channel], fftLength).magsq();
        }
        @Override public int getChannelBitmap() { return (1 << wave.length) - 1; }
        @Override public int getNChan() { return wave.length; }
        @Override public double[] getDelaysInSamples() {
            return new double[wave.length * (wave.length - 1) / 2];
        }
        @Override public double getAmplitude(int channel) { return amplitude[channel]; }
        @Override public double linAmplitudeToDB(double value) { return 20.0 * Math.log10(value); }

        private static double[] filter(double[] input, FFTFilterParams params) {
            double[] output = new double[input.length];
            new FFTFilter(params, SAMPLE_RATE).runFilter(input, output);
            return output;
        }
    }

    private static final class Case {
        final String name;
        final boolean checkAll;
        final SweepClassifierSet[] sets;
        Case(String name, boolean checkAll, SweepClassifierSet... sets) {
            this.name = name;
            this.checkAll = checkAll;
            this.sets = sets;
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            System.err.println("Usage: SweepClassifierFixtureExporter <classification.csv> <defaults.csv>");
            System.exit(2);
        }
        Locale.setDefault(Locale.ROOT);
        StubDetector detector = allocate(StubDetector.class);
        StubControl control = allocate(StubControl.class);
        control.detector = detector;
        StubClick click = allocate(StubClick.class);
        click.wave = waveform();
        click.amplitude = amplitudes(click.wave);

        File classifications = new File(args[0]);
        classifications.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(classifications)) {
            writer.println("case,clickType,discard,classifiersPassed");
            for (Case testCase : cases()) {
                SweepClassifierParameters parameters = new SweepClassifierParameters();
                parameters.checkAllClassifiers = testCase.checkAll;
                for (SweepClassifierSet set : testCase.sets) parameters.addSet(set);
                SweepClassifier classifier = allocate(SweepClassifier.class);
                classifier.setSweepClassifierParameters(parameters);
                ClickIdInformation result =
                        new SweepClassifierWorker(control, classifier).identify(click);
                String passed = result.classifiersPassed == null ? "" :
                        Arrays.toString(result.classifiersPassed)
                                .replace("[", "").replace("]", "").replace(", ", ";");
                writer.printf(Locale.ROOT, "%s,%d,%s,%s%n", testCase.name,
                        result.clickType, Boolean.toString(result.discard), passed);
            }
        }
        writeDefaults(new File(args[1]));
    }

    private static Case[] cases() {
        SweepClassifierSet disabled = pass(40);
        disabled.setEnable(false);
        SweepClassifierSet allFirst = pass(41);
        allFirst.setDiscard(false);
        SweepClassifierSet allSecond = pass(42);
        allSecond.setDiscard(true);
        return new Case[]{
                new Case("all-pass", false, failLength(7), pass(42)),
                new Case("length-fail", false, failLength(7)),
                new Case("amplitude-fail", false, failAmplitude(8)),
                new Case("energy-fail", false, failEnergy(9)),
                new Case("peak-fail", false, failPeak(10)),
                new Case("width-fail", false, failWidth(11)),
                new Case("mean-fail", false, failMean(12)),
                new Case("zero-crossing-fail", false, failZeroCrossing(13)),
                new Case("sweep-fail", false, failSweep(14)),
                new Case("min-correlation-fail", false, failMinCorrelation(15)),
                new Case("peak-correlation-fail", false, failPeakCorrelation(16)),
                new Case("require-one-pass", false, amplitudeChoice(17, SweepClassifierSet.CHANNELS_REQUIRE_ONE)),
                new Case("require-all-fail", false, amplitudeChoice(18, SweepClassifierSet.CHANNELS_REQUIRE_ALL)),
                new Case("use-means-pass", false, amplitudeChoice(19, SweepClassifierSet.CHANNELS_USE_MEANS)),
                new Case("fft-filter-pass", false, filteredPass(20)),
                new Case("fft-filter-amplitude-unfiltered-pass", false, filteredAmplitudePass(21)),
                new Case("fft-filter-unrestricted-spectrum-pass", false, filteredUnrestrictedPeakPass(22)),
                new Case("disabled-skipped", false, disabled, pass(43)),
                new Case("check-all", true, allFirst, allSecond),
        };
    }

    private static SweepClassifierSet base(int species) {
        SweepClassifierSet set = new SweepClassifierSet();
        set.setName("fixture-" + species);
        set.setSpeciesCode(species);
        set.setDiscard(false);
        set.enableLength = false;
        set.restrictLength = true;
        set.restrictedBins = 128;
        set.lengthSmoothing = 5;
        set.lengthdB = 6;
        set.enableEnergyBands = false;
        set.testAmplitude = false;
        set.enablePeak = false;
        set.enableWidth = false;
        set.enableMean = false;
        set.enableZeroCrossings = false;
        set.enableSweep = false;
        set.enableMinXCrossCorr = false;
        set.enablePeakXCorr = false;
        set.enableBearingLims = true; // no localisation must pass
        return set;
    }

    private static SweepClassifierSet pass(int species) {
        SweepClassifierSet set = base(species);
        set.setDiscard(true);
        set.enableLength = true;
        set.minLength = 0;
        set.maxLength = 100;
        set.testAmplitude = true;
        set.amplitudeRange = new double[]{-1000, 1000};
        set.enableEnergyBands = true;
        set.testEnergyBand = new double[]{7000, 12000};
        set.controlEnergyBand[0] = new double[]{1000, 4000};
        set.controlEnergyBand[1] = new double[]{18000, 22000};
        set.energyThresholds = new double[]{-1000, -1000};
        set.enablePeak = true;
        set.enableWidth = true;
        set.enableMean = true;
        set.peakSearchRange = new double[]{1000, 23000};
        set.peakRange = new double[]{0, 24000};
        set.peakWidthRange = new double[]{0, 24000};
        set.meanRange = new double[]{0, 24000};
        set.enableZeroCrossings = true;
        set.nCrossings = new int[]{0, 1000};
        set.enableSweep = true;
        set.zcSweep = new double[]{-1e9, 1e9};
        set.enableMinXCrossCorr = true;
        set.minCorr = -1e300;
        set.enablePeakXCorr = true;
        set.corrFactor = -1;
        return set;
    }

    private static SweepClassifierSet failLength(int species) {
        SweepClassifierSet set = base(species);
        set.enableLength = true;
        set.minLength = 100;
        set.maxLength = 200;
        return set;
    }
    private static SweepClassifierSet failAmplitude(int species) {
        SweepClassifierSet set = base(species);
        set.testAmplitude = true;
        set.amplitudeRange = new double[]{100, 200};
        return set;
    }
    private static SweepClassifierSet failEnergy(int species) {
        SweepClassifierSet set = base(species);
        set.enableEnergyBands = true;
        set.testEnergyBand = new double[]{7000, 12000};
        set.controlEnergyBand[0] = new double[]{1000, 4000};
        set.controlEnergyBand[1] = new double[]{18000, 22000};
        set.energyThresholds = new double[]{1000, 1000};
        return set;
    }
    private static SweepClassifierSet failPeak(int species) {
        SweepClassifierSet set = base(species);
        set.enablePeak = true;
        set.peakSearchRange = new double[]{1000, 23000};
        set.peakRange = new double[]{1, 100};
        return set;
    }
    private static SweepClassifierSet failWidth(int species) {
        SweepClassifierSet set = base(species);
        set.enableWidth = true;
        set.peakSearchRange = new double[]{1000, 23000};
        set.peakWidthRange = new double[]{20000, 24000};
        return set;
    }
    private static SweepClassifierSet failMean(int species) {
        SweepClassifierSet set = base(species);
        set.enableMean = true;
        set.peakSearchRange = new double[]{1000, 23000};
        set.meanRange = new double[]{1, 100};
        return set;
    }
    private static SweepClassifierSet failZeroCrossing(int species) {
        SweepClassifierSet set = base(species);
        set.enableZeroCrossings = true;
        set.nCrossings = new int[]{1000, 2000};
        return set;
    }
    private static SweepClassifierSet failSweep(int species) {
        SweepClassifierSet set = base(species);
        set.enableSweep = true;
        set.zcSweep = new double[]{1e9, 2e9};
        return set;
    }
    private static SweepClassifierSet failMinCorrelation(int species) {
        SweepClassifierSet set = base(species);
        set.enableMinXCrossCorr = true;
        set.minCorr = 1e300;
        return set;
    }
    private static SweepClassifierSet failPeakCorrelation(int species) {
        SweepClassifierSet set = base(species);
        set.enablePeakXCorr = true;
        set.corrFactor = 1e300;
        return set;
    }
    private static SweepClassifierSet amplitudeChoice(int species, int choice) {
        SweepClassifierSet set = base(species);
        set.channelChoices = choice;
        set.testAmplitude = true;
        set.amplitudeRange = choice == SweepClassifierSet.CHANNELS_USE_MEANS
                ? new double[]{-12, -5} : new double[]{-3, 3};
        return set;
    }
    private static SweepClassifierSet filteredPass(int species) {
        SweepClassifierSet set = pass(species);
        set.enableFFTFilter = true;
        set.fftFilterParams = new FFTFilterParams();
        set.fftFilterParams.highPassFreq = 5000;
        return set;
    }
    private static SweepClassifierSet filteredAmplitudePass(int species) {
        SweepClassifierSet set = amplitudeChoice(species, SweepClassifierSet.CHANNELS_REQUIRE_ONE);
        set.enableFFTFilter = true;
        set.fftFilterParams = new FFTFilterParams();
        set.fftFilterParams.highPassFreq = 20000;
        return set;
    }
    private static SweepClassifierSet filteredUnrestrictedPeakPass(int species) {
        SweepClassifierSet set = base(species);
        set.restrictLength = false;
        set.enablePeak = true;
        set.peakSearchRange = new double[]{1000, 23000};
        set.peakRange = new double[]{8000, 10000};
        set.enableFFTFilter = true;
        set.fftFilterParams = new FFTFilterParams();
        set.fftFilterParams.highPassFreq = 20000;
        return set;
    }

    private static double[][] waveform() {
        double[][] wave = new double[2][96];
        for (int i = 0; i < wave[0].length; i++) {
            double envelope = Math.exp(-0.5 * Math.pow((i - 42.0) / 7.0, 2.0));
            wave[0][i] = 0.03 * Math.sin(i * 0.19) +
                    envelope * (Math.sin(2 * Math.PI * 9000 * i / SAMPLE_RATE) +
                    0.45 * Math.sin(2 * Math.PI * 14000 * i / SAMPLE_RATE));
            wave[1][i] = 0.2 * (0.02 * Math.cos(i * 0.11 + 0.2) +
                    0.82 * envelope * (Math.sin(2 * Math.PI * 9200 * i / SAMPLE_RATE + 0.4) +
                    0.25 * Math.sin(2 * Math.PI * 15000 * i / SAMPLE_RATE)));
        }
        return wave;
    }

    private static double[] amplitudes(double[][] wave) {
        double[] result = new double[wave.length];
        for (int channel = 0; channel < wave.length; channel++) {
            double slope = (wave[channel][wave[channel].length - 1] - wave[channel][0]) /
                    (wave[channel].length - 1);
            for (int i = 0; i < wave[channel].length; i++) {
                double correction = wave[channel][0] + slope * i;
                result[channel] = Math.max(result[channel], Math.abs(wave[channel][i] - correction));
            }
        }
        return result;
    }

    private static void writeDefaults(File file) throws Exception {
        file.getParentFile().mkdirs();
        SweepClassifierSet porpoise = new SweepClassifierSet();
        porpoise.porpoiseDefaults();
        SweepClassifierSet beaked = new SweepClassifierSet();
        beaked.beakedWhaleDefaults();
        try (PrintWriter writer = new PrintWriter(file)) {
            writer.println("name,enableLength,minLength,maxLength,lengthDb,enableEnergy,testLow,testHigh,control0Low,control0High,control1Low,control1High,threshold0,threshold1,enablePeak,searchLow,searchHigh,peakLow,peakHigh,enableMean,meanLow,meanHigh,enableZeroCrossings,zcLow,zcHigh,enableSweep,sweepLow,sweepHigh");
            writeDefault(writer, porpoise);
            writeDefault(writer, beaked);
        }
    }

    private static void writeDefault(PrintWriter writer, SweepClassifierSet set) {
        writer.printf(Locale.ROOT,
                "%s,%s,%.17g,%.17g,%.17g,%s,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%s,%.17g,%.17g,%.17g,%.17g,%s,%.17g,%.17g,%s,%d,%d,%s,%.17g,%.17g%n",
                set.getName(), set.enableLength, set.minLength, set.maxLength, set.lengthdB,
                set.enableEnergyBands, set.testEnergyBand[0], set.testEnergyBand[1],
                set.controlEnergyBand[0][0], set.controlEnergyBand[0][1],
                set.controlEnergyBand[1][0], set.controlEnergyBand[1][1],
                set.energyThresholds[0], set.energyThresholds[1], set.enablePeak,
                set.peakSearchRange[0], set.peakSearchRange[1], set.peakRange[0], set.peakRange[1],
                set.enableMean, set.meanRange[0], set.meanRange[1], set.enableZeroCrossings,
                set.nCrossings[0], set.nCrossings[1], set.enableSweep, set.zcSweep[0], set.zcSweep[1]);
    }

    @SuppressWarnings("unchecked")
    private static <T> T allocate(Class<T> type) throws Exception {
        Field field = Unsafe.class.getDeclaredField("theUnsafe");
        field.setAccessible(true);
        return (T) ((Unsafe) field.get(null)).allocateInstance(type);
    }
}
