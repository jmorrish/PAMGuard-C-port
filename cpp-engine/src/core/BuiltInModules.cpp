#include "pamguard/core/BuiltInModules.h"

#include "pamguard/core/SignalNodes.h"
#include "pamguard/core/DetectorNodes.h"
#include "pamguard/core/FftDetectorNodes.h"
#include "pamguard/core/FilterDecimatorSettings.h"
#include "pamguard/core/IshmaelSettings.h"
#include "pamguard/core/LocalisationData.h"
#include "pamguard/core/MatchedTemplateSettings.h"
#include "pamguard/core/OperatorNodes.h"
#include "pamguard/core/SignalRoutingSettings.h"
#include "pamguard/core/WhistleMoanSettings.h"

#include <array>

namespace pamguard::core {

namespace {

PortDescriptor input(
    std::string id,
    std::string name,
    std::string data_type,
    std::vector<std::string> capabilities = {}) {
    return {
        std::move(id),
        std::move(name),
        PortDirection::Input,
        std::move(data_type),
        true,
        false,
        std::move(capabilities),
    };
}

PortDescriptor output(
    std::string id,
    std::string name,
    std::string data_type,
    std::vector<std::string> capabilities = {}) {
    return {
        std::move(id),
        std::move(name),
        PortDirection::Output,
        std::move(data_type),
        false,
        false,
        std::move(capabilities),
    };
}

PortDescriptor optional_input(
    std::string id,
    std::string name,
    std::string data_type,
    std::vector<std::string> capabilities = {}) {
    auto descriptor = input(
        std::move(id),
        std::move(name),
        std::move(data_type),
        std::move(capabilities));
    descriptor.required = false;
    return descriptor;
}

PortDescriptor optional_multi_input(
    std::string id,
    std::string name,
    std::string data_type,
    std::vector<std::string> capabilities = {}) {
    auto descriptor = optional_input(
        std::move(id),
        std::move(name),
        std::move(data_type),
        std::move(capabilities));
    descriptor.accepts_multiple = true;
    return descriptor;
}

void register_audio_transform(
    ModuleRegistry& registry,
    std::string id,
    std::string name,
    std::string description,
    std::string schema,
    std::string defaults) {
    registry.register_type({
        std::move(id),
        std::move(name),
        "Signal processing",
        std::move(description),
        0,
        {},
        {
            input("input", "Raw audio input", kRawAudioDataType, {"sampled"}),
            output("output", "Raw audio output", kRawAudioDataType, {"sampled"}),
        },
        std::move(schema),
        std::move(defaults),
    });
}

} // namespace

void register_builtin_module_types(ModuleRegistry& registry) {
    registry.register_type({
        "pamguard.acquisition",
        "Acquisition",
        "Sources",
        "Live device, stream, or file acquisition producing timestamped raw audio.",
        0,
        {},
        {
            output("audio", "Raw audio", kRawAudioDataType, {"sampled", "realtime"}),
        },
        R"({"type":"object","properties":{"sourceId":{"type":"string"},"sampleRateHz":{"type":"number","exclusiveMinimum":0},"channelCount":{"type":"integer","minimum":1,"maximum":32},"voltsPeak2Peak":{"type":"number","exclusiveMinimum":0},"subtractDC":{"type":"boolean"},"dcTimeConstantSeconds":{"type":"number","exclusiveMinimum":0},"calibrationDbOffsetByChannel":{"type":"array","items":{"type":"number"}}},"required":["sourceId","sampleRateHz","channelCount","subtractDC","dcTimeConstantSeconds","calibrationDbOffsetByChannel"]})",
        R"({"sourceId":"default","sampleRateHz":48000,"channelCount":1,"subtractDC":true,"dcTimeConstantSeconds":1,"calibrationDbOffsetByChannel":[]})",
    });
    register_audio_transform(
        registry,
        "pamguard.amplifier",
        "Amplifier",
        "PAMGuard Signal Amplifier: Gain (dB) and Invert for each absolute channel.",
        std::string(signal_amplifier_settings_schema_json()),
        signal_amplifier_default_settings_json());
    register_audio_transform(
        registry,
        "pamguard.patch-panel",
        "Patch Panel",
        "PAMGuard's 32-by-32 input/output routing matrix; arbitrary gains are an explicit Advanced extension.",
        std::string(patch_panel_settings_schema_json()),
        patch_panel_default_settings_json());
    register_audio_transform(
        registry,
        "pamguard.filter",
        "Filter",
        "PAMGuard Butterworth, Chebyshev, FIR, or FFT filtering on selected channels.",
        std::string(standalone_filter_settings_schema_json()),
        standalone_filter_default_settings_json());
    register_audio_transform(
        registry,
        "pamguard.decimator",
        "Decimator",
        "PAMGuard resampling with anti-alias filtering and selectable interpolation.",
        std::string(decimator_settings_schema_json()),
        decimator_default_settings_json());
    registry.register_type({
        "pamguard.fft",
        "FFT",
        "Signal processing",
        "Independent PAMGuard FFT process; add multiple instances for different bands and resolutions.",
        0,
        {},
        {
            input("input", "Raw audio input", kRawAudioDataType, {"sampled"}),
            output("fft", "FFT frames", kFftDataType, {"frequency-domain"}),
        },
        R"({"type":"object","properties":{"fftLength":{"type":"integer","minimum":2},"fftHop":{"type":"integer","minimum":1},"windowType":{"type":"string","enum":["Rectangular","Hamming","Hann","Bartlett","Blackman","Blackman-Harris"]},"channels":{"type":"array","minItems":1,"items":{"type":"integer","minimum":0}},"clickRemoval":{"type":"boolean"},"clickThreshold":{"type":"number"},"clickPower":{"type":"integer","minimum":2,"multipleOf":2}},"required":["fftLength","fftHop","windowType","channels","clickRemoval","clickThreshold","clickPower"]})",
        R"({"fftLength":1024,"fftHop":512,"windowType":"Hann","channels":[0],"clickRemoval":false,"clickThreshold":5.0,"clickPower":6})",
    });
    registry.register_type({
        "pamguard.spectrogram-noise",
        "Spectrogram Noise Reduction",
        "Signal processing",
        "PAMGuard FFT noise-reduction chain: median filter, average subtraction, Gaussian kernel smoothing, and threshold.",
        0,
        {},
        {
            input("input", "FFT input", kFftDataType, {"frequency-domain"}),
            output("output", "Noise-reduced FFT", kFftDataType, {"frequency-domain"}),
        },
        R"({"type":"object","properties":{"medianFilter":{"type":"boolean"},"medianFilterLength":{"type":"integer","minimum":1},"averageSubtraction":{"type":"boolean"},"updateConstant":{"type":"number","exclusiveMinimum":0,"exclusiveMaximum":1},"kernelSmoothing":{"type":"boolean"},"threshold":{"type":"boolean"},"thresholdDb":{"type":"number"},"finalOutput":{"type":"integer","minimum":0,"maximum":2}},"required":["medianFilter","medianFilterLength","averageSubtraction","updateConstant","kernelSmoothing","threshold","thresholdDb","finalOutput"]})",
        R"({"medianFilter":false,"medianFilterLength":61,"averageSubtraction":false,"updateConstant":0.02,"kernelSmoothing":false,"threshold":false,"thresholdDb":8.0,"finalOutput":2})",
    });
    registry.register_type({
        "pamguard.click-detector",
        "Click Detector",
        "Detectors",
        "PAMGuard click trigger, waveform extraction, noise sampling, background, and trigger-function process.",
        0,
        {},
        {
            input("input", "Raw audio input", kRawAudioDataType, {"sampled"}),
            output(
                "clicks",
                "Detected clicks",
                kClickDataType,
                {"detections", "waveform", "overlay"}),
            output("noise", "Noise samples", kClickNoiseDataType),
            output(
                "background",
                "Trigger background",
                kClickTriggerBackgroundDataType),
            output(
                "trigger",
                "Trigger function",
                kClickTriggerFunctionDataType),
        },
        R"({"type":"object","properties":{"channelBitmap":{"type":"integer","minimum":0},"triggerBitmap":{"type":"integer","minimum":0},"groupingType":{"type":"string","enum":["all","singles","user"]},"channelGroups":{"type":"array","items":{"type":"integer","minimum":0,"maximum":31}},"minTriggerChannels":{"type":"integer","minimum":1},"thresholdDb":{"type":"number"},"longFilter":{"type":"number","minimum":0,"maximum":1},"longFilter2":{"type":"number","minimum":0,"maximum":1},"shortFilter":{"type":"number","minimum":0,"maximum":1},"preSample":{"type":"integer","minimum":0},"postSample":{"type":"integer","minimum":0},"minSep":{"type":"integer","minimum":0},"maxLength":{"type":"integer","minimum":1},"sampleNoise":{"type":"boolean"},"noiseSampleIntervalSeconds":{"type":"number","exclusiveMinimum":0},"storeBackground":{"type":"boolean"},"backgroundIntervalMilliseconds":{"type":"integer","minimum":0},"publishTriggerFunction":{"type":"boolean"},"preFilter":{"$ref":"#/$defs/filter"},"triggerFilter":{"$ref":"#/$defs/filter"},"echo":{"type":"object","properties":{"runOnline":{"type":"boolean"},"discardEchoes":{"type":"boolean"},"maxIntervalSeconds":{"type":"number","minimum":0}},"required":["runOnline","discardEchoes","maxIntervalSeconds"]}},"required":["channelBitmap","triggerBitmap","groupingType","channelGroups","minTriggerChannels","thresholdDb","longFilter","shortFilter","preSample","postSample","minSep","maxLength","echo"],"$defs":{"filter":{"type":"object","properties":{"type":{"type":"string"},"band":{"type":"string"},"order":{"type":"integer","minimum":1},"lowPassFreqHz":{"type":"number"},"highPassFreqHz":{"type":"number"},"passBandRippleDb":{"type":"number"}}}}})",
        R"({"channelBitmap":3,"triggerBitmap":4294967295,"groupingType":"all","channelGroups":[],"minTriggerChannels":1,"thresholdDb":10.0,"longFilter":0.00001,"longFilter2":0.000001,"shortFilter":0.1,"preSample":40,"postSample":40,"minSep":100,"maxLength":1024,"sampleNoise":true,"noiseSampleIntervalSeconds":5.0,"storeBackground":true,"backgroundIntervalMilliseconds":5000,"publishTriggerFunction":false,"preFilter":{"type":"butterworth","band":"highPass","order":4,"lowPassFreqHz":20000,"highPassFreqHz":500,"passBandRippleDb":2.0},"triggerFilter":{"type":"butterworth","band":"highPass","order":2,"lowPassFreqHz":20000,"highPassFreqHz":2000,"passBandRippleDb":2.0},"echo":{"runOnline":false,"discardEchoes":false,"maxIntervalSeconds":0.1}})",
    });
    registry.register_type({
        "pamguard.click-features",
        "Click Features",
        "Detectors",
        "PAMGuard click length, spectra, peak, width, mean frequency, and band-energy measurements.",
        0,
        {},
        {
            input("clicks", "Detected clicks", kClickDataType),
            output("features", "Click features", kClickFeatureDataType),
        },
        R"({"type":"object","properties":{"fftLength":{"type":"integer","minimum":0},"lengthEnergyFraction":{"type":"number","minimum":0,"maximum":100},"widthEnergyFraction":{"type":"number","minimum":0,"maximum":100},"energyBandsHz":{"type":"array","items":{"type":"array","prefixItems":[{"type":"number","minimum":0},{"type":"number","minimum":0}],"minItems":2,"maxItems":2}},"peakFrequencySearchHz":{"type":"array","prefixItems":[{"type":"number","minimum":0},{"type":"number","minimum":0}],"minItems":2,"maxItems":2},"meanFrequencyRangeHz":{"type":"array","prefixItems":[{"type":"number","minimum":0},{"type":"number","minimum":0}],"minItems":2,"maxItems":2}}})",
        R"({"fftLength":0,"lengthEnergyFraction":90.0,"widthEnergyFraction":90.0,"energyBandsHz":[[1000,6000],[6000,14000]],"peakFrequencySearchHz":[500,20000],"meanFrequencyRangeHz":[500,20000]})",
    });
    registry.register_type({
        "pamguard.click-localiser",
        "Click Localiser",
        "Localisation",
        "PAMGuard correlation-delay measurement and geometry-aware far-field bearing from detected click waveforms.",
        0,
        {},
        {
            input("clicks", "Detected clicks", kClickDataType),
            output(
                "accepted",
                "Accepted localised clicks",
                kClickDataType,
                {
                    "detections",
                    "waveform",
                    "overlay",
                    "classified",
                    "localised",
                }),
            output(
                "localisations",
                "Click delay localisations",
                kClickLocalisationDataType),
            output(
                "bearings",
                "Click bearings",
                kClickBearingDataType),
        },
        R"({"type":"object","properties":{"preSample":{"type":"integer","minimum":0},"speedOfSoundMps":{"type":"number","exclusiveMinimum":0},"speedOfSoundErrorMps":{"type":"number","minimum":0},"timingErrorSeconds":{"type":"number","minimum":0},"spacingErrorM":{"type":"number","minimum":0},"wobbleRadians":{"type":"number","minimum":0},"orientation":{"type":"object","properties":{"declared":{"type":"boolean"},"headingDegrees":{"type":"number"},"pitchDegrees":{"type":"number"},"rollDegrees":{"type":"number"}}},"hydrophones":{"type":"array","items":{"type":"object","properties":{"channel":{"type":"integer","minimum":0},"xM":{"type":"number"},"yM":{"type":"number"},"zM":{"type":"number"},"streamerId":{"type":"integer"},"xErrorM":{"type":"number","minimum":0},"yErrorM":{"type":"number","minimum":0},"zErrorM":{"type":"number","minimum":0}},"required":["channel","xM","yM","zM"]}},"delayMeasurement":{"allOf":[{"$ref":"#/$defs/delay"},{"type":"object","properties":{"typeSettings":{"type":"array","items":{"allOf":[{"$ref":"#/$defs/delay"},{"type":"object","properties":{"clickType":{"type":"integer","minimum":1}},"required":["clickType"]}]}}}}]},"angleVetoes":{"type":"array","items":{"type":"object","properties":{"channels":{"type":"integer","minimum":0},"startAngleDegrees":{"type":"number"},"endAngleDegrees":{"type":"number"}},"required":["startAngleDegrees","endAngleDegrees"]}}},"required":["speedOfSoundMps","hydrophones","delayMeasurement"],"$defs":{"delay":{"type":"object","properties":{"filterBearings":{"type":"boolean"},"filterBand":{"type":"string","enum":["highPass","lowPass","bandPass","bandStop"]},"filterHighPassHz":{"type":"number","minimum":0},"filterLowPassHz":{"type":"number","minimum":0},"envelopeBearings":{"type":"boolean"},"useLeadingEdge":{"type":"boolean"},"upSample":{"type":"integer","minimum":1},"useRestrictedBins":{"type":"boolean"},"restrictedBins":{"type":"integer","minimum":1}}}}})",
        R"({"preSample":40,"speedOfSoundMps":1500,"speedOfSoundErrorMps":0,"timingErrorSeconds":0,"spacingErrorM":0,"wobbleRadians":0,"orientation":{"declared":false,"headingDegrees":0,"pitchDegrees":0,"rollDegrees":0},"hydrophones":[],"delayMeasurement":{"filterBearings":false,"filterBand":"highPass","filterHighPassHz":0,"filterLowPassHz":0,"envelopeBearings":false,"useLeadingEdge":false,"upSample":1,"useRestrictedBins":false,"restrictedBins":80,"typeSettings":[]},"angleVetoes":[]})",
    });
    registry.register_type({
        "pamguard.click-classifier",
        "Click Classifier",
        "Classifiers",
        "PAMGuard basic or sweep click classifier with an accepted-click gate and classification annotations.",
        0,
        {},
        {
            input("clicks", "Detected clicks", kClickDataType),
            output(
                "accepted",
                "Accepted clicks",
                kClickDataType,
                {
                    "detections",
                    "waveform",
                    "overlay",
                    "classified",
                }),
            output(
                "classifications",
                "Click classifications",
                kClickClassificationDataType),
        },
        R"({"type":"object","properties":{"enabled":{"type":"boolean"},"mode":{"type":"string","enum":["basic","sweep"]},"discardUnclassified":{"type":"boolean"},"checkAllClassifiers":{"type":"boolean"},"amplitudeDbOffsetByChannel":{"type":"array","items":{"type":"number"}},"types":{"type":"array","items":{"type":"object"}}},"required":["enabled","mode","types"]})",
        R"({"enabled":false,"mode":"sweep","discardUnclassified":false,"checkAllClassifiers":false,"amplitudeDbOffsetByChannel":[],"types":[]})",
    });
    registry.register_type({
        "pamguard.matched-template-classifier",
        "Matched Template Classifier",
        "Classifiers",
        "PAMGuard matched/reject waveform-template click classifier which annotates and preserves every input click.",
        0,
        {},
        {
            input(
                "clicks",
                "Detected clicks",
                kClickDataType,
                {"detections", "waveform"}),
            output(
                "accepted",
                "Classified clicks",
                kClickDataType,
                {
                    "detections",
                    "waveform",
                    "overlay",
                    "classified",
                }),
            output(
                "classifications",
                "Matched-template classifications",
                kMatchedTemplateClassificationDataType,
                {"annotations", "classified"}),
        },
        std::string(
            matched_template_settings_schema_json()),
        matched_template_runtime_settings_json(
            matched_template_default_settings()),
    });
    registry.register_type({
        "pamguard.click-train",
        "Click Train Tracker",
        "Detectors",
        "PAMGuard max-ICI click train association and summary statistics.",
        0,
        {},
        {
            input("clicks", "Detected clicks", kClickDataType),
            output("trains", "Click trains", kClickTrainDataType),
        },
        R"({"type":"object","properties":{"enabled":{"type":"boolean"},"minIciSeconds":{"type":"number","exclusiveMinimum":0},"maxIciSeconds":{"type":"number","exclusiveMinimum":0},"maxIciChange":{"type":"number","minimum":1},"okAngleErrorDegrees":{"type":"number","minimum":0},"initialPerpendicularDistanceM":{"type":"number","minimum":0},"minClicks":{"type":"integer","minimum":1},"minAngleChangeDegrees":{"type":"number","minimum":0},"iciUpdateRatio":{"type":"number","minimum":0,"maximum":1},"minUpdateGapSeconds":{"type":"number","minimum":0}},"required":["enabled","minIciSeconds","maxIciSeconds","maxIciChange","okAngleErrorDegrees","initialPerpendicularDistanceM","minClicks","minAngleChangeDegrees","iciUpdateRatio","minUpdateGapSeconds"]})",
        R"({"enabled":false,"minIciSeconds":0.1,"maxIciSeconds":2.0,"maxIciChange":1.2,"okAngleErrorDegrees":1.0,"initialPerpendicularDistanceM":100.0,"minClicks":6,"minAngleChangeDegrees":5.0,"iciUpdateRatio":0.5,"minUpdateGapSeconds":5.0})",
    });
    registry.register_type({
        "pamguard.mht-click-train",
        "MHT Click Train Detector",
        "Detectors",
        "PAMGuard multiple-hypothesis click-train detector and classifier chain with optional feature, delay, and bearing inputs.",
        0,
        {},
        {
            input("clicks", "Detected clicks", kClickDataType),
            optional_input(
                "features",
                "Click features",
                kClickFeatureDataType),
            optional_input(
                "localisations",
                "Click localisations",
                kClickLocalisationDataType),
            optional_input(
                "bearings",
                "Click bearings",
                kClickBearingDataType),
            output(
                "trains",
                "MHT click trains",
                kMhtClickTrainDataType,
                {"clip-trigger"}),
            output(
                "classifications",
                "Click-train classifications",
                kClickTrainClassificationDataType,
                {"clip-trigger"}),
        },
        R"({"type":"object","properties":{"minClicks":{"type":"integer","minimum":1},"kernel":{"type":"object","properties":{"nHold":{"type":"integer","minimum":1},"nPruneback":{"type":"integer","minimum":0},"nPrunebackStart":{"type":"integer","minimum":0},"maxCoast":{"type":"integer","minimum":0}}},"chi2":{"type":"object","properties":{"enableIdi":{"type":"boolean"},"enableAmplitude":{"type":"boolean"},"enableLength":{"type":"boolean"},"enableBearing":{"type":"boolean"},"enablePeakFrequency":{"type":"boolean"},"enableTimeDelay":{"type":"boolean"},"enableCorrelation":{"type":"boolean"},"correlationFftLength":{"type":"integer","minimum":2},"coastPenalty":{"type":"number"},"newTrackPenalty":{"type":"number"},"newTrackN":{"type":"integer","minimum":0},"maxIciSeconds":{"type":"number","exclusiveMinimum":0},"lowIciExponent":{"type":"number"},"longTrackExponent":{"type":"number"},"junkTrackPenalty":{"type":"number"},"maxChi":{"type":"number"},"useElectricalNoiseFilter":{"type":"boolean"},"electricalNoiseMinChi2":{"type":"number"},"electricalNoiseNDataUnits":{"type":"integer","minimum":1}}},"classifier":{"type":"object","properties":{"enabled":{"type":"boolean"},"averageSpectrumFftLength":{"type":"integer","minimum":1},"pre":{"type":"object"},"idi":{"type":"object"},"bearing":{"type":"object"},"template":{"type":"object"}}}},"required":["minClicks","kernel","chi2","classifier"]})",
        R"({"minClicks":3,"kernel":{"nHold":20,"nPruneback":4,"nPrunebackStart":5,"maxCoast":3},"chi2":{"enableIdi":true,"enableAmplitude":true,"enableLength":true,"enableBearing":false,"enablePeakFrequency":false,"enableTimeDelay":false,"enableCorrelation":false,"correlationFftLength":512,"coastPenalty":10,"newTrackPenalty":50,"newTrackN":3,"maxIciSeconds":0.4,"lowIciExponent":0.1,"longTrackExponent":0.1,"junkTrackPenalty":20000000,"maxChi":200000000000000000,"useElectricalNoiseFilter":false,"electricalNoiseMinChi2":0.00001,"electricalNoiseNDataUnits":30},"classifier":{"enabled":false,"averageSpectrumFftLength":256,"pre":{"chi2Threshold":1500,"minClicks":5,"minTimeSeconds":0,"speciesFlag":1},"idi":{"enabled":false,"useMedianIdi":true,"minMedianIdi":0,"maxMedianIdi":2,"useMeanIdi":false,"minMeanIdi":0,"maxMeanIdi":2,"useStdIdi":false,"minStdIdi":0,"maxStdIdi":100,"speciesFlag":1},"bearing":{"enabled":false,"bearingLimitMinRadians":1.4835298641951802,"bearingLimitMaxRadians":1.6580627893946132,"useMean":false,"minMeanDerivative":-0.00008726646259971647,"maxMeanDerivative":0.00008726646259971647,"useMedian":true,"minMedianDerivative":-0.00008726646259971647,"maxMedianDerivative":0.00008726646259971647,"useStd":true,"minStdDerivative":0,"maxStdDerivative":0.02617993877991494,"speciesFlag":-1},"template":{"enabled":false,"templateSpectrum":[],"templateSampleRateHz":0,"correlationThreshold":0.5,"speciesFlag":1}}})",
    });
    registry.register_type({
        "pamguard.noise-band-monitor",
        "Noise Band Monitor",
        "Sound Processing",
        "PAMGuard octave-family band RMS and peak levels from selectable raw audio.",
        0,
        {},
        {
            input(
                "input",
                "Raw audio source",
                kRawAudioDataType,
                {"sampled"}),
            output(
                "measurements",
                "Band noise measurements",
                kNoiseBandDataType,
                {"measurements"}),
        },
        R"({"type":"object","properties":{"channelBitmap":{"type":"integer","minimum":0,"maximum":4294967295},"bandType":{"type":"string","enum":["octave","thirdOctave","decidecade","decade","tenthOctave","twelfthOctave"]},"filterType":{"type":"string","enum":["butterworth","firWindow"]},"minimumFrequencyHz":{"type":"number","exclusiveMinimum":0},"maximumFrequencyHz":{"type":"number","exclusiveMinimum":0},"referenceFrequencyHz":{"type":"number","exclusiveMinimum":0},"iirOrder":{"type":"integer","minimum":2,"maximum":20,"multipleOf":2},"firOrder":{"type":"integer","minimum":2,"maximum":20},"firGamma":{"type":"number","exclusiveMinimum":0},"outputIntervalSeconds":{"type":"number","exclusiveMinimum":0},"calibrationDbOffsetByChannel":{"type":"array","items":{"type":"number"}}},"required":["channelBitmap","bandType","filterType","minimumFrequencyHz","maximumFrequencyHz","referenceFrequencyHz","iirOrder","firOrder","firGamma","outputIntervalSeconds"]})",
        R"({"channelBitmap":1,"bandType":"thirdOctave","filterType":"butterworth","minimumFrequencyHz":1.7925856629456591,"maximumFrequencyHz":1133.6866687924667,"referenceFrequencyHz":1000,"iirOrder":6,"firOrder":7,"firGamma":2.5,"outputIntervalSeconds":10,"calibrationDbOffsetByChannel":[]})",
    });
    registry.register_type({
        "pamguard.fft-noise-monitor",
        "Noise Monitor",
        "Sound Processing",
        "PAMGuard FFT-band interval statistics from a selectable FFT source.",
        0,
        {},
        {
            input(
                "input",
                "FFT source",
                kFftDataType,
                {"frequency-domain"}),
            output(
                "measurements",
                "Noise measurements",
                kFftNoiseDataType,
                {"measurements"}),
        },
        R"({"type":"object","properties":{"fftLength":{"type":"integer","minimum":2},"fftHop":{"type":"integer","minimum":1},"channels":{"type":"array","items":{"type":"integer","minimum":0}},"measurementIntervalSeconds":{"type":"integer","minimum":1},"nMeasures":{"type":"integer","minimum":1},"useAll":{"type":"boolean"},"bands":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"lowFrequencyHz":{"type":"number","minimum":0},"highFrequencyHz":{"type":"number","exclusiveMinimum":0}},"required":["name","lowFrequencyHz","highFrequencyHz"]}}},"required":["channels","measurementIntervalSeconds","nMeasures","useAll","bands"]})",
        R"({"channels":[0],"measurementIntervalSeconds":60,"nMeasures":100,"useAll":true,"bands":[]})",
    });
    registry.register_type({
        "pamguard.ltsa",
        "Long Term Spectral Average",
        "Sound Processing",
        "PAMGuard LTSA period averaging from any compatible FFT block.",
        0,
        {},
        {
            input(
                "input",
                "FFT source",
                kFftDataType,
                {"frequency-domain"}),
            output(
                "ltsa",
                "LTSA periods",
                kLtsaDataType,
                {"frequency-domain", "measurements"}),
        },
        R"({"type":"object","properties":{"channelBitmap":{"type":"integer","minimum":0,"maximum":4294967295},"intervalSeconds":{"type":"integer","minimum":1}},"required":["channelBitmap","intervalSeconds"]})",
        R"({"channelBitmap":1,"intervalSeconds":60})",
    });
    registry.register_type({
        "pamguard.ishmael-energy-sum",
        "Ishmael Energy Sum",
        "Detectors",
        "PAMGuard Ishmael FFT energy-sum detection function and peak picker.",
        0,
        {},
        {
            input(
                "input",
                "FFT source",
                kFftDataType,
                {"frequency-domain"}),
            output(
                "function",
                "Detection function",
                kIshmaelFunctionDataType,
                {"timeseries"}),
            output(
                "detections",
                "Ishmael detections",
                kIshmaelDetectionDataType,
                {"detections", "overlay", "clip-trigger"}),
        },
        std::string(
            ishmael_energy_sum_runtime_schema_json()),
        ishmael_energy_sum_runtime_default_settings_json(),
    });
    registry.register_type({
        "pamguard.whistles-moans",
        "Whistle Tone Connect Process",
        "Detectors",
        "PAMGuard WhistleToneConnectProcess connected-region, stub-removal, fragmentation, and rejoining runtime.",
        0,
        {},
        {
            input(
                "input",
                "FFT source",
                kFftDataType,
                {"frequency-domain"}),
            output(
                "contours",
                "Whistle/moan contours",
                kWhistleContourDataType,
                {"detections", "overlay", "clip-trigger"}),
        },
        std::string(
            whistle_moan_contour_runtime_schema_json()),
        whistle_moan_contour_runtime_default_settings_json(),
    });
    registry.register_type({
        "pamguard.ishmael-sgram-corr",
        "Ishmael Spectrogram Correlation",
        "Detectors",
        "PAMGuard Mellinger-Clark spectrogram-correlation detection function and peak picker.",
        0,
        {},
        {
            input("input", "FFT source", kFftDataType, {"frequency-domain"}),
            output(
                "function",
                "Detection function",
                kIshmaelFunctionDataType,
                {"timeseries"}),
            output(
                "detections",
                "Ishmael detections",
                kIshmaelDetectionDataType,
                {"detections", "overlay", "clip-trigger"}),
        },
        std::string(
            ishmael_sgram_corr_runtime_schema_json()),
        ishmael_sgram_corr_runtime_default_settings_json(),
    });
    registry.register_type({
        "pamguard.ishmael-match-filter",
        "Ishmael Matched Filter",
        "Detectors",
        "PAMGuard raw-audio overlap-save normalized matched-filter detection function and peak picker.",
        0,
        {},
        {
            input("input", "Raw audio source", kRawAudioDataType, {"sampled"}),
            output(
                "function",
                "Detection function",
                kIshmaelFunctionDataType,
                {"timeseries"}),
            output(
                "detections",
                "Ishmael detections",
                kIshmaelDetectionDataType,
                {"detections", "overlay", "clip-trigger"}),
        },
        std::string(
            ishmael_match_filter_runtime_schema_json()),
        ishmael_match_filter_runtime_default_settings_json(),
    });
    registry.register_type({
        "pamguard.level-meter",
        "Level Meter",
        "Operator support",
        "Interval RMS and peak measurements for selectable raw-audio channels.",
        0,
        {},
        {
            input("input", "Raw audio source", kRawAudioDataType, {"sampled"}),
            output(
                "levels",
                "Level measurements",
                kLevelMeasurementDataType,
                {"timeseries", "monitoring"}),
        },
        R"({"type":"object","properties":{"intervalSeconds":{"type":"number","exclusiveMinimum":0},"channelBitmap":{"type":"integer","minimum":0}},"required":["intervalSeconds"]})",
        R"({"intervalSeconds":0.25,"channelBitmap":4294967295})",
        {"live", "offline"},
        {"level", "timeplot"},
        "implemented",
        "PAMGuard-semantics",
    });
    registry.register_type({
        "pamguard.sound-recorder",
        "Sound Recorder",
        "Operator support",
        "Continuous or segmented IEEE-float WAV recording from any raw-audio branch.",
        0,
        {},
        {
            input("input", "Raw audio source", kRawAudioDataType, {"sampled"}),
            output(
                "recordings",
                "Recording events",
                kRecordingEventDataType,
                {"events", "recordings"}),
        },
        R"({"type":"object","properties":{"directory":{"type":"string","minLength":1},"filePrefix":{"type":"string","minLength":1},"segmentSeconds":{"type":"number","minimum":0}},"required":["directory","filePrefix","segmentSeconds"]})",
        R"({"directory":"recordings","filePrefix":"pamguard","segmentSeconds":300})",
        {"live", "offline"},
        {"events"},
        "implemented",
        "web-adapted",
    });
    registry.register_type({
        "pamguard.clip-generator",
        "Clip Generator",
        "Operator support",
        "Creates pre/post-trigger raw-audio clips from click detections without disturbing the source stream.",
        0,
        {},
        {
            input("audio", "Raw audio source", kRawAudioDataType, {"sampled"}),
            optional_multi_input(
                "triggers",
                "Clip trigger sources",
                "pamguard.acoustic-data-unit",
                {"clip-trigger"}),
            output(
                "clips",
                "Detection-triggered clips",
                kAudioClipDataType,
                {"events", "waveform", "playable"}),
        },
        R"({"type":"object","additionalProperties":false,"properties":{"storageMode":{"type":"string","enum":["wav-files","binary","both"]},"datedSubFolders":{"type":"boolean"},"requiredHistorySeconds":{"type":"number","minimum":0},"triggerPolicies":{"type":"array","items":{"type":"object"}}},"required":["storageMode","datedSubFolders","requiredHistorySeconds","triggerPolicies"]})",
        R"({"storageMode":"binary","datedSubFolders":true,"requiredHistorySeconds":0,"triggerPolicies":[]})",
        {"live", "offline"},
        {"events", "waveform"},
        "implemented",
        "PAMGuard-semantics",
    });
    registry.register_type({
        "pamguard.alarm-event-counter",
        "Alarm and Event Counter",
        "Operator support",
        "Counts click detections in a rolling time window and publishes explicit alarm state.",
        0,
        {},
        {
            input("input", "Click detections", kClickDataType, {"detections"}),
            output(
                "alarms",
                "Alarm states",
                kAlarmStateDataType,
                {"events", "monitoring"}),
        },
        R"({"type":"object","properties":{"countThreshold":{"type":"integer","minimum":1},"windowSeconds":{"type":"number","exclusiveMinimum":0},"message":{"type":"string"}},"required":["countThreshold","windowSeconds"]})",
        R"({"countThreshold":5,"windowSeconds":10,"message":"Click-rate alarm"})",
        {"live", "offline"},
        {"events", "status"},
        "implemented",
        "PAMGuard-semantics",
    });
    for (const auto& operator_type : std::vector<std::array<std::string, 4>>{
             {"pamguard.effort-monitor", "Effort Monitor", "effort",
              "Operator effort on/off and status observations."},
             {"pamguard.aural-listening", "Aural Listening", "listening",
              "Time-stamped operator listening and species observations."},
             {"pamguard.user-input", "User Input", "annotation",
              "General time-stamped operator annotations."},
         }) {
        registry.register_type({
            operator_type[0],
            operator_type[1],
            "Operator support",
            operator_type[3],
            0,
            {},
            {
                output(
                    "events",
                    "Operator events",
                    kOperatorEventDataType,
                    {"events", "annotations"}),
            },
            R"({"type":"object","properties":{"defaultCategory":{"type":"string"}}})",
            std::string("{\"defaultCategory\":\"") +
                operator_type[2] + "\"}",
            {"live", "offline"},
            {"events"},
            "implemented",
            "web-adapted",
        });
    }
    registry.register_type({
        "pamguard.storage-health",
        "Backup / Storage Health",
        "Operator support",
        "Publishes disk-capacity state for recording and backup destinations.",
        0,
        {},
        {
            output(
                "status",
                "Storage status",
                kStorageHealthDataType,
                {"status", "monitoring"}),
        },
        R"({"type":"object","properties":{"path":{"type":"string","minLength":1},"warningFreePercent":{"type":"number","minimum":0,"maximum":100},"intervalSeconds":{"type":"number","exclusiveMinimum":0}},"required":["path","warningFreePercent","intervalSeconds"]})",
        R"({"path":".","warningFreePercent":10,"intervalSeconds":30})",
        {"live", "offline"},
        {"status", "datamap"},
        "implemented",
        "web-adapted",
    });
    registry.register_type({
        "pamguard.spectrogram-display",
        "Spectrogram",
        "Displays",
        "Independently sourced spectrogram display with operator-selected axes and overlays.",
        0,
        {},
        {
            input("fft", "FFT source", kFftDataType, {"frequency-domain"}),
        },
        R"({"type":"object","properties":{"minimumFrequencyHz":{"type":"number","minimum":0},"maximumFrequencyHz":{"type":"number","minimum":0},"minimumAmplitudeDb":{"type":"number"},"maximumAmplitudeDb":{"type":"number"},"timeWindowSeconds":{"type":"number","exclusiveMinimum":0},"overlays":{"type":"array","items":{"type":"string"}}}})",
        R"({"minimumFrequencyHz":0,"maximumFrequencyHz":24000,"minimumAmplitudeDb":-120,"maximumAmplitudeDb":0,"timeWindowSeconds":10,"overlays":[]})",
        {"live", "offline"},
        {"spectrogram"},
        "implemented",
        "browser-validated",
    });
    registry.register_type({
        "pamguard.click-display",
        "Click Display",
        "Displays",
        "Operator click history display with selectable channels, time window, and PAMGuard-style bearing, amplitude, and ICI ranges.",
        0,
        {},
        {
            input(
                "clicks",
                "Click source",
                kClickDataType,
                {"detections", "waveform", "overlay"}),
        },
        R"({"type":"object","properties":{"timeWindowSeconds":{"type":"number","exclusiveMinimum":0},"channelBitmap":{"type":"integer","minimum":0},"bearingLimitsDegrees":{"type":"array","prefixItems":[{"type":"number"},{"type":"number"}],"minItems":2,"maxItems":2},"amplitudeLimitsDb":{"type":"array","prefixItems":[{"type":"number"},{"type":"number"}],"minItems":2,"maxItems":2},"iciLimitsSeconds":{"type":"array","prefixItems":[{"type":"number","minimum":0},{"type":"number","minimum":0}],"minItems":2,"maxItems":2},"showEchoes":{"type":"boolean"}},"required":["timeWindowSeconds","channelBitmap","bearingLimitsDegrees","amplitudeLimitsDb","iciLimitsSeconds","showEchoes"]})",
        R"({"timeWindowSeconds":20,"channelBitmap":3,"bearingLimitsDegrees":[0,180],"amplitudeLimitsDb":[0,30],"iciLimitsSeconds":[0.001,3],"showEchoes":true})",
        {"live", "offline"},
        {"clicks"},
        "implemented",
        "browser-validated",
    });
    registry.register_type({
        "pamguard.level-meter-display",
        "Level Meter Display",
        "Displays",
        "Browser presentation adapter for PAMGuard Level Meter measurements.",
        0,
        {},
        {
            input(
                "levels",
                "Level measurements",
                kLevelMeasurementDataType,
                {"timeseries", "monitoring"}),
        },
        R"({"type":"object","additionalProperties":false,"properties":{}})",
        R"({})",
        {"live", "offline"},
        {"level"},
        "implemented",
        "browser-validated",
    });
    registry.register_type({
        "pamguard.sound-output",
        "Sound Output",
        "Output",
        "Low-latency monitoring of an operator-selected raw-audio block.",
        0,
        {},
        {
            input("audio", "Audio source", kRawAudioDataType, {"sampled"}),
        },
        R"({"type":"object","properties":{"channelBitmap":{"type":"integer","minimum":0},"mix":{"type":"string","enum":["direct","mono","stereo"]},"deviceId":{"type":"string"},"gain":{"type":"number","minimum":0},"muted":{"type":"boolean"},"highPassHz":{"type":"number","minimum":0},"outputRateHz":{"type":"number","minimum":0},"latencyMs":{"type":"number","minimum":20,"maximum":2000}}})",
        R"({"channelBitmap":1,"mix":"direct","deviceId":"default","gain":1.0,"muted":false,"highPassHz":0,"outputRateHz":0,"latencyMs":100})",
    });
}

} // namespace pamguard::core
