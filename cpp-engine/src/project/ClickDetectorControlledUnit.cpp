#include "pamguard/project/ClickDetectorControlledUnit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <utility>

#include <json.hpp>

namespace pamguard::project {

namespace {

using Json = nlohmann::json;

constexpr std::uint64_t kMaximumBitmap =
    std::numeric_limits<std::uint32_t>::max();

Json parse_object(std::string_view text, const char* context) {
    Json value;
    try {
        value = Json::parse(text);
    }
    catch (const std::exception& error) {
        throw ClickDetectorSettingsError(
            std::string(context) + " is not valid JSON: " + error.what());
    }
    if (!value.is_object()) {
        throw ClickDetectorSettingsError(
            std::string(context) + " must be a JSON object");
    }
    return value;
}

void require_exact(
    const Json& value,
    std::initializer_list<std::string_view> fields,
    const std::string& context) {
    if (!value.is_object() || value.size() != fields.size()) {
        throw ClickDetectorSettingsError(
            context + " contains missing or unknown fields");
    }
    for (const auto field : fields) {
        if (!value.contains(std::string(field))) {
            throw ClickDetectorSettingsError(
                context + " omits '" + std::string(field) + "'");
        }
    }
}

void require_boolean(const Json& value, const std::string& context) {
    if (!value.is_boolean()) {
        throw ClickDetectorSettingsError(context + " must be boolean");
    }
}

double finite_number(const Json& value, const std::string& context) {
    if (!value.is_number()) {
        throw ClickDetectorSettingsError(context + " must be a number");
    }
    const auto number = value.get<double>();
    if (!std::isfinite(number)) {
        throw ClickDetectorSettingsError(context + " must be finite");
    }
    return number;
}

std::uint64_t unsigned_integer(
    const Json& value,
    const std::string& context) {
    if (!value.is_number_integer()) {
        throw ClickDetectorSettingsError(context + " must be an integer");
    }
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    const auto number = value.get<std::int64_t>();
    if (number < 0) {
        throw ClickDetectorSettingsError(
            context + " must be non-negative");
    }
    return static_cast<std::uint64_t>(number);
}

void require_range(
    double value,
    double minimum,
    double maximum,
    const std::string& context) {
    if (value < minimum || value > maximum) {
        throw ClickDetectorSettingsError(
            context + " is outside its supported range");
    }
}

void validate_pair(
    const Json& value,
    const std::string& context,
    bool positive = false) {
    if (!value.is_array() || value.size() != 2) {
        throw ClickDetectorSettingsError(
            context + " must contain exactly two numbers");
    }
    const auto low = finite_number(value.at(0), context);
    const auto high = finite_number(value.at(1), context);
    if (low > high || (positive && low <= 0.0)) {
        throw ClickDetectorSettingsError(
            context + " must be ordered" +
            (positive ? " and positive" : ""));
    }
}

std::pair<double, double> validate_classifier_pair(
    const Json& value,
    const std::string& context,
    bool non_negative = false,
    bool integer = false,
    bool ordered = true) {
    if (!value.is_array() || value.size() != 2) {
        throw ClickDetectorSettingsError(
            context + " must contain exactly two numbers");
    }
    double low = 0.0;
    double high = 0.0;
    if (integer) {
        if (!value.at(0).is_number_integer() ||
            !value.at(1).is_number_integer()) {
            throw ClickDetectorSettingsError(
                context + " values must be integers");
        }
        low = value.at(0).get<double>();
        high = value.at(1).get<double>();
    }
    else {
        low = finite_number(value.at(0), context + " minimum");
        high = finite_number(value.at(1), context + " maximum");
    }
    if ((ordered && low > high) ||
        (non_negative && (low < 0.0 || high < 0.0))) {
        throw ClickDetectorSettingsError(
            context +
            (ordered ? " must be ordered" : "") +
            (ordered && non_negative ? " and" : "") +
            (non_negative ? " must be non-negative" : ""));
    }
    return {low, high};
}

void require_strict_classifier_range(
    const std::pair<double, double>& range,
    const std::string& context) {
    if (range.first >= range.second) {
        throw ClickDetectorSettingsError(
            context + " maximum must be greater than its minimum");
    }
}

void require_string_enum(
    const Json& value,
    std::initializer_list<std::string_view> values,
    const std::string& context) {
    if (!value.is_string()) {
        throw ClickDetectorSettingsError(context + " must be a string");
    }
    const auto token = value.get<std::string>();
    if (std::none_of(
            values.begin(),
            values.end(),
            [&](const auto candidate) { return candidate == token; })) {
        throw ClickDetectorSettingsError(context + " is unsupported");
    }
}

std::uint64_t validate_classifier_species_code(
    const Json& value,
    const std::string& context) {
    const auto code = unsigned_integer(value, context);
    if (code == 0 || code > 255) {
        throw ClickDetectorSettingsError(
            context + " must be in 1..255");
    }
    return code;
}

std::uint64_t validate_basic_classifier_type(
    const Json& value,
    const std::string& context) {
    require_exact(
        value,
        {
            "name",
            "speciesCode",
            "enabled",
            "discard",
            "whichSelections",
            "band1FreqHz",
            "band2FreqHz",
            "band1EnergyDb",
            "band2EnergyDb",
            "bandEnergyDifferenceDb",
            "peakFrequencySearchHz",
            "peakFrequencyRangeHz",
            "peakWidthHz",
            "widthEnergyFraction",
            "meanSumRangeHz",
            "meanSelectionRangeHz",
            "clickLengthMs",
            "lengthEnergyFraction",
        },
        context);
    if (!value.at("name").is_string() ||
        value.at("name").get_ref<const std::string&>().size() > 128) {
        throw ClickDetectorSettingsError(
            context + " name must be a string of at most 128 characters");
    }
    const auto species_code = validate_classifier_species_code(
        value.at("speciesCode"),
        context + " speciesCode");
    require_boolean(value.at("enabled"), context + " enabled");
    require_boolean(value.at("discard"), context + " discard");
    const auto selections = unsigned_integer(
        value.at("whichSelections"),
        context + " whichSelections");
    if (selections > 0x1f) {
        throw ClickDetectorSettingsError(
            context + " whichSelections contains unsupported criterion bits");
    }
    (void) validate_classifier_pair(
        value.at("band1FreqHz"),
        context + " band1FreqHz",
        false,
        false,
        false);
    (void) validate_classifier_pair(
        value.at("band2FreqHz"),
        context + " band2FreqHz",
        false,
        false,
        false);
    (void) validate_classifier_pair(
        value.at("band1EnergyDb"),
        context + " band1EnergyDb",
        false,
        false,
        false);
    (void) validate_classifier_pair(
        value.at("band2EnergyDb"),
        context + " band2EnergyDb",
        false,
        false,
        false);
    (void) finite_number(
        value.at("bandEnergyDifferenceDb"),
        context + " bandEnergyDifferenceDb");
    (void) validate_classifier_pair(
        value.at("peakFrequencySearchHz"),
        context + " peakFrequencySearchHz",
        false,
        false,
        false);
    (void) validate_classifier_pair(
        value.at("peakFrequencyRangeHz"),
        context + " peakFrequencyRangeHz",
        false,
        false,
        false);
    (void) validate_classifier_pair(
        value.at("peakWidthHz"),
        context + " peakWidthHz",
        false,
        false,
        false);
    (void) finite_number(
        value.at("widthEnergyFraction"),
        context + " widthEnergyFraction");
    (void) validate_classifier_pair(
        value.at("meanSumRangeHz"),
        context + " meanSumRangeHz",
        false,
        false,
        false);
    (void) validate_classifier_pair(
        value.at("meanSelectionRangeHz"),
        context + " meanSelectionRangeHz",
        false,
        false,
        false);
    (void) validate_classifier_pair(
        value.at("clickLengthMs"),
        context + " clickLengthMs",
        false,
        false,
        false);
    (void) finite_number(
        value.at("lengthEnergyFraction"),
        context + " lengthEnergyFraction");
    return species_code;
}

std::uint64_t validate_sweep_classifier_type(
    const Json& value,
    const std::string& context) {
    require_exact(
        value,
        {
            "name",
            "speciesCode",
            "discard",
            "enabled",
            "channelChoice",
            "restrictLength",
            "restrictedBins",
            "restrictedBinType",
            "enableLength",
            "lengthSmoothing",
            "lengthDb",
            "lengthMs",
            "enableEnergyBands",
            "testEnergyBandHz",
            "controlEnergyBand0Hz",
            "controlEnergyBand1Hz",
            "energyThreshold0Db",
            "energyThreshold1Db",
            "testAmplitude",
            "amplitudeRangeDb",
            "enableFftFilter",
            "fftFilter",
            "enablePeak",
            "enableWidth",
            "enableMean",
            "peakSearchRangeHz",
            "peakRangeHz",
            "peakWidthRangeHz",
            "meanRangeHz",
            "peakSmoothing",
            "peakWidthThresholdDb",
            "enableZeroCrossings",
            "zeroCrossingCount",
            "enableSweep",
            "zeroCrossingSweepKhzPerMs",
            "enableMinCrossCorrelation",
            "enablePeakCrossCorrelation",
            "minCorrelation",
            "correlationFactor",
            "enableBearingLimits",
            "excludeBearingLimits",
            "bearingLimitsRadians",
        },
        context);
    if (!value.at("name").is_string() ||
        value.at("name").get_ref<const std::string&>().empty() ||
        value.at("name").get_ref<const std::string&>().size() > 128) {
        throw ClickDetectorSettingsError(
            context + " name must be a non-empty string of at most 128 "
            "characters");
    }
    const auto species_code = validate_classifier_species_code(
        value.at("speciesCode"),
        context + " speciesCode");
    for (const auto field : {
             "discard",
             "enabled",
             "restrictLength",
             "enableLength",
             "enableEnergyBands",
             "testAmplitude",
             "enableFftFilter",
             "enablePeak",
             "enableWidth",
             "enableMean",
             "enableZeroCrossings",
             "enableSweep",
             "enableMinCrossCorrelation",
             "enablePeakCrossCorrelation",
             "enableBearingLimits",
             "excludeBearingLimits",
         }) {
        require_boolean(value.at(field), context + " " + field);
    }
    require_string_enum(
        value.at("channelChoice"),
        {"requireAll", "requireOne", "useMeans"},
        context + " channelChoice");
    require_string_enum(
        value.at("restrictedBinType"),
        {"clickCenter", "clickStart"},
        context + " restrictedBinType");
    if (unsigned_integer(
            value.at("restrictedBins"),
            context + " restrictedBins") == 0) {
        throw ClickDetectorSettingsError(
            context + " restrictedBins must be positive");
    }
    const auto length_smoothing = unsigned_integer(
        value.at("lengthSmoothing"),
        context + " lengthSmoothing");
    if (length_smoothing == 0 || length_smoothing % 2 == 0) {
        throw ClickDetectorSettingsError(
            context + " lengthSmoothing must be positive and odd");
    }
    if (finite_number(
            value.at("lengthDb"),
            context + " lengthDb") == 0.0) {
        throw ClickDetectorSettingsError(
            context + " lengthDb cannot be zero");
    }
    const auto length = validate_classifier_pair(
        value.at("lengthMs"),
        context + " lengthMs",
        false,
        false,
        false);
    if (value.at("enableLength").get<bool>()) {
        require_strict_classifier_range(length, context + " lengthMs");
    }

    const auto test_energy = validate_classifier_pair(
        value.at("testEnergyBandHz"),
        context + " testEnergyBandHz",
        false,
        false,
        false);
    const auto control_energy_0 = validate_classifier_pair(
        value.at("controlEnergyBand0Hz"),
        context + " controlEnergyBand0Hz",
        false,
        false,
        false);
    const auto control_energy_1 = validate_classifier_pair(
        value.at("controlEnergyBand1Hz"),
        context + " controlEnergyBand1Hz",
        false,
        false,
        false);
    (void) finite_number(
        value.at("energyThreshold0Db"),
        context + " energyThreshold0Db");
    (void) finite_number(
        value.at("energyThreshold1Db"),
        context + " energyThreshold1Db");
    if (value.at("enableEnergyBands").get<bool>()) {
        require_strict_classifier_range(
            test_energy,
            context + " testEnergyBandHz");
        require_strict_classifier_range(
            control_energy_0,
            context + " controlEnergyBand0Hz");
        require_strict_classifier_range(
            control_energy_1,
            context + " controlEnergyBand1Hz");
    }

    const auto amplitude = validate_classifier_pair(
        value.at("amplitudeRangeDb"),
        context + " amplitudeRangeDb",
        false,
        false,
        false);
    if (value.at("testAmplitude").get<bool>()) {
        require_strict_classifier_range(
            amplitude,
            context + " amplitudeRangeDb");
    }

    const auto& fft_filter = value.at("fftFilter");
    require_exact(
        fft_filter,
        {"band", "lowPassFreqHz", "highPassFreqHz"},
        context + " fftFilter");
    require_string_enum(
        fft_filter.at("band"),
        {"highPass", "lowPass", "bandPass", "bandStop"},
        context + " fftFilter band");
    const auto fft_low_pass = finite_number(
        fft_filter.at("lowPassFreqHz"),
        context + " fftFilter lowPassFreqHz");
    const auto fft_high_pass = finite_number(
        fft_filter.at("highPassFreqHz"),
        context + " fftFilter highPassFreqHz");
    (void) fft_low_pass;
    (void) fft_high_pass;

    const auto peak_search = validate_classifier_pair(
        value.at("peakSearchRangeHz"),
        context + " peakSearchRangeHz",
        false,
        false,
        false);
    const auto peak = validate_classifier_pair(
        value.at("peakRangeHz"),
        context + " peakRangeHz",
        false,
        false,
        false);
    const auto width = validate_classifier_pair(
        value.at("peakWidthRangeHz"),
        context + " peakWidthRangeHz",
        false,
        false,
        false);
    const auto mean = validate_classifier_pair(
        value.at("meanRangeHz"),
        context + " meanRangeHz",
        false,
        false,
        false);
    const bool enable_peak = value.at("enablePeak").get<bool>();
    const bool enable_width = value.at("enableWidth").get<bool>();
    const bool enable_mean = value.at("enableMean").get<bool>();
    if (enable_peak || enable_width || enable_mean) {
        const auto peak_smoothing = unsigned_integer(
            value.at("peakSmoothing"),
            context + " peakSmoothing");
        if (peak_smoothing == 0 || peak_smoothing % 2 == 0) {
            throw ClickDetectorSettingsError(
                context + " peakSmoothing must be positive and odd");
        }
        require_strict_classifier_range(
            peak_search,
            context + " peakSearchRangeHz");
    }
    else if (!value.at("peakSmoothing").is_number_integer()) {
        throw ClickDetectorSettingsError(
            context + " peakSmoothing must be an integer");
    }
    if (enable_peak) {
        require_strict_classifier_range(peak, context + " peakRangeHz");
    }
    if (enable_width) {
        if (finite_number(
                value.at("peakWidthThresholdDb"),
                context + " peakWidthThresholdDb") == 0.0) {
            throw ClickDetectorSettingsError(
                context + " peakWidthThresholdDb cannot be zero");
        }
        require_strict_classifier_range(
            width,
            context + " peakWidthRangeHz");
    }
    else {
        (void) finite_number(
            value.at("peakWidthThresholdDb"),
            context + " peakWidthThresholdDb");
    }
    if (enable_mean) {
        require_strict_classifier_range(mean, context + " meanRangeHz");
    }

    const auto zero_count = validate_classifier_pair(
        value.at("zeroCrossingCount"),
        context + " zeroCrossingCount",
        false,
        true,
        false);
    const auto zero_sweep = validate_classifier_pair(
        value.at("zeroCrossingSweepKhzPerMs"),
        context + " zeroCrossingSweepKhzPerMs",
        false,
        false,
        false);
    if (value.at("enableZeroCrossings").get<bool>()) {
        require_strict_classifier_range(
            zero_count,
            context + " zeroCrossingCount");
    }
    if (value.at("enableSweep").get<bool>()) {
        require_strict_classifier_range(
            zero_sweep,
            context + " zeroCrossingSweepKhzPerMs");
    }

    (void) finite_number(
        value.at("minCorrelation"),
        context + " minCorrelation");
    (void) finite_number(
        value.at("correlationFactor"),
        context + " correlationFactor");
    const auto bearings = validate_classifier_pair(
        value.at("bearingLimitsRadians"),
        context + " bearingLimitsRadians",
        false,
        false,
        false);
    (void) bearings;
    return species_code;
}

void validate_filter(const Json& value, const std::string& context) {
    require_exact(
        value,
        {
            "type",
            "band",
            "order",
            "lowPassFreqHz",
            "highPassFreqHz",
            "passBandRippleDb",
        },
        context);
    if (!value.at("type").is_string() ||
        !value.at("band").is_string()) {
        throw ClickDetectorSettingsError(
            context + " type and band must be strings");
    }
    const auto order = unsigned_integer(value.at("order"), context + " order");
    if (order == 0 || order > 64) {
        throw ClickDetectorSettingsError(
            context + " order must be in 1..64");
    }
    if (finite_number(
            value.at("lowPassFreqHz"),
            context + " lowPassFreqHz") < 0.0 ||
        finite_number(
            value.at("highPassFreqHz"),
            context + " highPassFreqHz") < 0.0) {
        throw ClickDetectorSettingsError(
            context + " frequencies must be non-negative");
    }
    (void) finite_number(
        value.at("passBandRippleDb"),
        context + " passBandRippleDb");
}

void validate_delay(
    const Json& value,
    const std::string& context,
    bool includes_click_type) {
    if (includes_click_type) {
        require_exact(
            value,
            {
                "clickType",
                "filterBearings",
                "filterBand",
                "filterHighPassHz",
                "filterLowPassHz",
                "envelopeBearings",
                "useLeadingEdge",
                "upSample",
                "useRestrictedBins",
                "restrictedBins",
            },
            context);
        const auto click_type =
            unsigned_integer(value.at("clickType"), context + " clickType");
        if (click_type == 0 || click_type > 255) {
            throw ClickDetectorSettingsError(
                context + " clickType must be in 1..255");
        }
    }
    else {
        require_exact(
            value,
            {
                "filterBearings",
                "filterBand",
                "filterHighPassHz",
                "filterLowPassHz",
                "envelopeBearings",
                "useLeadingEdge",
                "upSample",
                "useRestrictedBins",
                "restrictedBins",
            },
            context);
    }
    require_boolean(value.at("filterBearings"), context + " filterBearings");
    if (!value.at("filterBand").is_string()) {
        throw ClickDetectorSettingsError(
            context + " filterBand must be a string");
    }
    static const std::set<std::string> bands{
        "highPass",
        "lowPass",
        "bandPass",
        "bandStop",
    };
    if (!bands.contains(value.at("filterBand").get<std::string>())) {
        throw ClickDetectorSettingsError(
            context + " filterBand is unsupported");
    }
    if (finite_number(
            value.at("filterHighPassHz"),
            context + " filterHighPassHz") < 0.0 ||
        finite_number(
            value.at("filterLowPassHz"),
            context + " filterLowPassHz") < 0.0) {
        throw ClickDetectorSettingsError(
            context + " filter frequencies must be non-negative");
    }
    require_boolean(
        value.at("envelopeBearings"),
        context + " envelopeBearings");
    require_boolean(
        value.at("useLeadingEdge"),
        context + " useLeadingEdge");
    const auto up_sample =
        unsigned_integer(value.at("upSample"), context + " upSample");
    if (up_sample == 0 || up_sample > 32) {
        throw ClickDetectorSettingsError(
            context + " upSample must be in 1..32");
    }
    require_boolean(
        value.at("useRestrictedBins"),
        context + " useRestrictedBins");
    if (unsigned_integer(
            value.at("restrictedBins"),
            context + " restrictedBins") == 0) {
        throw ClickDetectorSettingsError(
            context + " restrictedBins must be positive");
    }
}

void validate_detector(const Json& detector) {
    require_exact(
        detector,
        {
            "channelBitmap",
            "groupingType",
            "channelGroups",
            "triggerBitmap",
            "minTriggerChannels",
            "thresholdDb",
            "longFilter",
            "longFilter2",
            "shortFilter",
            "preSample",
            "postSample",
            "minSep",
            "maxLength",
            "sampleNoise",
            "noiseSampleIntervalSeconds",
            "storeBackground",
            "backgroundIntervalMilliseconds",
            "publishTriggerFunction",
            "preFilter",
            "triggerFilter",
            "echo",
        },
        "Click Detector detection settings");
    const auto bitmap = unsigned_integer(
        detector.at("channelBitmap"),
        "Click Detector channelBitmap");
    if (bitmap == 0 || bitmap > kMaximumBitmap) {
        throw ClickDetectorSettingsError(
            "Click Detector channelBitmap must select at least one channel");
    }
    if (unsigned_integer(
            detector.at("triggerBitmap"),
            "Click Detector triggerBitmap") > kMaximumBitmap) {
        throw ClickDetectorSettingsError(
            "Click Detector triggerBitmap exceeds 32 channels");
    }
    const auto grouping =
        detector.at("groupingType").get<std::string>();
    if (grouping != "all" &&
        grouping != "singles" &&
        grouping != "user") {
        throw ClickDetectorSettingsError(
            "Click Detector groupingType must be all, singles, or user");
    }
    const auto& groups = detector.at("channelGroups");
    if (!groups.is_array() || groups.size() > 32) {
        throw ClickDetectorSettingsError(
            "Click Detector channelGroups must be an array of at most 32");
    }
    for (const auto& group : groups) {
        if (unsigned_integer(
                group,
                "Click Detector channelGroups") > 31) {
            throw ClickDetectorSettingsError(
                "Click Detector channelGroups values must be in 0..31");
        }
    }
    if (grouping == "user") {
        for (std::size_t channel = 0; channel < 32; ++channel) {
            if ((bitmap & (std::uint64_t{1} << channel)) != 0 &&
                groups.size() <= channel) {
                throw ClickDetectorSettingsError(
                    "User grouping must assign every selected channel");
            }
        }
    }
    const auto minimum = unsigned_integer(
        detector.at("minTriggerChannels"),
        "Click Detector minTriggerChannels");
    if (minimum == 0 || minimum > 32) {
        throw ClickDetectorSettingsError(
            "Click Detector minTriggerChannels must be in 1..32");
    }
    (void) finite_number(
        detector.at("thresholdDb"),
        "Click Detector thresholdDb");
    require_range(
        finite_number(detector.at("longFilter"), "Click Detector longFilter"),
        0.0,
        1.0,
        "Click Detector longFilter");
    require_range(
        finite_number(detector.at("longFilter2"), "Click Detector longFilter2"),
        0.0,
        1.0,
        "Click Detector longFilter2");
    require_range(
        finite_number(detector.at("shortFilter"), "Click Detector shortFilter"),
        0.0,
        1.0,
        "Click Detector shortFilter");
    (void) unsigned_integer(detector.at("preSample"), "Click Detector preSample");
    (void) unsigned_integer(detector.at("postSample"), "Click Detector postSample");
    (void) unsigned_integer(detector.at("minSep"), "Click Detector minSep");
    if (unsigned_integer(
            detector.at("maxLength"),
            "Click Detector maxLength") == 0) {
        throw ClickDetectorSettingsError(
            "Click Detector maxLength must be positive");
    }
    require_boolean(detector.at("sampleNoise"), "Click Detector sampleNoise");
    if (finite_number(
            detector.at("noiseSampleIntervalSeconds"),
            "Click Detector noiseSampleIntervalSeconds") <= 0.0) {
        throw ClickDetectorSettingsError(
            "Click Detector noise interval must be positive");
    }
    require_boolean(
        detector.at("storeBackground"),
        "Click Detector storeBackground");
    (void) unsigned_integer(
        detector.at("backgroundIntervalMilliseconds"),
        "Click Detector backgroundIntervalMilliseconds");
    require_boolean(
        detector.at("publishTriggerFunction"),
        "Click Detector publishTriggerFunction");
    validate_filter(detector.at("preFilter"), "Click Detector preFilter");
    validate_filter(detector.at("triggerFilter"), "Click Detector triggerFilter");
    const auto& echo = detector.at("echo");
    require_exact(
        echo,
        {"runOnline", "discardEchoes", "maxIntervalSeconds"},
        "Click Detector echo settings");
    require_boolean(echo.at("runOnline"), "Click Detector echo runOnline");
    require_boolean(
        echo.at("discardEchoes"),
        "Click Detector echo discardEchoes");
    if (finite_number(
            echo.at("maxIntervalSeconds"),
            "Click Detector echo maxIntervalSeconds") < 0.0) {
        throw ClickDetectorSettingsError(
            "Click Detector echo interval must be non-negative");
    }
}

void validate_features(const Json& features) {
    require_exact(
        features,
        {
            "fftLength",
            "lengthEnergyFraction",
            "widthEnergyFraction",
            "energyBandsHz",
            "peakFrequencySearchHz",
            "meanFrequencyRangeHz",
        },
        "Click feature settings");
    (void) unsigned_integer(features.at("fftLength"), "Click feature fftLength");
    require_range(
        finite_number(
            features.at("lengthEnergyFraction"),
            "Click feature lengthEnergyFraction"),
        0.0,
        100.0,
        "Click feature lengthEnergyFraction");
    require_range(
        finite_number(
            features.at("widthEnergyFraction"),
            "Click feature widthEnergyFraction"),
        0.0,
        100.0,
        "Click feature widthEnergyFraction");
    if (!features.at("energyBandsHz").is_array()) {
        throw ClickDetectorSettingsError(
            "Click feature energyBandsHz must be an array");
    }
    for (const auto& band : features.at("energyBandsHz")) {
        validate_pair(band, "Click feature energy band");
    }
    validate_pair(
        features.at("peakFrequencySearchHz"),
        "Click feature peak-frequency search");
    validate_pair(
        features.at("meanFrequencyRangeHz"),
        "Click feature mean-frequency range");
}

void validate_classification(const Json& classification) {
    require_exact(
        classification,
        {
            "runOnline",
            "mode",
            "discardUnclassified",
            "checkAllClassifiers",
            "amplitudeDbOffsetByChannel",
            "basicTypes",
            "sweepTypes",
        },
        "Click classification settings");
    require_boolean(
        classification.at("runOnline"),
        "Click classification runOnline");
    require_boolean(
        classification.at("discardUnclassified"),
        "Click classification discardUnclassified");
    require_boolean(
        classification.at("checkAllClassifiers"),
        "Click classification checkAllClassifiers");
    if (!classification.at("mode").is_string()) {
        throw ClickDetectorSettingsError(
            "Click classification mode must be a string");
    }
    static const std::set<std::string> modes{
        "none",
        "basic",
        "sweep",
    };
    if (!modes.contains(
            classification.at("mode").get<std::string>())) {
        throw ClickDetectorSettingsError(
            "Click classification mode must be none, basic, or sweep");
    }
    if (!classification.at("amplitudeDbOffsetByChannel").is_array()) {
        throw ClickDetectorSettingsError(
            "Click classification amplitude offsets must be an array");
    }
    for (const auto& offset :
         classification.at("amplitudeDbOffsetByChannel")) {
        (void) finite_number(offset, "Click classification amplitude offset");
    }
    const auto& basic_types = classification.at("basicTypes");
    if (!basic_types.is_array()) {
        throw ClickDetectorSettingsError(
            "Click classification basicTypes must be an array");
    }
    std::set<std::uint64_t> basic_species_codes;
    for (std::size_t index = 0; index < basic_types.size(); ++index) {
        const auto code = validate_basic_classifier_type(
            basic_types.at(index),
            "Click classification basicTypes[" +
                std::to_string(index) + "]");
        if (!basic_species_codes.insert(code).second) {
            throw ClickDetectorSettingsError(
                "Click classification basicTypes speciesCode values "
                "must be unique");
        }
    }

    const auto& sweep_types = classification.at("sweepTypes");
    if (!sweep_types.is_array()) {
        throw ClickDetectorSettingsError(
            "Click classification sweepTypes must be an array");
    }
    std::set<std::uint64_t> sweep_species_codes;
    for (std::size_t index = 0; index < sweep_types.size(); ++index) {
        const auto code = validate_sweep_classifier_type(
            sweep_types.at(index),
            "Click classification sweepTypes[" +
                std::to_string(index) + "]");
        if (!sweep_species_codes.insert(code).second) {
            throw ClickDetectorSettingsError(
                "Click classification sweepTypes speciesCode values "
                "must be unique");
        }
    }
}

void validate_localisation(const Json& localisation) {
    require_exact(
        localisation,
        {
            "delayMeasurement",
            "typeSettings",
            "angleVetoes",
            "trackedTrain",
        },
        "Click localisation settings");
    validate_delay(
        localisation.at("delayMeasurement"),
        "Click delay measurement",
        false);
    if (!localisation.at("typeSettings").is_array()) {
        throw ClickDetectorSettingsError(
            "Click type delay settings must be an array");
    }
    for (const auto& item : localisation.at("typeSettings")) {
        validate_delay(item, "Click type delay setting", true);
    }
    if (!localisation.at("angleVetoes").is_array()) {
        throw ClickDetectorSettingsError(
            "Click angle vetoes must be an array");
    }
    for (const auto& veto : localisation.at("angleVetoes")) {
        require_exact(
            veto,
            {"channels", "startAngleDegrees", "endAngleDegrees"},
            "Click angle veto");
        if (unsigned_integer(
                veto.at("channels"),
                "Click angle veto channels") > kMaximumBitmap) {
            throw ClickDetectorSettingsError(
                "Click angle veto channels exceed 32 channels");
        }
        (void) finite_number(
            veto.at("startAngleDegrees"),
            "Click angle veto start angle");
        (void) finite_number(
            veto.at("endAngleDegrees"),
            "Click angle veto end angle");
    }
    const auto& tracked = localisation.at("trackedTrain");
    require_exact(
        tracked,
        {
            "isSelected",
            "maxRangeM",
            "maxHeightM",
            "minHeightM",
            "maxTimeMilliseconds",
            "limitPoints",
            "maxPoints",
        },
        "Tracked-click-train localisation");
    if (!tracked.at("isSelected").is_array() ||
        tracked.at("isSelected").size() != 4) {
        throw ClickDetectorSettingsError(
            "Tracked-click-train isSelected must contain exactly four "
            "Java algorithm flags");
    }
    for (const auto& selected : tracked.at("isSelected")) {
        require_boolean(
            selected,
            "Tracked-click-train isSelected entry");
    }
    if (finite_number(
            tracked.at("maxRangeM"),
            "Tracked-click-train maxRangeM") <= 0.0) {
        throw ClickDetectorSettingsError(
            "Tracked-click-train maxRangeM must be positive");
    }
    const auto maximum_height = finite_number(
        tracked.at("maxHeightM"),
        "Tracked-click-train maxHeightM");
    const auto minimum_height = finite_number(
        tracked.at("minHeightM"),
        "Tracked-click-train minHeightM");
    if (minimum_height > maximum_height) {
        throw ClickDetectorSettingsError(
            "Tracked-click-train height bounds must be ordered");
    }
    (void) unsigned_integer(
        tracked.at("maxTimeMilliseconds"),
        "Tracked-click-train maxTimeMilliseconds");
    require_boolean(
        tracked.at("limitPoints"),
        "Tracked-click-train limitPoints");
    if (unsigned_integer(
            tracked.at("maxPoints"),
            "Tracked-click-train maxPoints") == 0) {
        throw ClickDetectorSettingsError(
            "Tracked-click-train maxPoints must be positive");
    }
}

void validate_train(const Json& train) {
    require_exact(
        train,
        {
            "enabled",
            "minIciSeconds",
            "maxIciSeconds",
            "maxIciChange",
            "okAngleErrorDegrees",
            "initialPerpendicularDistanceM",
            "minClicks",
            "minAngleChangeDegrees",
            "iciUpdateRatio",
            "minUpdateGapSeconds",
        },
        "Click Train Identification settings");
    require_boolean(
        train.at("enabled"),
        "Click Train Identification enabled");
    const auto minimum = finite_number(
        train.at("minIciSeconds"),
        "Click Train Identification minIciSeconds");
    const auto maximum = finite_number(
        train.at("maxIciSeconds"),
        "Click Train Identification maxIciSeconds");
    if (minimum <= 0.0 || maximum < minimum) {
        throw ClickDetectorSettingsError(
            "Click Train Identification ICI range must be ordered and positive");
    }
    if (finite_number(
            train.at("maxIciChange"),
            "Click Train Identification maxIciChange") < 1.0) {
        throw ClickDetectorSettingsError(
            "Click Train Identification maxIciChange must be at least 1");
    }
    if (finite_number(
            train.at("okAngleErrorDegrees"),
            "Click Train Identification okAngleErrorDegrees") < 0.0 ||
        finite_number(
            train.at("initialPerpendicularDistanceM"),
            "Click Train Identification initialPerpendicularDistanceM") <
            0.0 ||
        finite_number(
            train.at("minAngleChangeDegrees"),
            "Click Train Identification minAngleChangeDegrees") < 0.0) {
        throw ClickDetectorSettingsError(
            "Click Train Identification angle and distance limits "
            "must be non-negative");
    }
    if (unsigned_integer(
            train.at("minClicks"),
            "Click Train Identification minClicks") == 0) {
        throw ClickDetectorSettingsError(
            "Click Train Identification minClicks must be positive");
    }
    require_range(
        finite_number(
            train.at("iciUpdateRatio"),
            "Click Train Identification iciUpdateRatio"),
        0.0,
        1.0,
        "Click Train Identification iciUpdateRatio");
    if (finite_number(
            train.at("minUpdateGapSeconds"),
            "Click Train Identification minUpdateGapSeconds") < 0.0) {
        throw ClickDetectorSettingsError(
            "Click Train Identification update gap must be non-negative");
    }
}

Json validated_click_settings(
    std::string_view text,
    std::uint32_t version) {
    if (version != 1) {
        throw ClickDetectorSettingsError(
            "Unsupported Click Detector settings version");
    }
    auto settings = parse_object(text, "Click Detector settings");
    require_exact(
        settings,
        {
            "detector",
            "features",
            "classification",
            "localisation",
            "train",
            "display",
        },
        "Click Detector settings");
    validate_detector(settings.at("detector"));
    validate_features(settings.at("features"));
    validate_classification(settings.at("classification"));
    validate_localisation(settings.at("localisation"));
    validate_train(settings.at("train"));
    validate_click_display_settings_json(
        settings.at("display").dump(),
        1);
    return settings;
}

Json delay_without_click_type(const Json& value) {
    auto result = value;
    result.erase("clickType");
    return result;
}

Json localiser_runtime_settings(
    const Json& settings,
    const core::ArrayConfiguration& array) {
    Json hydrophones = Json::array();
    for (const auto& hydrophone : array.hydrophones) {
        hydrophones.push_back({
            {"channel", hydrophone.channel},
            {"xM", hydrophone.x_m},
            {"yM", hydrophone.y_m},
            {"zM", hydrophone.z_m},
            {"streamerId", hydrophone.streamer_id},
            {"xErrorM", hydrophone.x_error_m},
            {"yErrorM", hydrophone.y_error_m},
            {"zErrorM", hydrophone.z_error_m},
        });
    }
    Json type_settings = Json::array();
    for (const auto& item : settings.at("localisation").at("typeSettings")) {
        auto projected = delay_without_click_type(item);
        projected["clickType"] = item.at("clickType");
        type_settings.push_back(std::move(projected));
    }
    auto delay =
        settings.at("localisation").at("delayMeasurement");
    delay["typeSettings"] = std::move(type_settings);
    return {
        {"preSample", settings.at("detector").at("preSample")},
        {"speedOfSoundMps", array.speed_of_sound_mps},
        {"speedOfSoundErrorMps", array.speed_of_sound_error_mps},
        {"timingErrorSeconds", array.timing_error_seconds},
        {"spacingErrorM", array.spacing_error_m},
        {"wobbleRadians", array.wobble_radians},
        {"orientation", {
            {"declared", array.orientation.declared},
            {"headingDegrees", array.orientation.heading_degrees},
            {"pitchDegrees", array.orientation.pitch_degrees},
            {"rollDegrees", array.orientation.roll_degrees},
        }},
        {"hydrophones", std::move(hydrophones)},
        {"delayMeasurement", std::move(delay)},
        {
            "angleVetoes",
            settings.at("localisation").at("angleVetoes"),
        },
    };
}

Json basic_classifier_type_default() {
    return {
        {"name", "New basic click type"},
        {"speciesCode", 1},
        {"enabled", true},
        {"discard", false},
        {"whichSelections", 5},
        {"band1FreqHz", {0.0, 0.0}},
        {"band2FreqHz", {0.0, 0.0}},
        {"band1EnergyDb", {0.0, 0.0}},
        {"band2EnergyDb", {0.0, 0.0}},
        {"bandEnergyDifferenceDb", 0.0},
        {"peakFrequencySearchHz", {0.0, 0.0}},
        {"peakFrequencyRangeHz", {0.0, 0.0}},
        {"peakWidthHz", {0.0, 0.0}},
        {"widthEnergyFraction", 0.0},
        {"meanSumRangeHz", {0.0, 0.0}},
        {"meanSelectionRangeHz", {0.0, 0.0}},
        {"clickLengthMs", {0.0, 0.0}},
        {"lengthEnergyFraction", 0.0},
    };
}

Json sweep_classifier_type_default() {
    return {
        {"name", "New sweep click type"},
        {"speciesCode", 1},
        {"discard", false},
        {"enabled", true},
        {"channelChoice", "requireAll"},
        {"restrictLength", true},
        {"restrictedBins", 128},
        {"restrictedBinType", "clickCenter"},
        {"enableLength", true},
        {"lengthSmoothing", 5},
        {"lengthDb", 6.0},
        {"lengthMs", {0.0, 1.0}},
        {"enableEnergyBands", false},
        {"testEnergyBandHz", {0.0, 0.0}},
        {"controlEnergyBand0Hz", {0.0, 0.0}},
        {"controlEnergyBand1Hz", {0.0, 0.0}},
        {"energyThreshold0Db", 0.0},
        {"energyThreshold1Db", 0.0},
        {"testAmplitude", false},
        {"amplitudeRangeDb", {0.0, 200.0}},
        {"enableFftFilter", false},
        {
            "fftFilter",
            {
                {"band", "highPass"},
                {"lowPassFreqHz", 0.0},
                {"highPassFreqHz", 0.0},
            },
        },
        {"enablePeak", false},
        {"enableWidth", false},
        {"enableMean", false},
        {"peakSearchRangeHz", {0.0, 0.0}},
        {"peakRangeHz", {0.0, 0.0}},
        {"peakWidthRangeHz", {0.0, 0.0}},
        {"meanRangeHz", {0.0, 0.0}},
        {"peakSmoothing", 5},
        {"peakWidthThresholdDb", 6.0},
        {"enableZeroCrossings", false},
        {"zeroCrossingCount", {0, 0}},
        {"enableSweep", false},
        {"zeroCrossingSweepKhzPerMs", {0.0, 0.0}},
        {"enableMinCrossCorrelation", false},
        {"enablePeakCrossCorrelation", false},
        {"minCorrelation", 0.0},
        {"correlationFactor", 1.0},
        {"enableBearingLimits", false},
        {"excludeBearingLimits", false},
        {
            "bearingLimitsRadians",
            {-3.14159265358979323846, 3.14159265358979323846},
        },
    };
}

Json classifier_pair_schema(
    bool non_negative = false,
    bool integer = false) {
    Json item{{"type", integer ? "integer" : "number"}};
    if (non_negative) {
        item["minimum"] = 0;
    }
    return {
        {"type", "array"},
        {"minItems", 2},
        {"maxItems", 2},
        {"items", std::move(item)},
    };
}

void add_classifier_property_defaults(
    Json& properties,
    const Json& defaults) {
    for (auto& [name, property] : properties.items()) {
        property["default"] = defaults.at(name);
    }
}

Json basic_classifier_type_schema() {
    const auto defaults = basic_classifier_type_default();
    Json properties{
        {
            "name",
            {
                {"type", "string"},
                {"maxLength", 128},
                {
                    "description",
                    "Portable non-null form of ClickTypeCommonParams.name; "
                    "raw Java instances may hold null.",
                },
                {"x-pamguard-portable-normalization", "non-null-string"},
            },
        },
        {
            "speciesCode",
            {
                {"type", "integer"},
                {"minimum", 1},
                {"maximum", 255},
                {
                    "description",
                    "Portable project identity range. Java stores an int "
                    "but later narrows it to a byte.",
                },
                {"x-pamguard-portable-normalization", "unique-1-to-255"},
            },
        },
        {
            "enabled",
            {
                {"type", "boolean"},
                {
                    "description",
                    "Stored by PAMGuard, but BasicClickIdentifier in the "
                    "pinned Java authority does not consult this flag.",
                },
                {
                    "x-pamguard-runtime-effect",
                    "stored-java-quirk-no-classification-effect",
                },
            },
        },
        {"discard", {{"type", "boolean"}}},
        {
            "whichSelections",
            {{"type", "integer"}, {"minimum", 0}, {"maximum", 31}},
        },
        {"band1FreqHz", classifier_pair_schema()},
        {"band2FreqHz", classifier_pair_schema()},
        {"band1EnergyDb", classifier_pair_schema()},
        {"band2EnergyDb", classifier_pair_schema()},
        {"bandEnergyDifferenceDb", {{"type", "number"}}},
        {"peakFrequencySearchHz", classifier_pair_schema()},
        {"peakFrequencyRangeHz", classifier_pair_schema()},
        {"peakWidthHz", classifier_pair_schema()},
        {"widthEnergyFraction", {{"type", "number"}}},
        {"meanSumRangeHz", classifier_pair_schema()},
        {"meanSelectionRangeHz", classifier_pair_schema()},
        {"clickLengthMs", classifier_pair_schema()},
        {"lengthEnergyFraction", {{"type", "number"}}},
    };
    add_classifier_property_defaults(properties, defaults);
    return {
        {"type", "object"},
        {"additionalProperties", false},
        {"default", defaults},
        {"properties", std::move(properties)},
        {
            "required",
            Json::array({
                "name",
                "speciesCode",
                "enabled",
                "discard",
                "whichSelections",
                "band1FreqHz",
                "band2FreqHz",
                "band1EnergyDb",
                "band2EnergyDb",
                "bandEnergyDifferenceDb",
                "peakFrequencySearchHz",
                "peakFrequencyRangeHz",
                "peakWidthHz",
                "widthEnergyFraction",
                "meanSumRangeHz",
                "meanSelectionRangeHz",
                "clickLengthMs",
                "lengthEnergyFraction",
            }),
        },
    };
}

Json sweep_classifier_type_schema() {
    const auto defaults = sweep_classifier_type_default();
    Json fft_filter_properties{
        {
            "band",
            {
                {"type", "string"},
                {
                    "enum",
                    Json::array({
                        "highPass",
                        "lowPass",
                        "bandPass",
                        "bandStop",
                    }),
                },
            },
        },
        {
            "lowPassFreqHz",
            {{"type", "number"}},
        },
        {
            "highPassFreqHz",
            {{"type", "number"}},
        },
    };
    add_classifier_property_defaults(
        fft_filter_properties,
        defaults.at("fftFilter"));
    Json properties{
        {
            "name",
            {
                {"type", "string"},
                {"minLength", 1},
                {"maxLength", 128},
                {
                    "description",
                    "Portable non-null set name. A raw Java set starts with "
                    "null and the dialog requires a name before saving.",
                },
                {"x-pamguard-portable-normalization", "non-null-string"},
            },
        },
        {
            "speciesCode",
            {
                {"type", "integer"},
                {"minimum", 1},
                {"maximum", 255},
                {
                    "description",
                    "Portable project identity range. Java stores an int "
                    "but later narrows it to a byte.",
                },
                {"x-pamguard-portable-normalization", "unique-1-to-255"},
            },
        },
        {"discard", {{"type", "boolean"}}},
        {"enabled", {{"type", "boolean"}}},
        {
            "channelChoice",
            {
                {"type", "string"},
                {
                    "enum",
                    Json::array({
                        "requireAll",
                        "requireOne",
                        "useMeans",
                    }),
                },
            },
        },
        {"restrictLength", {{"type", "boolean"}}},
        {
            "restrictedBins",
            {{"type", "integer"}, {"minimum", 1}},
        },
        {
            "restrictedBinType",
            {
                {"type", "string"},
                {"enum", Json::array({"clickCenter", "clickStart"})},
            },
        },
        {"enableLength", {{"type", "boolean"}}},
        {
            "lengthSmoothing",
            {
                {"type", "integer"},
                {"minimum", 1},
                {"x-pamguard-odd", true},
            },
        },
        {
            "lengthDb",
            {
                {"type", "number"},
                {"not", {{"const", 0}}},
            },
        },
        {"lengthMs", classifier_pair_schema()},
        {"enableEnergyBands", {{"type", "boolean"}}},
        {"testEnergyBandHz", classifier_pair_schema()},
        {"controlEnergyBand0Hz", classifier_pair_schema()},
        {"controlEnergyBand1Hz", classifier_pair_schema()},
        {"energyThreshold0Db", {{"type", "number"}}},
        {"energyThreshold1Db", {{"type", "number"}}},
        {"testAmplitude", {{"type", "boolean"}}},
        {"amplitudeRangeDb", classifier_pair_schema()},
        {"enableFftFilter", {{"type", "boolean"}}},
        {
            "fftFilter",
            {
                {"type", "object"},
                {"additionalProperties", false},
                {"default", defaults.at("fftFilter")},
                {"properties", std::move(fft_filter_properties)},
                {
                    "required",
                    Json::array({
                        "band",
                        "lowPassFreqHz",
                        "highPassFreqHz",
                    }),
                },
            },
        },
        {"enablePeak", {{"type", "boolean"}}},
        {"enableWidth", {{"type", "boolean"}}},
        {"enableMean", {{"type", "boolean"}}},
        {"peakSearchRangeHz", classifier_pair_schema()},
        {"peakRangeHz", classifier_pair_schema()},
        {"peakWidthRangeHz", classifier_pair_schema()},
        {"meanRangeHz", classifier_pair_schema()},
        {
            "peakSmoothing",
            {
                {"type", "integer"},
                {
                    "description",
                    "Must be positive and odd when any peak, width, or mean "
                    "test is enabled; inactive Java settings retain stale "
                    "values.",
                },
                {
                    "x-pamguard-conditional-constraint",
                    "positive-odd-when-spectral-test-enabled",
                },
            },
        },
        {
            "peakWidthThresholdDb",
            {
                {"type", "number"},
                {
                    "x-pamguard-conditional-constraint",
                    "nonzero-when-width-enabled",
                },
            },
        },
        {"enableZeroCrossings", {{"type", "boolean"}}},
        {"zeroCrossingCount", classifier_pair_schema(false, true)},
        {"enableSweep", {{"type", "boolean"}}},
        {"zeroCrossingSweepKhzPerMs", classifier_pair_schema()},
        {"enableMinCrossCorrelation", {{"type", "boolean"}}},
        {"enablePeakCrossCorrelation", {{"type", "boolean"}}},
        {"minCorrelation", {{"type", "number"}}},
        {"correlationFactor", {{"type", "number"}}},
        {"enableBearingLimits", {{"type", "boolean"}}},
        {"excludeBearingLimits", {{"type", "boolean"}}},
        {
            "bearingLimitsRadians",
            {
                {"type", "array"},
                {"minItems", 2},
                {"maxItems", 2},
                {
                    "items",
                    {
                        {"type", "number"},
                    },
                },
            },
        },
    };
    add_classifier_property_defaults(properties, defaults);
    return {
        {"type", "object"},
        {"additionalProperties", false},
        {"default", defaults},
        {"properties", std::move(properties)},
        {
            "required",
            Json::array({
                "name",
                "speciesCode",
                "discard",
                "enabled",
                "channelChoice",
                "restrictLength",
                "restrictedBins",
                "restrictedBinType",
                "enableLength",
                "lengthSmoothing",
                "lengthDb",
                "lengthMs",
                "enableEnergyBands",
                "testEnergyBandHz",
                "controlEnergyBand0Hz",
                "controlEnergyBand1Hz",
                "energyThreshold0Db",
                "energyThreshold1Db",
                "testAmplitude",
                "amplitudeRangeDb",
                "enableFftFilter",
                "fftFilter",
                "enablePeak",
                "enableWidth",
                "enableMean",
                "peakSearchRangeHz",
                "peakRangeHz",
                "peakWidthRangeHz",
                "meanRangeHz",
                "peakSmoothing",
                "peakWidthThresholdDb",
                "enableZeroCrossings",
                "zeroCrossingCount",
                "enableSweep",
                "zeroCrossingSweepKhzPerMs",
                "enableMinCrossCorrelation",
                "enablePeakCrossCorrelation",
                "minCorrelation",
                "correlationFactor",
                "enableBearingLimits",
                "excludeBearingLimits",
                "bearingLimitsRadians",
            }),
        },
    };
}

SettingsDescriptor click_settings_descriptor() {
    SettingsDescriptor descriptor{
        1,
        {
            "clickDetector.ClickParameters",
            "clickDetector.ClickClassifiers.ClickClassifierManager",
            "clickDetector.BasicClickIdParameters",
            "clickDetector.ClickTypeParams",
            "clickDetector.ClickClassifiers.ClickTypeCommonParams",
            "clickDetector.ClickClassifiers.basicSweep.SweepClassifierParameters",
            "clickDetector.ClickClassifiers.basicSweep.SweepClassifierSet",
            "clickDetector.clicktrains.ClickTrainIdParams",
            "clickDetector.localisation.ClickLocParams",
            "Localiser.DelayMeasurementParams",
            "angleVetoes.AngleVetoParameters",
            "clickDetector.echoDetection.SimpleEchoParams",
        },
        {
            "src/clickDetector/ClickControl.java",
            "src/clickDetector/ClickParameters.java",
            "src/clickDetector/ClickDetector.java",
            "src/clickDetector/ClickClassifiers/ClickClassifierManager.java",
            "src/clickDetector/BasicClickIdParameters.java",
            "src/clickDetector/ClickTypeParams.java",
            "src/clickDetector/ClickClassifiers/ClickTypeCommonParams.java",
            "src/clickDetector/ClickClassifiers/basic/BasicClickIdentifier.java",
            "src/clickDetector/ClickClassifiers/basicSweep/SweepClassifier.java",
            "src/clickDetector/ClickClassifiers/basicSweep/SweepClassifierParameters.java",
            "src/clickDetector/ClickClassifiers/basicSweep/SweepClassifierSet.java",
            "src/clickDetector/clicktrains/ClickTrainIdParams.java",
            "src/clickDetector/ClickTrainDetector.java",
            "src/clickDetector/ClickBTDisplay.java",
            "src/clickDetector/TrackedClickLocaliser.java",
            "src/clickDetector/localisation/ClickLocParams.java",
            "src/clickDetector/localisation/GeneralGroupLocaliser.java",
            "src/clickDetector/localisation/ClickGroupLocaliser.java",
            "src/Localiser/DelayMeasurementParams.java",
            "src/angleVetoes/AngleVetoParameters.java",
            "src/clickDetector/echoDetection/SimpleEchoParams.java",
        },
        R"({"detector":{"channelBitmap":3,"groupingType":"all","channelGroups":[0,0],"triggerBitmap":4294967295,"minTriggerChannels":1,"thresholdDb":10,"longFilter":0.00001,"longFilter2":0.000001,"shortFilter":0.1,"preSample":40,"postSample":40,"minSep":100,"maxLength":1024,"sampleNoise":true,"noiseSampleIntervalSeconds":5,"storeBackground":true,"backgroundIntervalMilliseconds":5000,"publishTriggerFunction":false,"preFilter":{"type":"butterworth","band":"highPass","order":4,"lowPassFreqHz":20000,"highPassFreqHz":500,"passBandRippleDb":2},"triggerFilter":{"type":"butterworth","band":"highPass","order":2,"lowPassFreqHz":20000,"highPassFreqHz":2000,"passBandRippleDb":2},"echo":{"runOnline":false,"discardEchoes":false,"maxIntervalSeconds":0.1}},"features":{"fftLength":0,"lengthEnergyFraction":90,"widthEnergyFraction":90,"energyBandsHz":[[1000,6000],[6000,14000]],"peakFrequencySearchHz":[500,20000],"meanFrequencyRangeHz":[500,20000]},"classification":{"runOnline":false,"mode":"sweep","discardUnclassified":false,"checkAllClassifiers":false,"amplitudeDbOffsetByChannel":[],"basicTypes":[],"sweepTypes":[]},"localisation":{"delayMeasurement":{"filterBearings":false,"filterBand":"highPass","filterHighPassHz":0,"filterLowPassHz":0,"envelopeBearings":false,"useLeadingEdge":false,"upSample":1,"useRestrictedBins":false,"restrictedBins":80},"typeSettings":[],"angleVetoes":[],"trackedTrain":{"isSelected":[true,false,false,false],"maxRangeM":20000,"maxHeightM":5,"minHeightM":-5000,"maxTimeMilliseconds":200,"limitPoints":false,"maxPoints":30}},"train":{"enabled":false,"minIciSeconds":0.1,"maxIciSeconds":2,"maxIciChange":1.2,"okAngleErrorDegrees":1,"initialPerpendicularDistanceM":100,"minClicks":6,"minAngleChangeDegrees":5,"iciUpdateRatio":0.5,"minUpdateGapSeconds":5},"display":{"channelBitmap":3,"timeWindowSeconds":20,"bearingLimitsDegrees":[0,180],"amplitudeLimitsDb":[0,30],"iciLimitsSeconds":[0.001,3],"showEchoes":true}})",
        {
            {
                "detection-parameters.tabs",
                {
                    "Source",
                    "Trigger",
                    "Click Length",
                    "Delays",
                    "Echoes",
                    "Noise",
                },
            },
        },
        {
            {
                "/detector/channelBitmap",
                "groupedSourceParameters.channelBitmap",
                "3",
                {},
                {},
                "clickDetector.ClickParameters#groupedSourceParameters",
            },
            {
                "/detector/groupingType",
                "groupedSourceParameters.groupingType",
                R"("all")",
                {},
                {},
                "PamView.dialog.GroupedSourcePanel#GROUP_ALL",
            },
            {
                "/detector/thresholdDb",
                "dbThreshold",
                "10",
                {},
                {},
                "clickDetector.ClickParameters#dbThreshold",
            },
            {
                "/classification/mode",
                "clickClassifierType",
                R"("sweep")",
                {},
                {},
                "clickDetector.ClickClassifiers.ClickClassifierManager#CLASSIFY_BETTER",
            },
            {
                "/classification/runOnline",
                "classifyOnline",
                "false",
                {},
                {},
                "clickDetector.ClickParameters#classifyOnline",
            },
            {
                "/train/enabled",
                "runClickTrainId",
                "false",
                {},
                {},
                "clickDetector.clicktrains.ClickTrainIdParams#runClickTrainId",
            },
            {
                "/train/minClicks",
                "minTrainClicks",
                "6",
                {},
                {},
                "clickDetector.clicktrains.ClickTrainIdParams#minTrainClicks",
            },
        },
        SettingsChangePolicy::StopRequired,
        "not-claimed",
        R"({
            "$schema":"https://json-schema.org/draft/2020-12/schema",
            "type":"object",
            "additionalProperties":false,
            "properties":{
                "detector":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "channelBitmap":{"type":"integer","minimum":1,"maximum":4294967295},
                        "groupingType":{"type":"string","enum":["singles","all","user"]},
                        "channelGroups":{"type":"array","maxItems":32,"items":{"type":"integer","minimum":0,"maximum":31}},
                        "triggerBitmap":{"type":"integer","minimum":0,"maximum":4294967295},
                        "minTriggerChannels":{"type":"integer","minimum":1,"maximum":32},
                        "thresholdDb":{"type":"number"},
                        "longFilter":{"type":"number","minimum":0,"maximum":1},
                        "longFilter2":{"type":"number","minimum":0,"maximum":1},
                        "shortFilter":{"type":"number","minimum":0,"maximum":1},
                        "preSample":{"type":"integer","minimum":0},
                        "postSample":{"type":"integer","minimum":0},
                        "minSep":{"type":"integer","minimum":0},
                        "maxLength":{"type":"integer","minimum":1},
                        "sampleNoise":{"type":"boolean"},
                        "noiseSampleIntervalSeconds":{"type":"number","exclusiveMinimum":0},
                        "storeBackground":{"type":"boolean"},
                        "backgroundIntervalMilliseconds":{"type":"integer","minimum":0},
                        "publishTriggerFunction":{"type":"boolean"},
                        "preFilter":{"$ref":"#/$defs/filter"},
                        "triggerFilter":{"$ref":"#/$defs/filter"},
                        "echo":{"$ref":"#/$defs/echo"}
                    },
                    "required":[
                        "channelBitmap","groupingType","channelGroups",
                        "triggerBitmap","minTriggerChannels","thresholdDb",
                        "longFilter","longFilter2","shortFilter",
                        "preSample","postSample","minSep","maxLength",
                        "sampleNoise","noiseSampleIntervalSeconds",
                        "storeBackground","backgroundIntervalMilliseconds",
                        "publishTriggerFunction","preFilter",
                        "triggerFilter","echo"
                    ]
                },
                "features":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "fftLength":{"type":"integer","minimum":0},
                        "lengthEnergyFraction":{"type":"number","minimum":0,"maximum":100},
                        "widthEnergyFraction":{"type":"number","minimum":0,"maximum":100},
                        "energyBandsHz":{"type":"array","items":{"$ref":"#/$defs/nonnegativePair"}},
                        "peakFrequencySearchHz":{"$ref":"#/$defs/nonnegativePair"},
                        "meanFrequencyRangeHz":{"$ref":"#/$defs/nonnegativePair"}
                    },
                    "required":[
                        "fftLength","lengthEnergyFraction",
                        "widthEnergyFraction","energyBandsHz",
                        "peakFrequencySearchHz","meanFrequencyRangeHz"
                    ]
                },
                "classification":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "runOnline":{"type":"boolean"},
                        "mode":{"type":"string","enum":["none","basic","sweep"]},
                        "discardUnclassified":{"type":"boolean"},
                        "checkAllClassifiers":{"type":"boolean"},
                        "amplitudeDbOffsetByChannel":{"type":"array","items":{"type":"number"}},
                        "basicTypes":{"type":"array","items":{}},
                        "sweepTypes":{"type":"array","items":{}}
                    },
                    "required":[
                        "runOnline","mode","discardUnclassified",
                        "checkAllClassifiers","amplitudeDbOffsetByChannel",
                        "basicTypes","sweepTypes"
                    ]
                },
                "localisation":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "delayMeasurement":{"$ref":"#/$defs/delay"},
                        "typeSettings":{
                            "type":"array",
                            "items":{
                                "type":"object",
                                "additionalProperties":false,
                                "properties":{
                                    "clickType":{"type":"integer","minimum":1,"maximum":255},
                                    "filterBearings":{"type":"boolean"},
                                    "filterBand":{"type":"string","enum":["highPass","lowPass","bandPass","bandStop"]},
                                    "filterHighPassHz":{"type":"number","minimum":0},
                                    "filterLowPassHz":{"type":"number","minimum":0},
                                    "envelopeBearings":{"type":"boolean"},
                                    "useLeadingEdge":{"type":"boolean"},
                                    "upSample":{"type":"integer","minimum":1,"maximum":32},
                                    "useRestrictedBins":{"type":"boolean"},
                                    "restrictedBins":{"type":"integer","minimum":1}
                                },
                                "required":[
                                    "clickType","filterBearings","filterBand",
                                    "filterHighPassHz","filterLowPassHz",
                                    "envelopeBearings","useLeadingEdge",
                                    "upSample","useRestrictedBins","restrictedBins"
                                ]
                            }
                        },
                        "angleVetoes":{
                            "type":"array",
                            "items":{
                                "type":"object",
                                "additionalProperties":false,
                                "properties":{
                                    "channels":{"type":"integer","minimum":0,"maximum":4294967295},
                                    "startAngleDegrees":{"type":"number"},
                                    "endAngleDegrees":{"type":"number"}
                                },
                                "required":["channels","startAngleDegrees","endAngleDegrees"]
                            }
                        },
                        "trackedTrain":{
                            "type":"object",
                            "x-pamguard-runtime-availability":"partial",
                            "x-pamguard-membership-runtime":"available",
                            "x-pamguard-localisation-runtime":"unavailable-missing-navigation-origins",
                            "x-pamguard-algorithms":[
                                {"index":0,"id":"least-squares","javaName":"Least Squares","runModes":["normal","mixed","viewer"]},
                                {"index":1,"id":"simplex-2d","javaName":"2D Simplex Optimization","runModes":["normal","mixed","viewer"]},
                                {"index":2,"id":"simplex-3d","javaName":"3D Simplex Optimization","runModes":["normal","mixed","viewer"]},
                                {"index":3,"id":"mcmc","javaName":"MCMC","runModes":["viewer"]}
                            ],
                            "x-pamguard-unavailable-reason":"Target-motion localisation requires the navigation-derived origin and array heading captured at every click; the current retained Click Detector stream does not publish those snapshots.",
                            "additionalProperties":false,
                            "properties":{
                                "isSelected":{
                                    "type":"array",
                                    "minItems":4,
                                    "maxItems":4,
                                    "items":{"type":"boolean"}
                                },
                                "maxRangeM":{"type":"number","exclusiveMinimum":0},
                                "maxHeightM":{"type":"number"},
                                "minHeightM":{"type":"number"},
                                "maxTimeMilliseconds":{"type":"integer","minimum":0},
                                "limitPoints":{"type":"boolean"},
                                "maxPoints":{"type":"integer","minimum":1}
                            },
                            "required":[
                                "isSelected","maxRangeM","maxHeightM",
                                "minHeightM","maxTimeMilliseconds",
                                "limitPoints","maxPoints"
                            ]
                        }
                    },
                    "required":[
                        "delayMeasurement","typeSettings",
                        "angleVetoes","trackedTrain"
                    ]
                },
                "train":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "enabled":{"type":"boolean"},
                        "minIciSeconds":{"type":"number","exclusiveMinimum":0},
                        "maxIciSeconds":{"type":"number","exclusiveMinimum":0},
                        "maxIciChange":{"type":"number","minimum":1},
                        "okAngleErrorDegrees":{"type":"number","minimum":0},
                        "initialPerpendicularDistanceM":{"type":"number","minimum":0},
                        "minClicks":{"type":"integer","minimum":1},
                        "minAngleChangeDegrees":{"type":"number","minimum":0},
                        "iciUpdateRatio":{"type":"number","minimum":0,"maximum":1},
                        "minUpdateGapSeconds":{"type":"number","minimum":0}
                    },
                    "required":[
                        "enabled","minIciSeconds","maxIciSeconds",
                        "maxIciChange","okAngleErrorDegrees",
                        "initialPerpendicularDistanceM","minClicks",
                        "minAngleChangeDegrees","iciUpdateRatio",
                        "minUpdateGapSeconds"
                    ]
                },
                "display":{"$ref":"#/$defs/display"}
            },
            "required":[
                "detector","features","classification",
                "localisation","train","display"
            ],
            "$defs":{
                "filter":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "type":{"type":"string"},
                        "band":{"type":"string"},
                        "order":{"type":"integer","minimum":1,"maximum":64},
                        "lowPassFreqHz":{"type":"number","minimum":0},
                        "highPassFreqHz":{"type":"number","minimum":0},
                        "passBandRippleDb":{"type":"number"}
                    },
                    "required":[
                        "type","band","order","lowPassFreqHz",
                        "highPassFreqHz","passBandRippleDb"
                    ]
                },
                "echo":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "runOnline":{"type":"boolean"},
                        "discardEchoes":{"type":"boolean"},
                        "maxIntervalSeconds":{"type":"number","minimum":0}
                    },
                    "required":[
                        "runOnline","discardEchoes","maxIntervalSeconds"
                    ]
                },
                "delay":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "filterBearings":{"type":"boolean"},
                        "filterBand":{"type":"string","enum":["highPass","lowPass","bandPass","bandStop"]},
                        "filterHighPassHz":{"type":"number","minimum":0},
                        "filterLowPassHz":{"type":"number","minimum":0},
                        "envelopeBearings":{"type":"boolean"},
                        "useLeadingEdge":{"type":"boolean"},
                        "upSample":{"type":"integer","minimum":1,"maximum":32},
                        "useRestrictedBins":{"type":"boolean"},
                        "restrictedBins":{"type":"integer","minimum":1}
                    },
                    "required":[
                        "filterBearings","filterBand",
                        "filterHighPassHz","filterLowPassHz",
                        "envelopeBearings","useLeadingEdge",
                        "upSample","useRestrictedBins","restrictedBins"
                    ]
                },
                "nonnegativePair":{
                    "type":"array",
                    "minItems":2,
                    "maxItems":2,
                    "items":{"type":"number","minimum":0}
                },
                "display":{
                    "type":"object",
                    "additionalProperties":false,
                    "properties":{
                        "channelBitmap":{"type":"integer","minimum":0,"maximum":4294967295},
                        "timeWindowSeconds":{"type":"number","exclusiveMinimum":0},
                        "bearingLimitsDegrees":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number"}},
                        "amplitudeLimitsDb":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number"}},
                        "iciLimitsSeconds":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number","exclusiveMinimum":0}},
                        "showEchoes":{"type":"boolean"}
                    },
                    "required":[
                        "channelBitmap","timeWindowSeconds",
                        "bearingLimitsDegrees","amplitudeLimitsDb",
                        "iciLimitsSeconds","showEchoes"
                    ]
                }
            }
        })",
    };
    auto schema = Json::parse(descriptor.settings_schema_json);
    schema["$defs"]["basicClassifierType"] =
        basic_classifier_type_schema();
    schema["$defs"]["sweepClassifierType"] =
        sweep_classifier_type_schema();
    auto& classification =
        schema["properties"]["classification"]["properties"];
    classification["basicTypes"]["items"] = {
        {"$ref", "#/$defs/basicClassifierType"},
    };
    classification["basicTypes"]["description"] =
        "Portable ClickTypeParams list. Each speciesCode is unique in "
        "1..255; raw Java settings are less restrictive.";
    classification["basicTypes"]["x-pamguard-unique-by"] =
        "speciesCode";
    classification["sweepTypes"]["items"] = {
        {"$ref", "#/$defs/sweepClassifierType"},
    };
    classification["sweepTypes"]["description"] =
        "Portable SweepClassifierSet list. Each speciesCode is unique in "
        "1..255; raw Java settings are less restrictive.";
    classification["sweepTypes"]["x-pamguard-unique-by"] =
        "speciesCode";
    classification["checkAllClassifiers"]["description"] =
        "SweepClassifierParameters.checkAllClassifiers: evaluate every "
        "enabled Sweep set, retain the first match as the primary type, "
        "and report every passing species code. Basic mode does not use it.";
    descriptor.settings_schema_json = schema.dump();
    return descriptor;
}

SettingsDescriptor click_display_settings_descriptor() {
    return {
        1,
        {
            "clickDetector.BTDisplayParameters",
        },
        {
            "src/clickDetector/ClickBTDisplay.java",
            "src/clickDetector/BTDisplayParameters.java",
            "src/clickDetector/ClickDisplayManager.java",
        },
        R"({"channelBitmap":3,"timeWindowSeconds":20,"bearingLimitsDegrees":[0,180],"amplitudeLimitsDb":[0,30],"iciLimitsSeconds":[0.001,3],"showEchoes":true})",
        {
            {
                "display.operational",
                {
                    "Channel groups",
                    "Time window",
                    "Bearing axis",
                    "Amplitude axis",
                    "ICI axis",
                    "Echo visibility",
                },
            },
        },
        {},
        SettingsChangePolicy::LiveSafe,
        "not-claimed",
        R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","additionalProperties":false,"properties":{"channelBitmap":{"type":"integer","minimum":0,"maximum":4294967295},"timeWindowSeconds":{"type":"number","exclusiveMinimum":0},"bearingLimitsDegrees":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number"}},"amplitudeLimitsDb":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number"}},"iciLimitsSeconds":{"type":"array","minItems":2,"maxItems":2,"items":{"type":"number","exclusiveMinimum":0}},"showEchoes":{"type":"boolean"}},"required":["channelBitmap","timeWindowSeconds","bearingLimitsDegrees","amplitudeLimitsDb","iciLimitsSeconds","showEchoes"]})",
    };
}

} // namespace

void validate_click_detector_settings_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    (void) validated_click_settings(settings_json, settings_version);
}

void validate_click_display_settings_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw ClickDetectorSettingsError(
            "Unsupported Click display settings version");
    }
    const auto settings =
        parse_object(settings_json, "Click display settings");
    require_exact(
        settings,
        {
            "channelBitmap",
            "timeWindowSeconds",
            "bearingLimitsDegrees",
            "amplitudeLimitsDb",
            "iciLimitsSeconds",
            "showEchoes",
        },
        "Click display settings");
    if (unsigned_integer(
            settings.at("channelBitmap"),
            "Click display channelBitmap") > kMaximumBitmap) {
        throw ClickDetectorSettingsError(
            "Click display channelBitmap exceeds 32 channels");
    }
    if (finite_number(
            settings.at("timeWindowSeconds"),
            "Click display timeWindowSeconds") <= 0.0) {
        throw ClickDetectorSettingsError(
            "Click display timeWindowSeconds must be positive");
    }
    validate_pair(
        settings.at("bearingLimitsDegrees"),
        "Click display bearing limits");
    validate_pair(
        settings.at("amplitudeLimitsDb"),
        "Click display amplitude limits");
    validate_pair(
        settings.at("iciLimitsSeconds"),
        "Click display ICI limits",
        true);
    require_boolean(
        settings.at("showEchoes"),
        "Click display showEchoes");
}

std::string click_detector_runtime_settings_json(
    std::string_view settings_json,
    std::uint32_t settings_version,
    std::string_view child_role,
    const core::ArrayConfiguration& array_geometry) {
    const auto settings =
        validated_click_settings(settings_json, settings_version);
    if (child_role == "detector") {
        return settings.at("detector").dump();
    }
    if (child_role == "features") {
        return settings.at("features").dump();
    }
    if (child_role == "classifier") {
        const auto& source = settings.at("classification");
        const auto mode = source.at("mode").get<std::string>();
        return Json{
            {"enabled", source.at("runOnline")},
            {"mode", mode == "none" ? "sweep" : mode},
            {
                "discardUnclassified",
                source.at("discardUnclassified"),
            },
            {
                "checkAllClassifiers",
                source.at("checkAllClassifiers"),
            },
            {
                "amplitudeDbOffsetByChannel",
                source.at("amplitudeDbOffsetByChannel"),
            },
            {
                "types",
                mode == "basic"
                    ? source.at("basicTypes")
                    : source.at("sweepTypes"),
            },
        }.dump();
    }
    if (child_role == "localiser") {
        return localiser_runtime_settings(
            settings,
            array_geometry).dump();
    }
    if (child_role == "train") {
        return settings.at("train").dump();
    }
    throw ClickDetectorSettingsError(
        "Unknown Click Detector runtime child role '" +
        std::string(child_role) + "'");
}

ControlledUnitDescriptor
make_click_detector_controlled_unit_descriptor() {
    return {
        "pamguard.click-detector",
        1,
        {
            "Click Detector",
            "Detectors",
            "clickDetector.ClickControl",
            "direct",
            "Detects and classifies short transient sounds such as echolocation clicks",
            "detectors/clickDetectorHelp/docs/ClickDetector_clickDetector.html",
            {
                "src/PamModel/PamModel.java",
                "src/clickDetector/ClickControl.java",
                "src/clickDetector/ClickDetector.java",
            },
        },
        {
            0,
            std::nullopt,
            {
                RunMode::Normal,
                RunMode::Mixed,
                RunMode::Viewer,
            },
        },
        {
            {
                "rawAudio",
                "Raw audio source",
                DataRoleDirection::Input,
                "pamguard.raw-audio",
                RoleCardinality::ExactlyOne,
                {"sampled"},
                "PamDetection.RawDataUnit",
                "pamguard.acquisition",
            },
            {
                "clicks",
                "Detected clicks",
                DataRoleDirection::Output,
                "pamguard.click",
                RoleCardinality::ExactlyOne,
                {"detections", "waveform", "overlay"},
                {},
                std::nullopt,
            },
            {
                "noise",
                "Noise samples",
                DataRoleDirection::Output,
                "pamguard.click-noise",
                RoleCardinality::ExactlyOne,
                {},
                {},
                std::nullopt,
            },
            {
                "background",
                "Trigger background",
                DataRoleDirection::Output,
                "pamguard.click-trigger-background",
                RoleCardinality::ExactlyOne,
                {},
                {},
                std::nullopt,
            },
            {
                "trigger",
                "Trigger function",
                DataRoleDirection::Output,
                "pamguard.click-trigger-function",
                RoleCardinality::ExactlyOne,
                {},
                {},
                std::nullopt,
            },
            {
                "features",
                "Click features",
                DataRoleDirection::Output,
                "pamguard.click-feature",
                RoleCardinality::ExactlyOne,
                {},
                {},
                std::nullopt,
            },
            {
                "classifications",
                "Click classifications",
                DataRoleDirection::Output,
                "pamguard.click-classification",
                RoleCardinality::ExactlyOne,
                {},
                {},
                std::nullopt,
            },
            {
                "localisations",
                "Click delay localisations",
                DataRoleDirection::Output,
                "pamguard.click-localisation",
                RoleCardinality::ExactlyOne,
                {},
                {},
                std::nullopt,
            },
            {
                "bearings",
                "Click bearings",
                DataRoleDirection::Output,
                "pamguard.click-bearing",
                RoleCardinality::ExactlyOne,
                {},
                {},
                std::nullopt,
            },
            {
                "trains",
                "Legacy click trains",
                DataRoleDirection::Output,
                "pamguard.click-train",
                RoleCardinality::ExactlyOne,
                {},
                {},
                std::nullopt,
            },
        },
        click_settings_descriptor(),
        {
            1,
            {
                {
                    "detector",
                    "pamguard.click-detector",
                    {"/detector", "pamguard.click-detector-settings.v1"},
                    true,
                    AvailabilityStatus::Available,
                    "fixture-parity",
                },
                {
                    "classifier",
                    "pamguard.click-classifier",
                    {"/classification", "pamguard.click-classifier-settings.v1"},
                    true,
                    AvailabilityStatus::Available,
                    "fixture-parity",
                },
                {
                    "features",
                    "pamguard.click-features",
                    {"/features", "pamguard.click-features-settings.v1"},
                    true,
                    AvailabilityStatus::Available,
                    "fixture-parity",
                },
                {
                    "localiser",
                    "pamguard.click-localiser",
                    {"/localisation", "pamguard.click-localiser-settings.v1"},
                    true,
                    AvailabilityStatus::Available,
                    "foundation",
                },
                {
                    "train",
                    "pamguard.click-train",
                    {"/train", "pamguard.click-train-settings.v1"},
                    true,
                    AvailabilityStatus::Available,
                    "fixture-parity",
                },
            },
            {
                {"rawAudio", {"detector", "input"}},
                {"clicks", {"localiser", "accepted"}},
                {"noise", {"detector", "noise"}},
                {"background", {"detector", "background"}},
                {"trigger", {"detector", "trigger"}},
                {"features", {"features", "features"}},
                {
                    "classifications",
                    {"classifier", "classifications"},
                },
                {
                    "localisations",
                    {"localiser", "localisations"},
                },
                {"bearings", {"localiser", "bearings"}},
                {"trains", {"train", "trains"}},
            },
            {
                {
                    "detector-to-classifier",
                    {"detector", "clicks"},
                    {"classifier", "clicks"},
                },
                {
                    "classifier-to-localiser",
                    {"classifier", "accepted"},
                    {"localiser", "clicks"},
                },
                {
                    "localiser-to-features",
                    {"localiser", "accepted"},
                    {"features", "clicks"},
                },
                {
                    "localiser-to-train",
                    {"localiser", "accepted"},
                    {"train", "clicks"},
                },
            },
            {"pamguard.click-display"},
            "pamguard.click-detector.runtime",
        },
        AvailabilityStatus::Available,
        "partial",
    };
}

DisplayProviderDescriptor
make_click_display_provider_descriptor() {
    return {
        "pamguard.click-display",
        1,
        "Click Display",
        "clickDetector.ClickDisplayProvider",
        "clickDetector.ClickBTDisplay",
        "pamguard.click-detector",
        1,
        1,
        false,
        {
            {
                "clicks",
                "Click source",
                DataRoleDirection::Input,
                "pamguard.click",
                RoleCardinality::ExactlyOne,
                {"detections", "waveform", "overlay"},
                "clickDetector.ClickDetection",
                std::nullopt,
            },
        },
        click_display_settings_descriptor(),
        {
            "pamguard.click-display",
            {
                {"clicks", "clicks"},
            },
        },
        AvailabilityStatus::Available,
        "browser-validated",
        {
            "src/clickDetector/ClickControl.java",
            "src/clickDetector/ClickDisplayManager.java",
            "src/clickDetector/ClickBTDisplay.java",
            "src/clickDetector/BTDisplayParameters.java",
        },
    };
}

} // namespace pamguard::project
