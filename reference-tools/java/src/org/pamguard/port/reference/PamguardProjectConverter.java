package org.pamguard.port.reference;

import Acquisition.AcquisitionParameters;
import Array.Hydrophone;
import Array.PamArray;
import Array.Streamer;
import PamController.PSFXReadWriter;
import PamController.PamControlledUnitSettings;
import PamController.PamSettingsGroup;
import Spectrogram.WindowFunction;
import clickDetector.BasicClickIdParameters;
import clickDetector.ClickParameters;
import clickDetector.ClickTypeParams;
import clickDetector.echoDetection.SimpleEchoParams;
import clickTrainDetector.ClickTrainParams;
import clickTrainDetector.classification.CTClassifierParams;
import clickTrainDetector.classification.bearingClassifier.BearingClassifierParams;
import clickTrainDetector.classification.idiClassifier.IDIClassifierParams;
import clickTrainDetector.classification.simplechi2classifier.Chi2ThresholdParams;
import clickTrainDetector.classification.standardClassifier.StandardClassifierParams;
import clickTrainDetector.classification.templateClassifier.TemplateClassifierParams;
import clickTrainDetector.clickTrainAlgorithms.mht.MHTKernelParams;
import clickTrainDetector.clickTrainAlgorithms.mht.MHTParams;
import clickTrainDetector.clickTrainAlgorithms.mht.StandardMHTChi2Params;
import clickTrainDetector.clickTrainAlgorithms.mht.electricalNoiseFilter.SimpleElectricalNoiseParams;
import clickTrainDetector.classification.templateClassifier.DefualtSpectrumTemplates;
import clickTrainDetector.classification.templateClassifier.DefualtSpectrumTemplates.SpectrumTemplateType;
import fftManager.FFTParameters;
import IshmaelDetector.EnergySumParams;
import IshmaelDetector.SgramCorrParams;
import ltsa.LtsaParameters;
import matchedTemplateClassifer.MTClassifier;
import matchedTemplateClassifer.MatchTemplate;
import matchedTemplateClassifer.MatchedTemplateParams;
import noiseBandMonitor.NoiseBandSettings;
import spectrogramNoiseReduction.SpectrogramNoiseSettings;
import spectrogramNoiseReduction.medianFilter.MedianFilterParams;
import whistlesAndMoans.WhistleToneParameters;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Locale;

/**
 * PAMGuard project import: converts a .psfx settings file into an engine
 * session-create JSON document.
 *
 * The .psfx payload is Java-serialised object graphs, so this must run on the
 * JVM with PAMGuard's own classes (docs/182 establishes why C++ cannot do it).
 * Reading uses the real PSFXReadWriter.loadFileSettings; settings objects are
 * matched by their real class, not by unit-name strings, so renamed modules
 * still convert.
 *
 * The pinned PAMGuard version is the local source tree this compiles against —
 * the same tree every parity fixture treats as the oracle. `write-sample`
 * produces a .psfx *from that version* using the real PamArray, Hydrophone,
 * Streamer, AcquisitionParameters, FFTParameters, ClickParameters, and
 * WhistleToneParameters classes and the real PSFXReadWriter.writePSFX, so the
 * converter has a genuine file to be validated against without waiting for an
 * external one. A .psfx from a different PAMGuard build may still fail to
 * deserialise; that is Java serialisation's version-brittleness, reported as
 * exit 3 exactly as PamguardSettingsInspector does.
 *
 * Unmapped settings are REPORTED, never silently dropped: every unit that did
 * not contribute to the JSON is listed on stdout with its class.
 *
 * Usage:
 *   PamguardProjectConverter convert <settings.psfx> <session.json>
 *   PamguardProjectConverter write-sample <sample.psfx>
 */
public final class PamguardProjectConverter {

    private PamguardProjectConverter() {
    }

    public static void main(String[] args) throws Exception {
        Locale.setDefault(Locale.ROOT);
        if (args.length == 3 && args[0].equals("convert")) {
            convert(new File(args[1]), new File(args[2]));
            return;
        }
        if (args.length == 2 && args[0].equals("write-sample")) {
            writeSample(new File(args[1]));
            return;
        }
        System.err.println("Usage: PamguardProjectConverter convert <settings.psfx> <session.json>");
        System.err.println("       PamguardProjectConverter write-sample <sample.psfx>");
        System.exit(2);
    }

    // ------------------------------------------------------------------
    // convert
    // ------------------------------------------------------------------

    private static void convert(File psfxFile, File jsonFile) throws Exception {
        if (!psfxFile.exists()) {
            System.err.println("Settings file not found: " + psfxFile.getAbsolutePath());
            System.exit(2);
        }
        PamSettingsGroup group;
        try {
            group = PSFXReadWriter.getInstance().loadFileSettings(psfxFile);
        }
        catch (Exception e) {
            if (hasCause(e, java.io.InvalidClassException.class)) {
                System.err.println("Settings file is not loadable by this PAMGuard build.");
                System.err.println("Serialisation incompatibility: " + e.getMessage());
                System.exit(3);
            }
            throw e;
        }
        if (group == null || group.getUnitSettings() == null || group.getUnitSettings().isEmpty()) {
            System.err.println("No unit settings found in " + psfxFile.getAbsolutePath());
            System.exit(1);
            return;
        }

        AcquisitionParameters acquisition = null;
        PamArray array = null;
        FFTParameters fft = null;
        ClickParameters click = null;
        WhistleToneParameters whistle = null;
        BasicClickIdParameters basicClassifier = null;
        ClickTrainParams clickTrain = null;
        MHTParams mht = null;
        SimpleEchoParams echoParams = null;
        NoiseBandSettings noiseBand = null;
        LtsaParameters ltsaParams = null;
        EnergySumParams energySum = null;
        SgramCorrParams sgramCorr = null;
        MatchedTemplateParams matchedTemplate = null;

        for (PamControlledUnitSettings unit : group.getUnitSettings()) {
            Object settings;
            try {
                settings = unit.getSettings();
            }
            catch (Throwable t) {
                System.out.printf("skipped: %s / %s (unreadable: %s)%n",
                        unit.getUnitType(), unit.getUnitName(), t.getClass().getSimpleName());
                continue;
            }
            if (settings instanceof AcquisitionParameters && acquisition == null) {
                acquisition = (AcquisitionParameters) settings;
            }
            else if (settings instanceof PamArray && array == null) {
                array = (PamArray) settings;
            }
            else if (settings instanceof FFTParameters && fft == null) {
                fft = (FFTParameters) settings;
            }
            else if (settings instanceof ClickParameters && click == null) {
                click = (ClickParameters) settings;
            }
            else if (settings instanceof WhistleToneParameters && whistle == null) {
                whistle = (WhistleToneParameters) settings;
            }
            else if (settings instanceof BasicClickIdParameters && basicClassifier == null) {
                basicClassifier = (BasicClickIdParameters) settings;
            }
            else if (settings instanceof ClickTrainParams && clickTrain == null) {
                clickTrain = (ClickTrainParams) settings;
            }
            else if (settings instanceof MHTParams && mht == null) {
                mht = (MHTParams) settings;
            }
            else if (settings instanceof SimpleEchoParams && echoParams == null) {
                echoParams = (SimpleEchoParams) settings;
            }
            else if (settings instanceof NoiseBandSettings && noiseBand == null) {
                noiseBand = (NoiseBandSettings) settings;
            }
            else if (settings instanceof LtsaParameters && ltsaParams == null) {
                ltsaParams = (LtsaParameters) settings;
            }
            else if (settings instanceof EnergySumParams && energySum == null) {
                energySum = (EnergySumParams) settings;
            }
            else if (settings instanceof SgramCorrParams && sgramCorr == null) {
                sgramCorr = (SgramCorrParams) settings;
            }
            else if (settings instanceof MatchedTemplateParams && matchedTemplate == null) {
                matchedTemplate = (MatchedTemplateParams) settings;
            }
            else {
                System.out.printf("skipped: %s / %s (%s)%n", unit.getUnitType(), unit.getUnitName(),
                        settings == null ? "<null>" : settings.getClass().getName());
            }
        }

        if (acquisition == null) {
            System.err.println("No AcquisitionParameters in the settings file: sample rate and channel count are required.");
            System.exit(1);
            return;
        }

        StringBuilder json = new StringBuilder();
        json.append("{\n");
        json.append("  \"sessionId\": \"").append(escape(stripExtension(psfxFile.getName()))).append("-import\",\n");
        json.append("  \"sampleRateHz\": ").append(format(acquisition.sampleRate)).append(",\n");
        json.append("  \"channelCount\": ").append(acquisition.nChannels);
        if (acquisition.voltsPeak2Peak > 0) {
            json.append(",\n  \"acquisition\": { \"voltsPeak2Peak\": ")
                    .append(format(acquisition.voltsPeak2Peak));
            if (acquisition.preamplifier != null) {
                json.append(", \"preampGainDb\": ").append(format(acquisition.preamplifier.getGain()));
            }
            json.append(" }");
        }
        else {
            System.out.println("skipped: acquisition calibration (voltsPeak2Peak not set)");
        }

        if (array != null) {
            json.append(",\n  \"array\": {\n");
            json.append("    \"id\": \"").append(escape(psfxImportArrayName(array))).append("\",\n");
            json.append("    \"speedOfSoundMps\": ").append(format(array.getSpeedOfSound())).append(",\n");
            json.append("    \"speedOfSoundErrorMps\": ").append(format(array.getSpeedOfSoundError())).append(",\n");
            json.append("    \"streamers\": [");
            int streamerCount = array.getNumStreamers();
            for (int i = 0; i < streamerCount; i++) {
                Streamer streamer = array.getStreamer(i);
                if (i > 0) {
                    json.append(",");
                }
                json.append("\n      { \"id\": ").append(streamer.getStreamerIndex());
                json.append(", \"xM\": ").append(format(streamer.getCoordinate(0)));
                json.append(", \"yM\": ").append(format(streamer.getCoordinate(1)));
                json.append(", \"zM\": ").append(format(streamer.getCoordinate(2)));
                json.append(", \"headingDegrees\": ").append(format(zeroIfNull(streamer.getHeading())));
                json.append(", \"pitchDegrees\": ").append(format(zeroIfNull(streamer.getPitch())));
                json.append(", \"rollDegrees\": ").append(format(zeroIfNull(streamer.getRoll())));
                json.append(" }");
            }
            json.append("\n    ],\n");
            json.append("    \"hydrophones\": [");
            ArrayList<Hydrophone> hydrophones = array.getHydrophoneArray();
            for (int i = 0; i < hydrophones.size(); i++) {
                Hydrophone hydrophone = hydrophones.get(i);
                double[] coordinates = hydrophone.getCoordinates();
                double[] errors = coordinateErrors(hydrophone);
                if (i > 0) {
                    json.append(",");
                }
                // Engine channels are hydrophone indices; PAMGuard's channel-
                // to-hydrophone mapping lives in acquisition settings and is
                // identity in every configuration the engine supports.
                json.append("\n      { \"channel\": ").append(i);
                json.append(", \"xM\": ").append(format(coordinates[0]));
                json.append(", \"yM\": ").append(format(coordinates[1]));
                json.append(", \"zM\": ").append(format(coordinates[2]));
                json.append(", \"xErrorM\": ").append(format(errors[0]));
                json.append(", \"yErrorM\": ").append(format(errors[1]));
                json.append(", \"zErrorM\": ").append(format(errors[2]));
                json.append(", \"sensitivityDb\": ").append(format(hydrophone.getSensitivity()));
                json.append(", \"streamerId\": ").append(hydrophone.getStreamerId());
                json.append(" }");
            }
            json.append("\n    ]\n  }");
        }

        if (fft != null) {
            json.append(",\n  \"fft\": {\n");
            json.append("    \"length\": ").append(fft.fftLength).append(",\n");
            json.append("    \"hop\": ").append(fft.fftHop).append(",\n");
            // The engine accepts PAMGuard's WindowFunction integer directly.
            json.append("    \"windowType\": ").append(fft.windowFunction).append(",\n");
            json.append("    \"channels\": [").append(bitmapChannels(fft.channelMap)).append("]\n  }");
        }

        if (click != null) {
            json.append(",\n  \"click\": {\n");
            json.append("    \"enabled\": true,\n");
            json.append("    \"localisation\": ").append(array != null).append(",\n");
            json.append("    \"channelBitmap\": ").append(clickChannelBitmap(click)).append(",\n");
            json.append("    \"triggerBitmap\": ").append(click.triggerBitmap).append(",\n");
            json.append("    \"minTriggerChannels\": ").append(click.minTriggerChannels).append(",\n");
            json.append("    \"thresholdDb\": ").append(format(click.dbThreshold)).append(",\n");
            json.append("    \"longFilter\": ").append(format(click.longFilter)).append(",\n");
            json.append("    \"shortFilter\": ").append(format(click.shortFilter)).append(",\n");
            json.append("    \"preSample\": ").append(click.preSample).append(",\n");
            json.append("    \"postSample\": ").append(click.postSample).append(",\n");
            json.append("    \"minSep\": ").append(click.minSep).append(",\n");
            json.append("    \"maxLength\": ").append(click.maxLength);
            appendIirFilter(json, "preFilter", click.preFilter);
            appendIirFilter(json, "triggerFilter", click.triggerFilter);
            if (click.runEchoOnline || echoParams != null) {
                json.append(",\n    \"echo\": { \"runOnline\": ").append(click.runEchoOnline);
                json.append(", \"discardEchoes\": ").append(click.discardEchoes);
                if (echoParams != null) {
                    json.append(", \"maxIntervalSeconds\": ").append(format(echoParams.maxIntervalSeconds));
                }
                json.append(" }");
            }
            if (basicClassifier != null && basicClassifier.clickTypeParams != null
                    && !basicClassifier.clickTypeParams.isEmpty()) {
                json.append(",\n    \"basicClassifier\": {\n      \"enabled\": true,\n      \"types\": [");
                for (int i = 0; i < basicClassifier.clickTypeParams.size(); i++) {
                    ClickTypeParams type = basicClassifier.clickTypeParams.get(i);
                    if (i > 0) {
                        json.append(",");
                    }
                    json.append("\n        { \"speciesCode\": ").append(type.getSpeciesCode());
                    json.append(", \"discard\": ").append(type.getDiscard());
                    json.append(", \"whichSelections\": ").append(type.whichSelections);
                    appendRange(json, "band1FreqHz", type.band1Freq);
                    appendRange(json, "band2FreqHz", type.band2Freq);
                    appendRange(json, "band1EnergyDb", type.band1Energy);
                    appendRange(json, "band2EnergyDb", type.band2Energy);
                    json.append(", \"bandEnergyDifferenceDb\": ").append(format(type.bandEnergyDifference));
                    appendRange(json, "peakFrequencySearchHz", type.peakFrequencySearch);
                    appendRange(json, "peakFrequencyRangeHz", type.peakFrequencyRange);
                    appendRange(json, "peakWidthHz", type.peakWidth);
                    json.append(", \"widthEnergyFraction\": ").append(format(type.widthEnergyFraction));
                    appendRange(json, "meanSumRangeHz", type.meanSumRange);
                    appendRange(json, "meanSelectionRangeHz", type.meanSelRange);
                    appendRange(json, "clickLengthMs", type.clickLength);
                    json.append(", \"lengthEnergyFraction\": ").append(format(type.lengthEnergyFraction));
                    json.append(" }");
                }
                json.append("\n      ]\n    }");
            }
            if (mht != null && mht.chi2Params instanceof StandardMHTChi2Params && mht.mhtKernal != null) {
                StandardMHTChi2Params chi2 = (StandardMHTChi2Params) mht.chi2Params;
                MHTKernelParams kernel = mht.mhtKernal;
                json.append(",\n    \"train\": {\n      \"enabled\": true,\n      \"algorithm\": \"mht\",\n      \"mht\": {");
                // StandardMHTChi2.createChi2Vars order: IDI, Amplitude,
                // Bearing, Correlation, TimeDelay, Length, PeakFrequency —
                // the enable array is parallel to it.
                boolean[] enable = chi2.enable;
                json.append("\n        \"enableIdi\": ").append(enableAt(enable, 0)).append(",");
                json.append("\n        \"enableAmplitude\": ").append(enableAt(enable, 1)).append(",");
                json.append("\n        \"enableBearing\": ").append(enableAt(enable, 2)).append(",");
                json.append("\n        \"enableCorrelation\": ").append(enableAt(enable, 3)).append(",");
                json.append("\n        \"enableTimeDelay\": ").append(enableAt(enable, 4)).append(",");
                json.append("\n        \"enableLength\": ").append(enableAt(enable, 5)).append(",");
                json.append("\n        \"enablePeakFrequency\": ").append(enableAt(enable, 6)).append(",");
                json.append("\n        \"maxIci\": ").append(format(chi2.maxICI)).append(",");
                json.append("\n        \"coastPenalty\": ").append(format(chi2.coastPenalty)).append(",");
                json.append("\n        \"newTrackPenalty\": ").append(format(chi2.newTrackPenalty)).append(",");
                json.append("\n        \"newTrackN\": ").append(chi2.newTrackN).append(",");
                json.append("\n        \"lowIciExponent\": ").append(format(chi2.lowICIExponent)).append(",");
                json.append("\n        \"longTrackExponent\": ").append(format(chi2.longTrackExponent)).append(",");
                json.append("\n        \"useElectricalNoiseFilter\": ").append(chi2.useElectricNoiseFilter).append(",");
                SimpleElectricalNoiseParams noise = chi2.electricalNoiseParams == null
                        ? new SimpleElectricalNoiseParams() : chi2.electricalNoiseParams;
                json.append("\n        \"electricalNoiseMinChi2\": ").append(format(noise.minChi2)).append(",");
                json.append("\n        \"electricalNoiseNDataUnits\": ").append(noise.nDataUnits).append(",");
                json.append("\n        \"nHold\": ").append(kernel.nHold).append(",");
                json.append("\n        \"nPruneback\": ").append(kernel.nPruneback).append(",");
                json.append("\n        \"nPrunebackStart\": ").append(kernel.nPruneBackStart).append(",");
                json.append("\n        \"maxCoast\": ").append(kernel.maxCoast);
                json.append("\n      }");
                if (clickTrain != null && clickTrain.runClassifier) {
                    appendClassifierChain(json, clickTrain);
                }
                json.append("\n    }");
            }
            json.append("\n  }");
        }

        if (whistle != null) {
            if (fft == null) {
                // Frequency limits convert to FFT bins, so a whistle module
                // without an FFT module cannot be mapped faithfully.
                System.out.println("skipped: whistle settings present but no FFT settings to derive search bins from");
            }
            else {
                double sampleRate = acquisition.sampleRate;
                long bin0 = Math.round(whistle.getMinFrequency() / sampleRate * fft.fftLength);
                long bin1 = Math.round(whistle.getMaxFrequency(sampleRate) / sampleRate * fft.fftLength);
                json.append(",\n  \"whistle\": {\n");
                json.append("    \"enabled\": true,\n");
                json.append("    \"regionEnabled\": true,\n");
                json.append("    \"searchBin0\": ").append(bin0).append(",\n");
                json.append("    \"searchBin1\": ").append(bin1).append(",\n");
                json.append("    \"minPixels\": ").append(whistle.minPixels).append(",\n");
                json.append("    \"minLength\": ").append(whistle.minLength).append(",\n");
                json.append("    \"maxCrossLength\": ").append(whistle.maxCrossLength).append(",\n");
                json.append("    \"connectType\": ").append(whistle.getConnectType()).append(",\n");
                json.append("    \"fragmentationMethod\": ").append(whistle.fragmentationMethod).append(",\n");
                json.append("    \"keepShapeStubs\": ").append(whistle.keepShapeStubs);
                appendNoiseSettings(json, whistle.getSpecNoiseSettings());
                json.append("\n  }");
            }
        }

        if (noiseBand != null) {
            String bandName;
            switch (noiseBand.bandType) {
            case OCTAVE: bandName = "octave"; break;
            case THIRDOCTAVE: bandName = "thirdOctave"; break;
            case DECIDECADE: bandName = "decidecade"; break;
            case DECADE: bandName = "decade"; break;
            case TENTHOCTAVE: bandName = "tenthOctave"; break;
            case TWELTHOCTAVE: bandName = "twelfthOctave"; break;
            default: bandName = null;
            }
            if (bandName == null || noiseBand.filterType != Filters.FilterType.BUTTERWORTH) {
                System.out.println("skipped: noise band monitor ("
                        + (bandName == null ? "unsupported band type" : "non-Butterworth filters") + ")");
            }
            else {
                json.append(",\n  \"noiseBand\": { \"enabled\": true, \"bandType\": \"").append(bandName);
                json.append("\", \"minFrequencyHz\": ").append(format(noiseBand.getMinFrequency()));
                json.append(", \"maxFrequencyHz\": ").append(format(noiseBand.getMaxFrequency()));
                json.append(", \"referenceFrequencyHz\": ").append(format(noiseBand.getReferenceFrequency()));
                json.append(", \"iirOrder\": ").append(noiseBand.iirOrder);
                json.append(", \"outputIntervalSeconds\": ").append(noiseBand.outputIntervalSeconds);
                json.append(" }");
            }
        }

        if (ltsaParams != null) {
            if (ltsaParams.intervalSeconds > 0) {
                json.append(",\n  \"ltsa\": { \"enabled\": true, \"intervalSeconds\": ")
                        .append(ltsaParams.intervalSeconds).append(" }");
            }
            else {
                System.out.println("skipped: LTSA (intervalSeconds not positive)");
            }
        }

        if (energySum != null) {
            if (energySum.f1 > energySum.f0) {
                json.append(",\n  \"ishmael\": { \"enabled\": true");
                json.append(", \"f0\": ").append(format(energySum.f0));
                json.append(", \"f1\": ").append(format(energySum.f1));
                json.append(", \"ratioF0\": ").append(format(energySum.ratiof0));
                json.append(", \"ratioF1\": ").append(format(energySum.ratiof1));
                json.append(", \"useRatio\": ").append(energySum.useRatio);
                json.append(", \"useLog\": ").append(energySum.useLog);
                json.append(", \"adaptiveThreshold\": ").append(energySum.adaptiveThreshold);
                json.append(", \"longFilter\": ").append(format(energySum.longFilter));
                json.append(", \"spikeDecay\": ").append(format(energySum.spikeDecay));
                json.append(", \"outputSmoothing\": ").append(energySum.outPutSmoothing);
                // shortFilter is a boxed Double and can be null in files
                // written before the field existed; the runtime default
                // EnergySumParams.clone() would restore is 0.1.
                json.append(", \"shortFilter\": ").append(format(
                        energySum.shortFilter == null ? 0.1 : energySum.shortFilter));
                json.append(", \"thresh\": ").append(format(energySum.thresh));
                json.append(", \"minTimeSeconds\": ").append(format(energySum.minTime));
                json.append(", \"maxTimeSeconds\": ").append(format(energySum.maxTime));
                json.append(", \"refractoryTimeSeconds\": ").append(format(energySum.refractoryTime));
                json.append(" }");
            }
            else {
                System.out.println("skipped: Ishmael energy sum (f1 not above f0)");
            }
        }

        if (sgramCorr != null) {
            if (sgramCorr.segment == null || sgramCorr.segment.length == 0) {
                System.out.println("skipped: Ishmael spectrogram correlation (no segments)");
            }
            else {
                json.append(",\n  \"sgramCorr\": { \"enabled\": true, \"segments\": [");
                for (int i = 0; i < sgramCorr.segment.length; i++) {
                    if (i > 0) {
                        json.append(", ");
                    }
                    json.append("[").append(format(sgramCorr.segment[i][0]))
                            .append(", ").append(format(sgramCorr.segment[i][1]))
                            .append(", ").append(format(sgramCorr.segment[i][2]))
                            .append(", ").append(format(sgramCorr.segment[i][3])).append("]");
                }
                json.append("]");
                json.append(", \"spread\": ").append(format(sgramCorr.spread));
                json.append(", \"useLog\": ").append(sgramCorr.useLog);
                json.append(", \"thresh\": ").append(format(sgramCorr.thresh));
                json.append(", \"minTimeSeconds\": ").append(format(sgramCorr.minTime));
                json.append(", \"maxTimeSeconds\": ").append(format(sgramCorr.maxTime));
                json.append(", \"refractoryTimeSeconds\": ").append(format(sgramCorr.refractoryTime));
                json.append(" }");
            }
        }

        if (matchedTemplate != null) {
            if (matchedTemplate.classifiers == null || matchedTemplate.classifiers.isEmpty()) {
                System.out.println("skipped: matched template classifier (no classifiers)");
            }
            else {
                json.append(",\n  \"matchedTemplate\": { \"enabled\": true");
                json.append(", \"normalisationType\": ").append(matchedTemplate.normalisationType);
                json.append(", \"peakSearch\": ").append(matchedTemplate.peakSearch);
                json.append(", \"peakSmoothing\": ").append(matchedTemplate.peakSmoothing);
                json.append(", \"lengthDb\": ").append(format(matchedTemplate.lengthdB));
                json.append(", \"restrictedBins\": ").append(matchedTemplate.restrictedBins);
                json.append(", \"channelClassification\": ").append(matchedTemplate.channelClassification);
                json.append(",\n    \"classifiers\": [");
                for (int i = 0; i < matchedTemplate.classifiers.size(); i++) {
                    MTClassifier classifier = matchedTemplate.classifiers.get(i);
                    if (i > 0) {
                        json.append(",");
                    }
                    json.append("\n      { \"thresholdToAccept\": ")
                            .append(format(classifier.thresholdToAccept));
                    appendMatchTemplate(json, "match", classifier.waveformMatch);
                    appendMatchTemplate(json, "reject", classifier.waveformReject);
                    json.append(" }");
                }
                json.append("\n    ] }");
            }
        }

        json.append("\n}\n");

        jsonFile.getParentFile().mkdirs();
        try (PrintWriter writer = new PrintWriter(jsonFile)) {
            writer.print(json);
        }
        System.out.printf("converted: acquisition=%s array=%s fft=%s click=%s whistle=%s -> %s%n",
                acquisition != null, array != null, fft != null, click != null, whistle != null,
                jsonFile.getPath());
    }

    // ------------------------------------------------------------------
    // write-sample
    // ------------------------------------------------------------------

    private static void writeSample(File psfxFile) throws Exception {
        // AcquisitionParameters' constructor reaches PamController.getInstance()
        // — the usual PamController wall. Deserialisation never calls
        // constructors, which is why *reading* real files works headlessly, so
        // allocate the sample instance the same way serialisation itself does.
        AcquisitionParameters acquisition = newWithoutConstructor(AcquisitionParameters.class);
        acquisition.voltsPeak2Peak = 5.0;
        acquisition.sampleRate = 96000;
        acquisition.nChannels = 4;

        // PamArray's constructor reaches HydrophoneLocators.getInstance() and
        // through it PamController, so it gets the same serialisation-style
        // allocation; its list fields are initialisers the allocation skips,
        // so they are seeded reflectively before use.
        PamArray array = newWithoutConstructor(PamArray.class);
        setPrivateField(array, "arrayName", "converter-sample-array");
        setPrivateField(array, "streamers", new ArrayList<Streamer>());
        setPrivateField(array, "hydrophoneArray", new ArrayList<Hydrophone>());
        array.setSpeedOfSound(1502.0);
        array.setSpeedOfSoundError(3.0);
        Streamer streamer = new Streamer(0, 10.0, 5.0, 0.0, 0.01, 0.01, 0.01);
        streamer.setHeading(15.0);
        streamer.setPitch(-3.0);
        streamer.setRoll(2.0);
        array.addStreamer(streamer);
        double[][] positions = {
                {0.0, 0.0, 0.0},
                {2.0, 0.0, 0.0},
                {0.0, 3.0, 0.0},
                {0.5, 1.0, 2.5},
        };
        for (int i = 0; i < positions.length; i++) {
            Hydrophone hydrophone = new Hydrophone(i, positions[i][0], positions[i][1], positions[i][2],
                    0.05, 0.05, 0.05, "sample", -201.0, new double[]{100.0, 48000.0}, 20.0);
            hydrophone.setStreamerId(0);
            array.addHydrophone(hydrophone);
        }

        FFTParameters fft = new FFTParameters();
        fft.fftLength = 512;
        fft.fftHop = 256;
        fft.windowFunction = WindowFunction.HANNING;
        fft.channelMap = 0xF;

        ClickParameters click = new ClickParameters();
        click.runEchoOnline = true;
        click.discardEchoes = false;
        click.dbThreshold = 12.0;
        click.longFilter = 0.00002;
        click.shortFilter = 0.2;
        click.preSample = 30;
        click.postSample = 50;
        click.minSep = 80;
        click.maxLength = 512;
        click.triggerBitmap = 0xF;
        click.minTriggerChannels = 2;
        setPrivateInt(click, "channelBitmap", 0xF);

        BasicClickIdParameters basicClassifier = new BasicClickIdParameters();
        ClickTypeParams porpoise = new ClickTypeParams(2, ClickTypeParams.STANDARD_PORPOISE);
        basicClassifier.clickTypeParams.add(porpoise);
        ClickTypeParams custom = new ClickTypeParams(7);
        custom.whichSelections = ClickTypeParams.ENABLE_ENERGYBAND | ClickTypeParams.ENABLE_CLICKLENGTH;
        custom.band1Freq = new double[]{20000.0, 40000.0};
        custom.band2Freq = new double[]{5000.0, 15000.0};
        custom.band1Energy = new double[]{6.0, 96.0};
        custom.band2Energy = new double[]{0.0, 90.0};
        custom.bandEnergyDifference = 6.0;
        custom.clickLength = new double[]{0.03, 0.6};
        custom.lengthEnergyFraction = 0.95;
        custom.setDiscard(true);
        basicClassifier.clickTypeParams.add(custom);

        // MHTParams' field initialisers construct the real chi2 var list, whose
        // constructors are safe headlessly (the MHT fixture exporters already
        // construct them), but the enclosing defaults come from field
        // initialisers rather than a constructor body, so plain construction
        // works; the sample then overrides the values the converter maps.
        MHTParams mht = newWithoutConstructor(MHTParams.class);
        MHTKernelParams kernel = new MHTKernelParams();
        kernel.nHold = 25;
        kernel.nPruneback = 5;
        kernel.nPruneBackStart = 7;
        kernel.maxCoast = 4;
        mht.mhtKernal = kernel;
        StandardMHTChi2Params chi2 = newWithoutConstructor(StandardMHTChi2Params.class);
        chi2.maxICI = 0.5;
        chi2.coastPenalty = 12.0;
        chi2.newTrackPenalty = 60.0;
        chi2.newTrackN = 4;
        chi2.lowICIExponent = 0.15;
        chi2.longTrackExponent = 0.12;
        chi2.useElectricNoiseFilter = true;
        SimpleElectricalNoiseParams noise = new SimpleElectricalNoiseParams();
        noise.minChi2 = 0.00002;
        noise.nDataUnits = 25;
        chi2.electricalNoiseParams = noise;
        // createChi2Vars order: IDI, Amplitude, Bearing, Correlation,
        // TimeDelay, Length, PeakFrequency.
        chi2.enable = new boolean[]{true, true, false, false, false, true, true};
        mht.chi2Params = chi2;

        ClickTrainParams clickTrain = newWithoutConstructor(ClickTrainParams.class);
        clickTrain.runClassifier = true;
        Chi2ThresholdParams pre = new Chi2ThresholdParams();
        pre.chi2Threshold = 1200.0;
        pre.minClicks = 6;
        pre.minTime = 2.5;
        clickTrain.simpleCTClassifier = pre;
        IDIClassifierParams idiClassifier = newWithoutConstructor(IDIClassifierParams.class);
        idiClassifier.useMedianIDI = true;
        idiClassifier.minMedianIDI = 0.05;
        idiClassifier.maxMedianIDI = 1.5;
        idiClassifier.speciesFlag = 3;
        TemplateClassifierParams templateClassifier = newWithoutConstructor(TemplateClassifierParams.class);
        templateClassifier.spectrumTemplate = DefualtSpectrumTemplates.getTemplate(SpectrumTemplateType.BEAKED_WHALE);
        templateClassifier.corrThreshold = 0.6;
        templateClassifier.speciesFlag = 5;
        clickTrain.ctClassifierParams = new CTClassifierParams[]{idiClassifier, templateClassifier};

        WhistleToneParameters whistle = new WhistleToneParameters();
        whistle.minPixels = 25;
        whistle.minLength = 12;
        whistle.maxCrossLength = 6;
        whistle.keepShapeStubs = true;
        whistle.setMinFrequency(2000.0);
        whistle.setMaxFrequency(20000.0);
        SpectrogramNoiseSettings noiseSettings = new SpectrogramNoiseSettings();
        noiseSettings.setRunMethod(0, true);
        noiseSettings.setRunMethod(1, true);
        noiseSettings.setRunMethod(2, false);
        noiseSettings.setRunMethod(3, true);
        MedianFilterParams medianNoise = new MedianFilterParams();
        medianNoise.filterLength = 41;
        spectrogramNoiseReduction.averageSubtraction.AverageSubtractionParameters averageNoise =
                new spectrogramNoiseReduction.averageSubtraction.AverageSubtractionParameters();
        averageNoise.updateConstant = 0.03;
        spectrogramNoiseReduction.threshold.ThresholdParams thresholdNoise =
                new spectrogramNoiseReduction.threshold.ThresholdParams();
        thresholdNoise.thresholdDB = 9.0;
        thresholdNoise.finalOutput = 2;
        noiseSettings.addSettings(medianNoise);
        noiseSettings.addSettings(averageNoise);
        noiseSettings.addSettings(null);
        noiseSettings.addSettings(thresholdNoise);
        whistle.setSpecNoiseSettings(noiseSettings);

        PamSettingsGroup group = new PamSettingsGroup(1782950400000L);
        group.addSettings(new PamControlledUnitSettings("Data Acquisition", "Sound Acquisition",
                AcquisitionParameters.class.getName(), 1, acquisition));
        group.addSettings(new PamControlledUnitSettings("Array Manager", "Array Manager",
                PamArray.class.getName(), 1, array));
        group.addSettings(new PamControlledUnitSettings("FFT Engine", "FFT Engine",
                FFTParameters.class.getName(), 1, fft));
        group.addSettings(new PamControlledUnitSettings("Click Detector", "Click Detector",
                ClickParameters.class.getName(), 1, click));
        group.addSettings(new PamControlledUnitSettings("WhistlesMoans", "Whistle and Moan Detector",
                WhistleToneParameters.class.getName(), 1, whistle));
        group.addSettings(new PamControlledUnitSettings("Click Detector", "Basic Click Identifier",
                BasicClickIdParameters.class.getName(), 1, basicClassifier));
        group.addSettings(new PamControlledUnitSettings("MHT Click Train Detector", "Click Train Detector",
                MHTParams.class.getName(), 1, mht));
        group.addSettings(new PamControlledUnitSettings("Click Train Detector", "Click Train Detector",
                ClickTrainParams.class.getName(), 1, clickTrain));
        SimpleEchoParams echoSample = new SimpleEchoParams();
        echoSample.maxIntervalSeconds = 0.08;
        group.addSettings(new PamControlledUnitSettings("Click Detector", "Echo Detector",
                SimpleEchoParams.class.getName(), 1, echoSample));
        NoiseBandSettings noiseBandSample = new NoiseBandSettings();
        noiseBandSample.bandType = noiseBandMonitor.BandType.THIRDOCTAVE;
        noiseBandSample.iirOrder = 6;
        noiseBandSample.outputIntervalSeconds = 5;
        group.addSettings(new PamControlledUnitSettings("Noise Band", "Noise Band Monitor",
                NoiseBandSettings.class.getName(), 1, noiseBandSample));
        LtsaParameters ltsaSample = new LtsaParameters();
        ltsaSample.intervalSeconds = 10;
        group.addSettings(new PamControlledUnitSettings("LTSA", "LTSA",
                LtsaParameters.class.getName(), 1, ltsaSample));
        EnergySumParams energySumSample = new EnergySumParams();
        energySumSample.f0 = 200.0;
        energySumSample.f1 = 2500.0;
        energySumSample.thresh = 2.0;
        energySumSample.minTime = 0.05;
        energySumSample.refractoryTime = 0.2;
        group.addSettings(new PamControlledUnitSettings("Energy Sum", "Energy Sum",
                EnergySumParams.class.getName(), 1, energySumSample));
        SgramCorrParams sgramSample = new SgramCorrParams();
        sgramSample.segment = new double[][]{{0.0, 500.0, 0.2, 1500.0}};
        sgramSample.spread = 80.0;
        sgramSample.thresh = 0.05;
        sgramSample.minTime = 0.05;
        sgramSample.refractoryTime = 0.2;
        group.addSettings(new PamControlledUnitSettings("Sgram Corr", "Spectrogram Correlation",
                SgramCorrParams.class.getName(), 1, sgramSample));
        MatchedTemplateParams matchedSample = new MatchedTemplateParams();
        // The default MTClassifier carries 192 kHz templates, above the
        // sample acquisition's 96 kHz — the engine would (correctly) refuse
        // to decimate them, so the sample uses synthetic 48 kHz templates.
        double[] matchWave = new double[64];
        double[] rejectWave = new double[64];
        for (int i = 0; i < matchWave.length; i++) {
            matchWave[i] = Math.exp(-0.05 * i) * Math.sin(2 * Math.PI * 9000.0 * i / 48000.0);
            rejectWave[i] = Math.exp(-0.04 * i) * Math.sin(2 * Math.PI * 3000.0 * i / 48000.0);
        }
        MTClassifier mtClassifier = new MTClassifier();
        mtClassifier.thresholdToAccept = 0.1;
        mtClassifier.normalisation = matchedSample.normalisationType;
        mtClassifier.waveformMatch = new MatchTemplate("sample match", matchWave, 48000);
        mtClassifier.waveformReject = new MatchTemplate("sample reject", rejectWave, 48000);
        matchedSample.classifiers.clear();
        matchedSample.classifiers.add(mtClassifier);
        matchedSample.restrictedBins = 256;
        group.addSettings(new PamControlledUnitSettings("Matched Template", "Matched Template Classifier",
                MatchedTemplateParams.class.getName(), 1, matchedSample));
        // A module the engine has no equivalent for, so the converter's
        // skip reporting always has something real to report.
        group.addSettings(new PamControlledUnitSettings("Spectrogram", "User Display",
                String.class.getName(), 1, "not-a-detector-module"));

        psfxFile.getParentFile().mkdirs();
        if (!PSFXReadWriter.getInstance().writePSFX(psfxFile.getPath(), group)) {
            System.err.println("writePSFX failed for " + psfxFile.getPath());
            System.exit(1);
        }
        System.out.println("wrote sample psfx: " + psfxFile.getPath());
    }

    // ------------------------------------------------------------------
    // helpers
    // ------------------------------------------------------------------

    /**
     * Allocate an instance without running its constructor, exactly as
     * ObjectInputStream does for a Serializable class. Needed only for sample
     * generation, where a settings class's constructor is PamController-coupled.
     */
    @SuppressWarnings("unchecked")
    private static <T> T newWithoutConstructor(Class<T> type) throws Exception {
        java.lang.reflect.Constructor<?> objectConstructor = Object.class.getDeclaredConstructor();
        java.lang.reflect.Constructor<?> allocator = sun.reflect.ReflectionFactory.getReflectionFactory()
                .newConstructorForSerialization(type, objectConstructor);
        return (T) allocator.newInstance();
    }

    private static boolean hasCause(Throwable error, Class<? extends Throwable> type) {
        while (error != null) {
            if (type.isInstance(error)) {
                return true;
            }
            error = error.getCause();
        }
        return false;
    }

    private static String psfxImportArrayName(PamArray array) {
        String name = array.getArrayName();
        return name == null || name.isEmpty() ? "imported-array" : name;
    }

    /** Hydrophone.getCoordinateErrors is protected, so reach it reflectively. */
    private static double[] coordinateErrors(Hydrophone hydrophone) throws Exception {
        Method method = Hydrophone.class.getDeclaredMethod("getCoordinateErrors");
        method.setAccessible(true);
        double[] errors = (double[]) method.invoke(hydrophone);
        return errors == null ? new double[3] : errors;
    }

    private static int clickChannelBitmap(ClickParameters click) throws Exception {
        Field field = ClickParameters.class.getDeclaredField("channelBitmap");
        field.setAccessible(true);
        return field.getInt(click);
    }

    private static void setPrivateInt(Object target, String fieldName, int value) throws Exception {
        Field field = target.getClass().getDeclaredField(fieldName);
        field.setAccessible(true);
        field.setInt(target, value);
    }

    private static void setPrivateField(Object target, String fieldName, Object value) throws Exception {
        Field field = target.getClass().getDeclaredField(fieldName);
        field.setAccessible(true);
        field.set(target, value);
    }

    /**
     * ClickTrainParams.simpleCTClassifier is the pre-classifier; the chain
     * proper lives in ctClassifierParams. StandardClassifierParams is a
     * composite holding nested params with an enable array, so it flattens
     * into the same per-type emitters the engine configures directly.
     */
    private static void appendClassifierChain(StringBuilder json, ClickTrainParams clickTrain) {
        json.append(",\n      \"classifier\": {\n        \"enabled\": true");
        Chi2ThresholdParams pre = clickTrain.simpleCTClassifier;
        if (pre != null) {
            json.append(",\n        \"preClassifier\": { \"chi2Threshold\": ").append(format(pre.chi2Threshold));
            json.append(", \"minClicks\": ").append(pre.minClicks);
            json.append(", \"minTimeSeconds\": ").append(format(pre.minTime));
            json.append(", \"speciesFlag\": ").append(pre.speciesFlag).append(" }");
        }
        if (clickTrain.ctClassifierParams != null) {
            for (CTClassifierParams params : clickTrain.ctClassifierParams) {
                appendClassifier(json, params);
            }
        }
        json.append("\n      }");
    }

    private static void appendClassifier(StringBuilder json, CTClassifierParams params) {
        if (params instanceof IDIClassifierParams) {
            IDIClassifierParams idi = (IDIClassifierParams) params;
            json.append(",\n        \"idi\": { \"enabled\": true");
            json.append(", \"useMedianIdi\": ").append(idi.useMedianIDI);
            json.append(", \"minMedianIdi\": ").append(format(zeroIfNull(idi.minMedianIDI)));
            json.append(", \"maxMedianIdi\": ").append(format(zeroIfNull(idi.maxMedianIDI)));
            json.append(", \"useMeanIdi\": ").append(idi.useMeanIDI);
            json.append(", \"minMeanIdi\": ").append(format(zeroIfNull(idi.minMeanIDI)));
            json.append(", \"maxMeanIdi\": ").append(format(zeroIfNull(idi.maxMeanIDI)));
            json.append(", \"useStdIdi\": ").append(idi.useStdIDI);
            json.append(", \"minStdIdi\": ").append(format(zeroIfNull(idi.minStdIDI)));
            json.append(", \"maxStdIdi\": ").append(format(zeroIfNull(idi.maxStdIDI)));
            json.append(", \"speciesFlag\": ").append(idi.speciesFlag).append(" }");
        }
        else if (params instanceof BearingClassifierParams) {
            BearingClassifierParams bearing = (BearingClassifierParams) params;
            // PAMGuard stores radians; the engine configures degrees.
            json.append(",\n        \"bearing\": { \"enabled\": true");
            json.append(", \"bearingLimMinDegrees\": ").append(format(Math.toDegrees(bearing.bearingLimMin)));
            json.append(", \"bearingLimMaxDegrees\": ").append(format(Math.toDegrees(bearing.bearingLimMax)));
            json.append(", \"useMean\": ").append(bearing.useMean);
            json.append(", \"minMeanBearingDerivativeDegrees\": ").append(format(Math.toDegrees(bearing.minMeanBearingD)));
            json.append(", \"maxMeanBearingDerivativeDegrees\": ").append(format(Math.toDegrees(bearing.maxMeanBearingD)));
            json.append(", \"useMedian\": ").append(bearing.useMedian);
            json.append(", \"minMedianBearingDerivativeDegrees\": ").append(format(Math.toDegrees(bearing.minMedianBearingD)));
            json.append(", \"maxMedianBearingDerivativeDegrees\": ").append(format(Math.toDegrees(bearing.maxMedianBearingD)));
            json.append(", \"useStd\": ").append(bearing.useStD);
            json.append(", \"minStdBearingDerivativeDegrees\": ").append(format(Math.toDegrees(bearing.minStdBearingD)));
            json.append(", \"maxStdBearingDerivativeDegrees\": ").append(format(Math.toDegrees(bearing.maxStdBearingD)));
            json.append(", \"speciesFlag\": ").append(bearing.speciesFlag).append(" }");
        }
        else if (params instanceof TemplateClassifierParams) {
            TemplateClassifierParams template = (TemplateClassifierParams) params;
            json.append(",\n        \"template\": { \"enabled\": true");
            if (template.spectrumTemplate != null && template.spectrumTemplate.name != null
                    && !template.spectrumTemplate.name.isEmpty()) {
                // A named template maps to the engine preset of the same name
                // (the engine holds PAMGuard's defaults with exact fixture
                // parity, docs/186); the raw spectrum would also work but the
                // name keeps the intent visible.
                json.append(", \"preset\": \"").append(escape(template.spectrumTemplate.name)).append("\"");
            }
            else if (template.spectrumTemplate != null && template.spectrumTemplate.waveform != null) {
                json.append(", \"spectrum\": [");
                for (int i = 0; i < template.spectrumTemplate.waveform.length; i++) {
                    if (i > 0) {
                        json.append(", ");
                    }
                    json.append(format(template.spectrumTemplate.waveform[i]));
                }
                json.append("], \"sampleRateHz\": ").append(format(template.spectrumTemplate.sR));
            }
            json.append(", \"correlationThreshold\": ").append(format(zeroIfNull(template.corrThreshold)));
            json.append(", \"speciesFlag\": ").append(template.speciesFlag).append(" }");
        }
        else if (params instanceof StandardClassifierParams) {
            StandardClassifierParams standard = (StandardClassifierParams) params;
            if (standard.ctClassifierParams != null) {
                for (int i = 0; i < standard.ctClassifierParams.length; i++) {
                    boolean enabled = standard.enable == null || (i < standard.enable.length && standard.enable[i]);
                    if (enabled && standard.ctClassifierParams[i] != null) {
                        appendClassifier(json, standard.ctClassifierParams[i]);
                    }
                }
            }
        }
        else {
            System.out.printf("skipped: click train classifier %s (unmapped type)%n",
                    params == null ? "<null>" : params.getClass().getName());
        }
    }

    /**
     * SpectrogramNoiseSettings rides inside WhistleToneParameters: runMethod
     * flags parallel to SpectrogramNoiseProcess's fixed method order (median,
     * average, kernel, threshold), with per-method settings in the same order.
     */
    /** ClickParameters.preFilter/triggerFilter, PAMGuard field names kept. */
    private static void appendMatchTemplate(StringBuilder json, String key, MatchTemplate template) {
        json.append(", \"").append(key).append("\": { \"name\": \"")
                .append(template.name == null ? "" : template.name.replace("\"", "'"))
                .append("\", \"sampleRateHz\": ").append(format(template.sR));
        json.append(", \"waveform\": [");
        for (int i = 0; i < template.waveform.length; i++) {
            if (i > 0) {
                json.append(",");
            }
            json.append(String.format(java.util.Locale.ROOT, "%.17g", template.waveform[i]));
        }
        json.append("] }");
    }

    private static void appendIirFilter(StringBuilder json, String key, Filters.FilterParams filter) {
        if (filter == null || filter.filterType == Filters.FilterType.NONE) {
            return;
        }
        String type = filter.filterType == Filters.FilterType.BUTTERWORTH ? "butterworth"
                : filter.filterType == Filters.FilterType.CHEBYCHEV ? "chebyshev" : null;
        if (type == null) {
            System.out.println("skipped: click " + key + " (unsupported filter type " + filter.filterType + ")");
            return;
        }
        String band;
        switch (filter.filterBand) {
        case HIGHPASS: band = "highpass"; break;
        case LOWPASS: band = "lowpass"; break;
        case BANDPASS: band = "bandpass"; break;
        case BANDSTOP: band = "bandstop"; break;
        default:
            System.out.println("skipped: click " + key + " (unsupported band " + filter.filterBand + ")");
            return;
        }
        json.append(",\n    \"").append(key).append("\": { \"type\": \"").append(type);
        json.append("\", \"band\": \"").append(band);
        json.append("\", \"order\": ").append(filter.filterOrder);
        json.append(", \"highPassFreq\": ").append(format(filter.highPassFreq));
        json.append(", \"lowPassFreq\": ").append(format(filter.lowPassFreq));
        json.append(", \"passBandRipple\": ").append(format(filter.passBandRipple));
        json.append(" }");
    }

    private static void appendNoiseSettings(StringBuilder json, SpectrogramNoiseSettings noise) {
        if (noise == null || noise.isRunMethod() == null) {
            return;
        }
        json.append(",\n    \"noise\": {");
        json.append(" \"medianFilter\": ").append(noise.isRunMethod(0));
        if (noise.getSettings(0) instanceof MedianFilterParams) {
            json.append(", \"medianFilterLength\": ")
                    .append(((MedianFilterParams) noise.getSettings(0)).filterLength);
        }
        json.append(", \"averageSubtraction\": ").append(noise.isRunMethod(1));
        if (noise.getSettings(1) instanceof spectrogramNoiseReduction.averageSubtraction.AverageSubtractionParameters) {
            json.append(", \"updateConstant\": ").append(format(
                    ((spectrogramNoiseReduction.averageSubtraction.AverageSubtractionParameters) noise.getSettings(1)).updateConstant));
        }
        json.append(", \"kernelSmoothing\": ").append(noise.isRunMethod(2));
        json.append(", \"threshold\": ").append(noise.isRunMethod(3));
        if (noise.getSettings(3) instanceof spectrogramNoiseReduction.threshold.ThresholdParams) {
            spectrogramNoiseReduction.threshold.ThresholdParams threshold =
                    (spectrogramNoiseReduction.threshold.ThresholdParams) noise.getSettings(3);
            json.append(", \"thresholdDb\": ").append(format(threshold.thresholdDB));
            json.append(", \"finalOutput\": ").append(threshold.finalOutput);
        }
        json.append(" }");
    }

    private static void appendRange(StringBuilder json, String key, double[] range) {
        if (range == null || range.length < 2) {
            return;
        }
        json.append(", \"").append(key).append("\": [").append(format(range[0])).append(", ")
                .append(format(range[1])).append("]");
    }

    private static boolean enableAt(boolean[] enable, int index) {
        return enable != null && index < enable.length && enable[index];
    }

    private static String bitmapChannels(int bitmap) {
        StringBuilder channels = new StringBuilder();
        for (int channel = 0; channel < 32; channel++) {
            if ((bitmap & (1 << channel)) != 0) {
                if (channels.length() > 0) {
                    channels.append(", ");
                }
                channels.append(channel);
            }
        }
        return channels.toString();
    }

    private static double zeroIfNull(Double value) {
        return value == null ? 0.0 : value;
    }

    private static String format(double value) {
        if (value == Math.rint(value) && Math.abs(value) < 1.0e15) {
            return String.format(Locale.ROOT, "%.1f", value);
        }
        return String.format(Locale.ROOT, "%.17g", value);
    }

    private static String escape(String text) {
        return text.replace("\\", "\\\\").replace("\"", "\\\"");
    }

    private static String stripExtension(String name) {
        int dot = name.lastIndexOf('.');
        return dot > 0 ? name.substring(0, dot) : name;
    }
}
