#include "pamguard/core/MhtClickTrainSettings.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

#include <json.hpp>

namespace pamguard::core {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumChannels = 32;

Json parse_json(
    std::string_view settings_json,
    std::string_view context) {
    try {
        return Json::parse(settings_json);
    }
    catch (const std::exception& error) {
        throw MhtClickTrainSettingsError(
            std::string(context) +
            " is not valid JSON: " + error.what());
    }
}

void require_version(std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw MhtClickTrainSettingsError(
            "Unsupported Click Train Detector settings version");
    }
}

void require_exact_fields(
    const Json& value,
    const std::set<std::string>& expected,
    std::string_view context) {
    if (!value.is_object() || value.size() != expected.size()) {
        throw MhtClickTrainSettingsError(
            std::string(context) +
            " must contain exactly the supported fields");
    }
    for (const auto& [name, _] : value.items()) {
        if (!expected.contains(name)) {
            throw MhtClickTrainSettingsError(
                std::string(context) +
                " contains unknown field '" + name + "'");
        }
    }
}

bool boolean_value(
    const Json& value,
    std::string_view context) {
    if (!value.is_boolean()) {
        throw MhtClickTrainSettingsError(
            std::string(context) + " must be a boolean");
    }
    return value.get<bool>();
}

double finite_number(
    const Json& value,
    std::string_view context) {
    if (!value.is_number()) {
        throw MhtClickTrainSettingsError(
            std::string(context) + " must be a number");
    }
    const auto result = value.get<double>();
    if (!std::isfinite(result)) {
        throw MhtClickTrainSettingsError(
            std::string(context) + " must be finite");
    }
    return result;
}

double non_negative_number(
    const Json& value,
    std::string_view context) {
    const auto result = finite_number(value, context);
    if (result < 0.0) {
        throw MhtClickTrainSettingsError(
            std::string(context) + " must be non-negative");
    }
    return result;
}

double positive_number(
    const Json& value,
    std::string_view context) {
    const auto result = finite_number(value, context);
    if (!(result > 0.0)) {
        throw MhtClickTrainSettingsError(
            std::string(context) + " must be positive");
    }
    return result;
}

std::size_t bounded_size(
    const Json& value,
    std::size_t minimum,
    std::size_t maximum,
    std::string_view context) {
    if (!value.is_number_integer() &&
        !value.is_number_unsigned()) {
        throw MhtClickTrainSettingsError(
            std::string(context) + " must be an integer");
    }
    try {
        const auto result = value.get<std::uint64_t>();
        if (result < minimum || result > maximum) {
            throw MhtClickTrainSettingsError(
                std::string(context) + " must be between " +
                std::to_string(minimum) + " and " +
                std::to_string(maximum));
        }
        return static_cast<std::size_t>(result);
    }
    catch (const MhtClickTrainSettingsError&) {
        throw;
    }
    catch (const std::exception&) {
        throw MhtClickTrainSettingsError(
            std::string(context) + " must be a non-negative integer");
    }
}

int bounded_integer(
    const Json& value,
    int minimum,
    int maximum,
    std::string_view context) {
    if (!value.is_number_integer()) {
        throw MhtClickTrainSettingsError(
            std::string(context) + " must be an integer");
    }
    try {
        const auto result = value.get<std::int64_t>();
        if (result < minimum || result > maximum) {
            throw MhtClickTrainSettingsError(
                std::string(context) + " must be between " +
                std::to_string(minimum) + " and " +
                std::to_string(maximum));
        }
        return static_cast<int>(result);
    }
    catch (const MhtClickTrainSettingsError&) {
        throw;
    }
    catch (const std::exception&) {
        throw MhtClickTrainSettingsError(
            std::string(context) + " is outside the supported integer range");
    }
}

std::vector<std::uint32_t> channel_groups(
    const Json& value) {
    if (!value.is_array() ||
        value.size() > kMaximumChannels) {
        throw MhtClickTrainSettingsError(
            "Click Train channelGroups must be an array with at most 32 entries");
    }
    std::vector<std::uint32_t> result;
    std::uint32_t used_channels = 0;
    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        if (!value.at(index).is_number_integer() &&
            !value.at(index).is_number_unsigned()) {
            throw MhtClickTrainSettingsError(
                "Click Train channelGroups entries must be integers");
        }
        std::uint64_t group = 0;
        try {
            group = value.at(index).get<std::uint64_t>();
        }
        catch (const std::exception&) {
            throw MhtClickTrainSettingsError(
                "Click Train channelGroups entries must be non-negative 32-bit bitmaps");
        }
        if (group == 0 ||
            group > std::numeric_limits<std::uint32_t>::max()) {
            throw MhtClickTrainSettingsError(
                "Click Train channelGroups entries must be non-zero 32-bit bitmaps");
        }
        const auto bitmap = static_cast<std::uint32_t>(group);
        if ((used_channels & bitmap) != 0) {
            throw MhtClickTrainSettingsError(
                "Click Train channelGroups cannot overlap");
        }
        used_channels |= bitmap;
        result.push_back(bitmap);
    }
    return result;
}

std::vector<int> click_types(const Json& value) {
    if (!value.is_array() || value.size() > 256) {
        throw MhtClickTrainSettingsError(
            "Click Train includedClickTypes must be an array with at most 256 entries");
    }
    std::vector<int> result;
    std::set<int> seen;
    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        const auto type = bounded_integer(
            value.at(index),
            0,
            255,
            "Click Train includedClickTypes[" +
                std::to_string(index) + "]");
        if (!seen.emplace(type).second) {
            throw MhtClickTrainSettingsError(
                "Click Train includedClickTypes must not contain duplicates");
        }
        result.push_back(type);
    }
    return result;
}

std::vector<double> finite_array(
    const Json& value,
    std::size_t minimum,
    std::string_view context) {
    if (!value.is_array() || value.size() < minimum) {
        throw MhtClickTrainSettingsError(
            std::string(context) + " must contain at least " +
            std::to_string(minimum) + " values");
    }
    std::vector<double> result;
    result.reserve(value.size());
    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        result.push_back(finite_number(
            value.at(index),
            std::string(context) + "[" +
                std::to_string(index) + "]"));
    }
    return result;
}

detectors::MhtBearingJumpDirection jump_direction(
    const Json& value) {
    if (!value.is_string()) {
        throw MhtClickTrainSettingsError(
            "Click Train bearing jumpDirection must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "both") {
        return detectors::MhtBearingJumpDirection::Both;
    }
    if (name == "positive") {
        return detectors::MhtBearingJumpDirection::Positive;
    }
    if (name == "negative") {
        return detectors::MhtBearingJumpDirection::Negative;
    }
    throw MhtClickTrainSettingsError(
        "Click Train bearing jumpDirection must be both, positive, or negative");
}

std::string_view jump_direction_name(
    detectors::MhtBearingJumpDirection value) {
    switch (value) {
    case detectors::MhtBearingJumpDirection::Both:
        return "both";
    case detectors::MhtBearingJumpDirection::Positive:
        return "positive";
    case detectors::MhtBearingJumpDirection::Negative:
        return "negative";
    }
    throw MhtClickTrainSettingsError(
        "Click Train bearing jumpDirection cannot be serialized");
}

MhtClickTrainSettings settings_from_value(const Json& value) {
    require_exact_fields(
        value,
        {
            "algorithm",
            "channelGroups",
            "dataSelector",
            "kernel",
            "chi2",
            "classifier",
            "localisation",
        },
        "Click Train Detector settings");
    if (!value.at("algorithm").is_string() ||
        value.at("algorithm").get<std::string>() != "mht") {
        throw MhtClickTrainSettingsError(
            "Click Train Detector algorithm must be mht");
    }

    MhtClickTrainSettings result;
    result.channel_groups =
        channel_groups(value.at("channelGroups"));

    const auto& selector = value.at("dataSelector");
    require_exact_fields(
        selector,
        {
            "enabled",
            "useEchoes",
            "minimumAmplitudeDb",
            "includedClickTypes",
        },
        "Click Train dataSelector");
    result.data_selector.enabled = boolean_value(
        selector.at("enabled"),
        "Click Train dataSelector.enabled");
    result.data_selector.use_echoes = boolean_value(
        selector.at("useEchoes"),
        "Click Train dataSelector.useEchoes");
    result.data_selector.minimum_amplitude_db =
        finite_number(
            selector.at("minimumAmplitudeDb"),
            "Click Train dataSelector.minimumAmplitudeDb");
    if (result.data_selector.minimum_amplitude_db != 0.0) {
        throw MhtClickTrainSettingsError(
            "Click Train dataSelector.minimumAmplitudeDb must remain zero "
            "until click units carry PAMGuard-calibrated amplitude dB");
    }
    result.data_selector.included_click_types =
        click_types(selector.at("includedClickTypes"));

    const auto& kernel = value.at("kernel");
    require_exact_fields(
        kernel,
        {
            "nHold",
            "nPruneback",
            "nPrunebackStart",
            "maxCoast",
        },
        "Click Train kernel");
    result.kernel.n_hold = bounded_size(
        kernel.at("nHold"),
        1,
        std::numeric_limits<std::uint32_t>::max(),
        "Click Train kernel.nHold");
    result.kernel.n_pruneback = bounded_size(
        kernel.at("nPruneback"),
        0,
        std::numeric_limits<std::uint32_t>::max(),
        "Click Train kernel.nPruneback");
    result.kernel.n_pruneback_start = bounded_size(
        kernel.at("nPrunebackStart"),
        0,
        std::numeric_limits<std::uint32_t>::max(),
        "Click Train kernel.nPrunebackStart");
    result.kernel.max_coast = bounded_integer(
        kernel.at("maxCoast"),
        0,
        std::numeric_limits<int>::max(),
        "Click Train kernel.maxCoast");

    const auto& chi2 = value.at("chi2");
    require_exact_fields(
        chi2,
        {
            "maximumIciSeconds",
            "coastPenalty",
            "newTrackPenalty",
            "newTrackClicks",
            "longTrackExponent",
            "lowIciExponent",
            "electricalNoiseFilter",
            "variables",
        },
        "Click Train chi2");
    result.chi2.maximum_ici_seconds = positive_number(
        chi2.at("maximumIciSeconds"),
        "Click Train chi2.maximumIciSeconds");
    result.chi2.coast_penalty = non_negative_number(
        chi2.at("coastPenalty"),
        "Click Train chi2.coastPenalty");
    result.chi2.new_track_penalty = non_negative_number(
        chi2.at("newTrackPenalty"),
        "Click Train chi2.newTrackPenalty");
    result.chi2.new_track_clicks = bounded_size(
        chi2.at("newTrackClicks"),
        0,
        std::numeric_limits<std::uint32_t>::max(),
        "Click Train chi2.newTrackClicks");
    result.chi2.long_track_exponent = non_negative_number(
        chi2.at("longTrackExponent"),
        "Click Train chi2.longTrackExponent");
    result.chi2.low_ici_exponent = non_negative_number(
        chi2.at("lowIciExponent"),
        "Click Train chi2.lowIciExponent");

    const auto& electrical =
        chi2.at("electricalNoiseFilter");
    require_exact_fields(
        electrical,
        {
            "enabled",
            "minimumChi2",
            "dataUnits",
        },
        "Click Train electricalNoiseFilter");
    result.chi2.electrical_noise_filter_enabled =
        boolean_value(
            electrical.at("enabled"),
            "Click Train electricalNoiseFilter.enabled");
    result.chi2.electrical_noise_minimum_chi2 =
        non_negative_number(
            electrical.at("minimumChi2"),
            "Click Train electricalNoiseFilter.minimumChi2");
    result.chi2.electrical_noise_data_units =
        bounded_size(
            electrical.at("dataUnits"),
            1,
            std::numeric_limits<std::uint32_t>::max(),
            "Click Train electricalNoiseFilter.dataUnits");

    const auto& variables = chi2.at("variables");
    require_exact_fields(
        variables,
        {
            "idi",
            "amplitude",
            "bearing",
            "correlation",
            "timeDelay",
            "length",
            "peakFrequency",
        },
        "Click Train chi2.variables");

    const auto common = [](
                            const Json& variable,
                            std::string_view context) {
        require_exact_fields(
            variable,
            {"enabled", "error", "minimumError"},
            context);
        return std::tuple{
            boolean_value(
                variable.at("enabled"),
                std::string(context) + ".enabled"),
            positive_number(
                variable.at("error"),
                std::string(context) + ".error"),
            positive_number(
                variable.at("minimumError"),
                std::string(context) + ".minimumError"),
        };
    };

    const auto& idi = variables.at("idi");
    require_exact_fields(
        idi,
        {
            "enabled",
            "error",
            "minimumError",
            "minimumIdiSeconds",
        },
        "Click Train chi2.variables.idi");
    result.chi2.idi.enabled = boolean_value(
        idi.at("enabled"),
        "Click Train chi2.variables.idi.enabled");
    result.chi2.idi.error = positive_number(
        idi.at("error"),
        "Click Train chi2.variables.idi.error");
    result.chi2.idi.min_error = positive_number(
        idi.at("minimumError"),
        "Click Train chi2.variables.idi.minimumError");
    result.chi2.idi.min_idi_seconds =
        non_negative_number(
            idi.at("minimumIdiSeconds"),
            "Click Train chi2.variables.idi.minimumIdiSeconds");

    const auto& amplitude = variables.at("amplitude");
    require_exact_fields(
        amplitude,
        {
            "enabled",
            "error",
            "minimumError",
            "jumpEnabled",
            "maximumJumpDb",
        },
        "Click Train chi2.variables.amplitude");
    result.chi2.amplitude.enabled = boolean_value(
        amplitude.at("enabled"),
        "Click Train chi2.variables.amplitude.enabled");
    result.chi2.amplitude.error = positive_number(
        amplitude.at("error"),
        "Click Train chi2.variables.amplitude.error");
    result.chi2.amplitude.min_error = positive_number(
        amplitude.at("minimumError"),
        "Click Train chi2.variables.amplitude.minimumError");
    result.chi2.amplitude.jump_enabled = boolean_value(
        amplitude.at("jumpEnabled"),
        "Click Train chi2.variables.amplitude.jumpEnabled");
    result.chi2.amplitude.maximum_jump_db =
        non_negative_number(
            amplitude.at("maximumJumpDb"),
            "Click Train chi2.variables.amplitude.maximumJumpDb");

    const auto& bearing = variables.at("bearing");
    require_exact_fields(
        bearing,
        {
            "enabled",
            "errorRadians",
            "minimumErrorRadians",
            "jumpEnabled",
            "maximumJumpRadians",
            "jumpDirection",
        },
        "Click Train chi2.variables.bearing");
    result.chi2.bearing.enabled = boolean_value(
        bearing.at("enabled"),
        "Click Train chi2.variables.bearing.enabled");
    result.chi2.bearing.error_radians = positive_number(
        bearing.at("errorRadians"),
        "Click Train chi2.variables.bearing.errorRadians");
    result.chi2.bearing.min_error_radians =
        positive_number(
            bearing.at("minimumErrorRadians"),
            "Click Train chi2.variables.bearing.minimumErrorRadians");
    result.chi2.bearing.jump_enabled = boolean_value(
        bearing.at("jumpEnabled"),
        "Click Train chi2.variables.bearing.jumpEnabled");
    result.chi2.bearing.maximum_jump_radians =
        non_negative_number(
            bearing.at("maximumJumpRadians"),
            "Click Train chi2.variables.bearing.maximumJumpRadians");
    result.chi2.bearing.jump_direction =
        jump_direction(bearing.at("jumpDirection"));

    const auto [correlation_enabled,
                correlation_error,
                correlation_minimum] =
        common(
            variables.at("correlation"),
            "Click Train chi2.variables.correlation");
    result.chi2.correlation.enabled =
        correlation_enabled;
    result.chi2.correlation.error = correlation_error;
    result.chi2.correlation.min_error =
        correlation_minimum;

    const auto [time_delay_enabled,
                time_delay_error,
                time_delay_minimum] =
        common(
            variables.at("timeDelay"),
            "Click Train chi2.variables.timeDelay");
    result.chi2.time_delay.enabled =
        time_delay_enabled;
    result.chi2.time_delay.error = time_delay_error;
    result.chi2.time_delay.min_error =
        time_delay_minimum;

    const auto [length_enabled,
                length_error,
                length_minimum] =
        common(
            variables.at("length"),
            "Click Train chi2.variables.length");
    result.chi2.length.enabled = length_enabled;
    result.chi2.length.error = length_error;
    result.chi2.length.min_error = length_minimum;

    const auto [frequency_enabled,
                frequency_error,
                frequency_minimum] =
        common(
            variables.at("peakFrequency"),
            "Click Train chi2.variables.peakFrequency");
    result.chi2.peak_frequency.enabled =
        frequency_enabled;
    result.chi2.peak_frequency.error = frequency_error;
    result.chi2.peak_frequency.min_error =
        frequency_minimum;

    const auto& classifier = value.at("classifier");
    require_exact_fields(
        classifier,
        {
            "runClassifier",
            "preClassifier",
            "idi",
            "bearing",
            "spectrumTemplate",
        },
        "Click Train classifier");
    result.classifier.run_classifier = boolean_value(
        classifier.at("runClassifier"),
        "Click Train classifier.runClassifier");

    const auto& pre = classifier.at("preClassifier");
    require_exact_fields(
        pre,
        {
            "chi2Threshold",
            "minimumClicks",
            "minimumSelectedPercentage",
            "minimumTimeSeconds",
            "speciesFlag",
        },
        "Click Train preClassifier");
    result.classifier.pre.chi2_threshold =
        non_negative_number(
            pre.at("chi2Threshold"),
            "Click Train preClassifier.chi2Threshold");
    result.classifier.pre.minimum_clicks =
        bounded_size(
            pre.at("minimumClicks"),
            0,
            std::numeric_limits<std::uint32_t>::max(),
            "Click Train preClassifier.minimumClicks");
    result.classifier.pre.minimum_selected_percentage =
        non_negative_number(
            pre.at("minimumSelectedPercentage"),
            "Click Train preClassifier.minimumSelectedPercentage");
    if (result.classifier.pre.minimum_selected_percentage !=
        0.0) {
        throw MhtClickTrainSettingsError(
            "Click Train preClassifier.minimumSelectedPercentage is unsupported until per-classifier data selectors are modeled; use Java's default zero");
    }
    result.classifier.pre.minimum_time_seconds =
        non_negative_number(
            pre.at("minimumTimeSeconds"),
            "Click Train preClassifier.minimumTimeSeconds");
    result.classifier.pre.species_flag = bounded_integer(
        pre.at("speciesFlag"),
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max(),
        "Click Train preClassifier.speciesFlag");

    const auto& idi_classifier = classifier.at("idi");
    require_exact_fields(
        idi_classifier,
        {
            "enabled",
            "useMedianIdi",
            "minimumMedianIdi",
            "maximumMedianIdi",
            "useMeanIdi",
            "minimumMeanIdi",
            "maximumMeanIdi",
            "useStdIdi",
            "minimumStdIdi",
            "maximumStdIdi",
            "speciesFlag",
        },
        "Click Train IDI classifier");
    auto& idi_result = result.classifier.idi;
    idi_result.enabled = boolean_value(
        idi_classifier.at("enabled"),
        "Click Train classifier.idi.enabled");
    idi_result.use_median_idi = boolean_value(
        idi_classifier.at("useMedianIdi"),
        "Click Train classifier.idi.useMedianIdi");
    idi_result.minimum_median_idi = non_negative_number(
        idi_classifier.at("minimumMedianIdi"),
        "Click Train classifier.idi.minimumMedianIdi");
    idi_result.maximum_median_idi = non_negative_number(
        idi_classifier.at("maximumMedianIdi"),
        "Click Train classifier.idi.maximumMedianIdi");
    idi_result.use_mean_idi = boolean_value(
        idi_classifier.at("useMeanIdi"),
        "Click Train classifier.idi.useMeanIdi");
    idi_result.minimum_mean_idi = non_negative_number(
        idi_classifier.at("minimumMeanIdi"),
        "Click Train classifier.idi.minimumMeanIdi");
    idi_result.maximum_mean_idi = non_negative_number(
        idi_classifier.at("maximumMeanIdi"),
        "Click Train classifier.idi.maximumMeanIdi");
    idi_result.use_std_idi = boolean_value(
        idi_classifier.at("useStdIdi"),
        "Click Train classifier.idi.useStdIdi");
    idi_result.minimum_std_idi = non_negative_number(
        idi_classifier.at("minimumStdIdi"),
        "Click Train classifier.idi.minimumStdIdi");
    idi_result.maximum_std_idi = non_negative_number(
        idi_classifier.at("maximumStdIdi"),
        "Click Train classifier.idi.maximumStdIdi");
    idi_result.species_flag = bounded_integer(
        idi_classifier.at("speciesFlag"),
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max(),
        "Click Train classifier.idi.speciesFlag");
    if (idi_result.minimum_median_idi >
            idi_result.maximum_median_idi ||
        idi_result.minimum_mean_idi >
            idi_result.maximum_mean_idi ||
        idi_result.minimum_std_idi >
            idi_result.maximum_std_idi) {
        throw MhtClickTrainSettingsError(
            "Click Train IDI classifier limits must be ordered");
    }

    const auto& bearing_classifier =
        classifier.at("bearing");
    require_exact_fields(
        bearing_classifier,
        {
            "enabled",
            "minimumBearingRadians",
            "maximumBearingRadians",
            "useMean",
            "minimumMeanDerivative",
            "maximumMeanDerivative",
            "useMedian",
            "minimumMedianDerivative",
            "maximumMedianDerivative",
            "useStd",
            "minimumStdDerivative",
            "maximumStdDerivative",
            "speciesFlag",
        },
        "Click Train bearing classifier");
    auto& bearing_result = result.classifier.bearing;
    bearing_result.enabled = boolean_value(
        bearing_classifier.at("enabled"),
        "Click Train classifier.bearing.enabled");
    bearing_result.minimum_bearing_radians =
        finite_number(
            bearing_classifier.at("minimumBearingRadians"),
            "Click Train classifier.bearing.minimumBearingRadians");
    bearing_result.maximum_bearing_radians =
        finite_number(
            bearing_classifier.at("maximumBearingRadians"),
            "Click Train classifier.bearing.maximumBearingRadians");
    bearing_result.use_mean = boolean_value(
        bearing_classifier.at("useMean"),
        "Click Train classifier.bearing.useMean");
    bearing_result.minimum_mean_derivative =
        finite_number(
            bearing_classifier.at("minimumMeanDerivative"),
            "Click Train classifier.bearing.minimumMeanDerivative");
    bearing_result.maximum_mean_derivative =
        finite_number(
            bearing_classifier.at("maximumMeanDerivative"),
            "Click Train classifier.bearing.maximumMeanDerivative");
    bearing_result.use_median = boolean_value(
        bearing_classifier.at("useMedian"),
        "Click Train classifier.bearing.useMedian");
    bearing_result.minimum_median_derivative =
        finite_number(
            bearing_classifier.at("minimumMedianDerivative"),
            "Click Train classifier.bearing.minimumMedianDerivative");
    bearing_result.maximum_median_derivative =
        finite_number(
            bearing_classifier.at("maximumMedianDerivative"),
            "Click Train classifier.bearing.maximumMedianDerivative");
    bearing_result.use_std = boolean_value(
        bearing_classifier.at("useStd"),
        "Click Train classifier.bearing.useStd");
    bearing_result.minimum_std_derivative =
        finite_number(
            bearing_classifier.at("minimumStdDerivative"),
            "Click Train classifier.bearing.minimumStdDerivative");
    bearing_result.maximum_std_derivative =
        finite_number(
            bearing_classifier.at("maximumStdDerivative"),
            "Click Train classifier.bearing.maximumStdDerivative");
    bearing_result.species_flag = bounded_integer(
        bearing_classifier.at("speciesFlag"),
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max(),
        "Click Train classifier.bearing.speciesFlag");
    if (bearing_result.minimum_bearing_radians >
            bearing_result.maximum_bearing_radians ||
        bearing_result.minimum_mean_derivative >
            bearing_result.maximum_mean_derivative ||
        bearing_result.minimum_median_derivative >
            bearing_result.maximum_median_derivative ||
        bearing_result.minimum_std_derivative >
            bearing_result.maximum_std_derivative) {
        throw MhtClickTrainSettingsError(
            "Click Train bearing classifier limits must be ordered");
    }

    const auto& spectrum =
        classifier.at("spectrumTemplate");
    require_exact_fields(
        spectrum,
        {
            "enabled",
            "name",
            "sampleRateHz",
            "spectrum",
            "correlationThreshold",
            "speciesFlag",
        },
        "Click Train spectrum-template classifier");
    auto& spectrum_result =
        result.classifier.spectrum_template;
    spectrum_result.enabled = boolean_value(
        spectrum.at("enabled"),
        "Click Train classifier.spectrumTemplate.enabled");
    if (!spectrum.at("name").is_string() ||
        spectrum.at("name").get<std::string>().empty()) {
        throw MhtClickTrainSettingsError(
            "Click Train spectrum-template name must be a non-empty string");
    }
    spectrum_result.template_name =
        spectrum.at("name").get<std::string>();
    spectrum_result.template_sample_rate_hz =
        positive_number(
            spectrum.at("sampleRateHz"),
            "Click Train classifier.spectrumTemplate.sampleRateHz");
    spectrum_result.template_spectrum = finite_array(
        spectrum.at("spectrum"),
        2,
        "Click Train classifier.spectrumTemplate.spectrum");
    spectrum_result.correlation_threshold =
        finite_number(
            spectrum.at("correlationThreshold"),
            "Click Train classifier.spectrumTemplate.correlationThreshold");
    if (spectrum_result.correlation_threshold < -1.0 ||
        spectrum_result.correlation_threshold > 1.0) {
        throw MhtClickTrainSettingsError(
            "Click Train spectrum-template correlationThreshold must be between -1 and 1");
    }
    spectrum_result.species_flag = bounded_integer(
        spectrum.at("speciesFlag"),
        std::numeric_limits<int>::min(),
        std::numeric_limits<int>::max(),
        "Click Train classifier.spectrumTemplate.speciesFlag");

    const auto& localisation = value.at("localisation");
    require_exact_fields(
        localisation,
        {
            "enabled",
            "minimumDataUnits",
            "minimumAngleRangeRadians",
        },
        "Click Train localisation");
    result.localisation.enabled = boolean_value(
        localisation.at("enabled"),
        "Click Train localisation.enabled");
    result.localisation.minimum_data_units = bounded_size(
        localisation.at("minimumDataUnits"),
        0,
        std::numeric_limits<std::uint32_t>::max(),
        "Click Train localisation.minimumDataUnits");
    result.localisation.minimum_angle_range_radians =
        non_negative_number(
            localisation.at("minimumAngleRangeRadians"),
            "Click Train localisation.minimumAngleRangeRadians");
    if (result.localisation.enabled) {
        throw MhtClickTrainSettingsError(
            "Click Train target-motion localisation is not implemented by the C++ runtime; localisation.enabled must remain false");
    }
    return result;
}

Json settings_to_value(const MhtClickTrainSettings& settings) {
    Json result{
        {"algorithm", "mht"},
        {"channelGroups", settings.channel_groups},
        {
            "dataSelector",
            {
                {"enabled", settings.data_selector.enabled},
                {"useEchoes", settings.data_selector.use_echoes},
                {"minimumAmplitudeDb",
                 settings.data_selector.minimum_amplitude_db},
                {"includedClickTypes",
                 settings.data_selector.included_click_types},
            },
        },
        {
            "kernel",
            {
                {"nHold", settings.kernel.n_hold},
                {"nPruneback", settings.kernel.n_pruneback},
                {"nPrunebackStart",
                 settings.kernel.n_pruneback_start},
                {"maxCoast", settings.kernel.max_coast},
            },
        },
        {
            "chi2",
            {
                {"maximumIciSeconds",
                 settings.chi2.maximum_ici_seconds},
                {"coastPenalty",
                 settings.chi2.coast_penalty},
                {"newTrackPenalty",
                 settings.chi2.new_track_penalty},
                {"newTrackClicks",
                 settings.chi2.new_track_clicks},
                {"longTrackExponent",
                 settings.chi2.long_track_exponent},
                {"lowIciExponent",
                 settings.chi2.low_ici_exponent},
                {
                    "electricalNoiseFilter",
                    {
                        {
                            "enabled",
                            settings.chi2
                                .electrical_noise_filter_enabled,
                        },
                        {
                            "minimumChi2",
                            settings.chi2
                                .electrical_noise_minimum_chi2,
                        },
                        {
                            "dataUnits",
                            settings.chi2
                                .electrical_noise_data_units,
                        },
                    },
                },
                {
                    "variables",
                    {
                        {
                            "idi",
                            {
                                {"enabled",
                                 settings.chi2.idi.enabled},
                                {"error",
                                 settings.chi2.idi.error},
                                {"minimumError",
                                 settings.chi2.idi.min_error},
                                {"minimumIdiSeconds",
                                 settings.chi2.idi
                                     .min_idi_seconds},
                            },
                        },
                        {
                            "amplitude",
                            {
                                {"enabled",
                                 settings.chi2.amplitude
                                     .enabled},
                                {"error",
                                 settings.chi2.amplitude.error},
                                {"minimumError",
                                 settings.chi2.amplitude
                                     .min_error},
                                {"jumpEnabled",
                                 settings.chi2.amplitude
                                     .jump_enabled},
                                {"maximumJumpDb",
                                 settings.chi2.amplitude
                                     .maximum_jump_db},
                            },
                        },
                        {
                            "bearing",
                            {
                                {"enabled",
                                 settings.chi2.bearing.enabled},
                                {"errorRadians",
                                 settings.chi2.bearing
                                     .error_radians},
                                {"minimumErrorRadians",
                                 settings.chi2.bearing
                                     .min_error_radians},
                                {"jumpEnabled",
                                 settings.chi2.bearing
                                     .jump_enabled},
                                {"maximumJumpRadians",
                                 settings.chi2.bearing
                                     .maximum_jump_radians},
                                {"jumpDirection",
                                 jump_direction_name(
                                     settings.chi2.bearing
                                         .jump_direction)},
                            },
                        },
                        {
                            "correlation",
                            {
                                {"enabled",
                                 settings.chi2.correlation
                                     .enabled},
                                {"error",
                                 settings.chi2.correlation.error},
                                {"minimumError",
                                 settings.chi2.correlation
                                     .min_error},
                            },
                        },
                        {
                            "timeDelay",
                            {
                                {"enabled",
                                 settings.chi2.time_delay
                                     .enabled},
                                {"error",
                                 settings.chi2.time_delay.error},
                                {"minimumError",
                                 settings.chi2.time_delay
                                     .min_error},
                            },
                        },
                        {
                            "length",
                            {
                                {"enabled",
                                 settings.chi2.length.enabled},
                                {"error",
                                 settings.chi2.length.error},
                                {"minimumError",
                                 settings.chi2.length
                                     .min_error},
                            },
                        },
                        {
                            "peakFrequency",
                            {
                                {"enabled",
                                 settings.chi2.peak_frequency
                                     .enabled},
                                {"error",
                                 settings.chi2.peak_frequency
                                     .error},
                                {"minimumError",
                                 settings.chi2.peak_frequency
                                     .min_error},
                            },
                        },
                    },
                },
            },
        },
        {
            "classifier",
            {
                {"runClassifier",
                 settings.classifier.run_classifier},
                {
                    "preClassifier",
                    {
                        {"chi2Threshold",
                         settings.classifier.pre
                             .chi2_threshold},
                        {"minimumClicks",
                         settings.classifier.pre
                             .minimum_clicks},
                        {"minimumSelectedPercentage",
                         settings.classifier.pre
                             .minimum_selected_percentage},
                        {"minimumTimeSeconds",
                         settings.classifier.pre
                             .minimum_time_seconds},
                        {"speciesFlag",
                         settings.classifier.pre
                             .species_flag},
                    },
                },
                {
                    "idi",
                    {
                        {"enabled",
                         settings.classifier.idi.enabled},
                        {"useMedianIdi",
                         settings.classifier.idi
                             .use_median_idi},
                        {"minimumMedianIdi",
                         settings.classifier.idi
                             .minimum_median_idi},
                        {"maximumMedianIdi",
                         settings.classifier.idi
                             .maximum_median_idi},
                        {"useMeanIdi",
                         settings.classifier.idi
                             .use_mean_idi},
                        {"minimumMeanIdi",
                         settings.classifier.idi
                             .minimum_mean_idi},
                        {"maximumMeanIdi",
                         settings.classifier.idi
                             .maximum_mean_idi},
                        {"useStdIdi",
                         settings.classifier.idi
                             .use_std_idi},
                        {"minimumStdIdi",
                         settings.classifier.idi
                             .minimum_std_idi},
                        {"maximumStdIdi",
                         settings.classifier.idi
                             .maximum_std_idi},
                        {"speciesFlag",
                         settings.classifier.idi
                             .species_flag},
                    },
                },
                {
                    "bearing",
                    {
                        {"enabled",
                         settings.classifier.bearing.enabled},
                        {"minimumBearingRadians",
                         settings.classifier.bearing
                             .minimum_bearing_radians},
                        {"maximumBearingRadians",
                         settings.classifier.bearing
                             .maximum_bearing_radians},
                        {"useMean",
                         settings.classifier.bearing.use_mean},
                        {"minimumMeanDerivative",
                         settings.classifier.bearing
                             .minimum_mean_derivative},
                        {"maximumMeanDerivative",
                         settings.classifier.bearing
                             .maximum_mean_derivative},
                        {"useMedian",
                         settings.classifier.bearing
                             .use_median},
                        {"minimumMedianDerivative",
                         settings.classifier.bearing
                             .minimum_median_derivative},
                        {"maximumMedianDerivative",
                         settings.classifier.bearing
                             .maximum_median_derivative},
                        {"useStd",
                         settings.classifier.bearing.use_std},
                        {"minimumStdDerivative",
                         settings.classifier.bearing
                             .minimum_std_derivative},
                        {"maximumStdDerivative",
                         settings.classifier.bearing
                             .maximum_std_derivative},
                        {"speciesFlag",
                         settings.classifier.bearing
                             .species_flag},
                    },
                },
                {
                    "spectrumTemplate",
                    {
                        {"enabled",
                         settings.classifier
                             .spectrum_template.enabled},
                        {"name",
                         settings.classifier
                             .spectrum_template.template_name},
                        {"sampleRateHz",
                         settings.classifier.spectrum_template
                             .template_sample_rate_hz},
                        {"spectrum",
                         settings.classifier.spectrum_template
                             .template_spectrum},
                        {"correlationThreshold",
                         settings.classifier.spectrum_template
                             .correlation_threshold},
                        {"speciesFlag",
                         settings.classifier.spectrum_template
                             .species_flag},
                    },
                },
            },
        },
        {
            "localisation",
            {
                {"enabled", settings.localisation.enabled},
                {"minimumDataUnits",
                 settings.localisation.minimum_data_units},
                {"minimumAngleRangeRadians",
                 settings.localisation
                     .minimum_angle_range_radians},
            },
        },
    };
    (void) settings_from_value(result);
    return result;
}

} // namespace

MhtTemplateClassifierSettings::
    MhtTemplateClassifierSettings()
    : template_spectrum{
          0.0207928796815748,
          0.0306907634391936,
          0.0542618013334441,
          0.0927715736291923,
          0.160880226335102,
          0.296684784810738,
          0.597646428735672,
          1.30240513409102,
          2.89418728104064,
          5.90182387336775,
          9.56798776063848,
          10.8497298549224,
          10.6268357383588,
          7.67719642764775,
          4.25588468454799,
          2.03543953486809,
          0.944338665649875,
          0.464770071613377,
          0.254353569529111,
          0.155756953724082,
          0.105040575926229,
          0.0764551025180798,
          0.0590657823674759,
          0.0478494061986734,
          0.0403052031330920,
          0.0350966305067761,
          0.0314672978023124,
          0.0289713012337297,
          0.0273407573040125,
          0.0264177207215999,
      } {}

MhtClickTrainSettings
mht_click_train_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(settings_version);
    return settings_from_value(parse_json(
        settings_json,
        "Click Train Detector settings"));
}

std::string mht_click_train_settings_to_json(
    const MhtClickTrainSettings& settings,
    std::uint32_t settings_version) {
    require_version(settings_version);
    return settings_to_value(settings).dump();
}

std::string mht_click_train_default_settings_json() {
    return mht_click_train_settings_to_json(
        MhtClickTrainSettings{},
        1);
}

std::string mht_click_train_runtime_settings_json(
    const MhtClickTrainSettings& settings) {
    // Validate before adapting so no low-level-only field can bypass the
    // canonical contract.
    (void) settings_from_value(settings_to_value(settings));
    const auto& chi2 = settings.chi2;
    const auto& classifier = settings.classifier;
    return Json{
        // MHTClickTrainAlgorithm.saveClickTrain hard-codes three.
        {"minClicks", 3},
        {"channelGroups", settings.channel_groups},
        {
            "dataSelector",
            {
                {"enabled", settings.data_selector.enabled},
                {"useEchoes", settings.data_selector.use_echoes},
                {"minimumAmplitudeDb",
                 settings.data_selector.minimum_amplitude_db},
                {"includedClickTypes",
                 settings.data_selector.included_click_types},
            },
        },
        {
            "kernel",
            {
                {"nHold", settings.kernel.n_hold},
                {"nPruneback", settings.kernel.n_pruneback},
                {"nPrunebackStart",
                 settings.kernel.n_pruneback_start},
                {"maxCoast", settings.kernel.max_coast},
            },
        },
        {
            "chi2",
            {
                {"enableIdi", chi2.idi.enabled},
                {"enableAmplitude", chi2.amplitude.enabled},
                {"enableBearing", chi2.bearing.enabled},
                {"enableCorrelation",
                 chi2.correlation.enabled},
                {"enableTimeDelay",
                 chi2.time_delay.enabled},
                {"enableLength", chi2.length.enabled},
                {"enablePeakFrequency",
                 chi2.peak_frequency.enabled},
                {"correlationFftLength", 512},
                {"coastPenalty", chi2.coast_penalty},
                {"newTrackPenalty",
                 chi2.new_track_penalty},
                {"newTrackN", chi2.new_track_clicks},
                {"maxIciSeconds",
                 chi2.maximum_ici_seconds},
                {"lowIciExponent",
                 chi2.low_ici_exponent},
                {"longTrackExponent",
                 chi2.long_track_exponent},
                {"junkTrackPenalty", 20000000.0},
                // Java's exact 2E17 sentinel is integral and fits int64.
                // Keep it as an integer JSON token so project-authority
                // canonicalisation does not have to accept a lossy-looking
                // binary64 integer above 2^53 - 1.
                {"maxChi", std::int64_t{200000000000000000LL}},
                {"useElectricalNoiseFilter",
                 chi2.electrical_noise_filter_enabled},
                {"electricalNoiseMinChi2",
                 chi2.electrical_noise_minimum_chi2},
                {"electricalNoiseNDataUnits",
                 chi2.electrical_noise_data_units},
                {
                    "idi",
                    {
                        {"error", chi2.idi.error},
                        {"minimumError",
                         chi2.idi.min_error},
                        {"minimumIdiSeconds",
                         chi2.idi.min_idi_seconds},
                    },
                },
                {
                    "amplitude",
                    {
                        {"error", chi2.amplitude.error},
                        {"minimumError",
                         chi2.amplitude.min_error},
                        {"jumpEnabled",
                         chi2.amplitude.jump_enabled},
                        {"maximumJumpDb",
                         chi2.amplitude.maximum_jump_db},
                    },
                },
                {
                    "bearing",
                    {
                        {"errorRadians",
                         chi2.bearing.error_radians},
                        {"minimumErrorRadians",
                         chi2.bearing.min_error_radians},
                        {"jumpEnabled",
                         chi2.bearing.jump_enabled},
                        {"maximumJumpRadians",
                         chi2.bearing.maximum_jump_radians},
                        {"jumpDirection",
                         jump_direction_name(
                             chi2.bearing.jump_direction)},
                    },
                },
                {
                    "correlation",
                    {
                        {"error", chi2.correlation.error},
                        {"minimumError",
                         chi2.correlation.min_error},
                    },
                },
                {
                    "timeDelay",
                    {
                        {"error", chi2.time_delay.error},
                        {"minimumError",
                         chi2.time_delay.min_error},
                    },
                },
                {
                    "length",
                    {
                        {"error", chi2.length.error},
                        {"minimumError",
                         chi2.length.min_error},
                    },
                },
                {
                    "peakFrequency",
                    {
                        {"error",
                         chi2.peak_frequency.error},
                        {"minimumError",
                         chi2.peak_frequency.min_error},
                    },
                },
            },
        },
        {
            "classifier",
            {
                {"enabled", classifier.run_classifier},
                // Fixed implementation detail, not a Java operator setting.
                {"averageSpectrumFftLength", 256},
                {
                    "pre",
                    {
                        {"chi2Threshold",
                         classifier.pre.chi2_threshold},
                        {"minClicks",
                         classifier.pre.minimum_clicks},
                        {"minTimeSeconds",
                         classifier.pre.minimum_time_seconds},
                        {"speciesFlag",
                         classifier.pre.species_flag},
                    },
                },
                {
                    "idi",
                    {
                        {"enabled",
                         classifier.idi.enabled},
                        {"useMedianIdi",
                         classifier.idi.use_median_idi},
                        {"minMedianIdi",
                         classifier.idi.minimum_median_idi},
                        {"maxMedianIdi",
                         classifier.idi.maximum_median_idi},
                        {"useMeanIdi",
                         classifier.idi.use_mean_idi},
                        {"minMeanIdi",
                         classifier.idi.minimum_mean_idi},
                        {"maxMeanIdi",
                         classifier.idi.maximum_mean_idi},
                        {"useStdIdi",
                         classifier.idi.use_std_idi},
                        {"minStdIdi",
                         classifier.idi.minimum_std_idi},
                        {"maxStdIdi",
                         classifier.idi.maximum_std_idi},
                        {"speciesFlag",
                         classifier.idi.species_flag},
                    },
                },
                {
                    "bearing",
                    {
                        {"enabled",
                         classifier.bearing.enabled},
                        {"bearingLimitMinRadians",
                         classifier.bearing
                             .minimum_bearing_radians},
                        {"bearingLimitMaxRadians",
                         classifier.bearing
                             .maximum_bearing_radians},
                        {"useMean",
                         classifier.bearing.use_mean},
                        {"minMeanDerivative",
                         classifier.bearing
                             .minimum_mean_derivative},
                        {"maxMeanDerivative",
                         classifier.bearing
                             .maximum_mean_derivative},
                        {"useMedian",
                         classifier.bearing.use_median},
                        {"minMedianDerivative",
                         classifier.bearing
                             .minimum_median_derivative},
                        {"maxMedianDerivative",
                         classifier.bearing
                             .maximum_median_derivative},
                        {"useStd",
                         classifier.bearing.use_std},
                        {"minStdDerivative",
                         classifier.bearing
                             .minimum_std_derivative},
                        {"maxStdDerivative",
                         classifier.bearing
                             .maximum_std_derivative},
                        {"speciesFlag",
                         classifier.bearing.species_flag},
                    },
                },
                {
                    "template",
                    {
                        {"enabled",
                         classifier.spectrum_template.enabled},
                        {"templateSpectrum",
                         classifier.spectrum_template
                             .template_spectrum},
                        {"templateSampleRateHz",
                         classifier.spectrum_template
                             .template_sample_rate_hz},
                        {"correlationThreshold",
                         classifier.spectrum_template
                             .correlation_threshold},
                        {"speciesFlag",
                         classifier.spectrum_template
                             .species_flag},
                    },
                },
            },
        },
    }.dump();
}

bool mht_click_train_has_channel_groups(
    const MhtClickTrainSettings& settings) noexcept {
    return !settings.channel_groups.empty();
}

bool mht_click_train_requires_features(
    const MhtClickTrainSettings& settings) noexcept {
    return settings.chi2.peak_frequency.enabled;
}

bool mht_click_train_requires_localisations(
    const MhtClickTrainSettings& settings) noexcept {
    return settings.chi2.time_delay.enabled;
}

bool mht_click_train_requires_bearings(
    const MhtClickTrainSettings& settings) noexcept {
    return settings.chi2.bearing.enabled ||
        (settings.classifier.run_classifier &&
         settings.classifier.bearing.enabled);
}

std::string_view
mht_click_train_settings_schema_json() noexcept {
    return R"({
      "$schema":"https://json-schema.org/draft/2020-12/schema",
      "x-pamguard-authority":{
        "version":"2.02.18e",
        "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
        "controlClass":"clickTrainDetector.ClickTrainControl",
        "settingsClass":"clickTrainDetector.ClickTrainParams",
        "algorithmSettingsClass":"clickTrainDetector.clickTrainAlgorithms.mht.MHTParams",
        "kernelSettingsClass":"clickTrainDetector.clickTrainAlgorithms.mht.MHTKernelParams",
        "chi2SettingsClass":"clickTrainDetector.clickTrainAlgorithms.mht.StandardMHTChi2Params",
        "dialogClass":"clickTrainDetector.layout.ClickTrainAlgorithmPaneFX"
      },
      "x-pamguard-portable-deviations":[
        "ClickTrainParams.dataSourceName and dataSourceIndex are represented by the public clicks binding",
        "ctDetectorType 0 is represented by the stable algorithm name mht; PAMGuard 2.02.18e instantiates no other algorithm",
        "Java null source-selector species arrays are normalized to an empty includedClickTypes array meaning all types",
        "source-selector event membership, species weights, min-ICI and score-by-amplitude fields are absent because the live ClickDataSelector scoring path does not use them or the C++ click unit has no offline-event owner; minimumAmplitudeDb is retained only at Java's zero default until click units carry PAMGuard-calibrated amplitude dB",
        "StandardMHTChi2Params.useCorrelation and CorrelationChi2Params.useFilter/fftFilterParams are rejected because the C++ port has no Java-equivalent filtered ICI/correlation path",
        "deprecated longTrackBonus, lowTrackICIBonus, maxICIMultipler and errLimits are not runtime settings",
        "chi2 variable names, units and errorScaleValue are fixed presentation metadata rather than portable scientific settings",
        "classifierName and random uniqueID are excluded; they are Java pane/data-selector identities, not scientific classifier inputs",
        "the first portable species-classifier slice exposes one IDI, bearing and spectrum-template classifier; Java arbitrary classifier arrays and StandardClassifier composites are not yet represented",
        "minimumSelectedPercentage is retained only at Java's zero default and non-zero values are rejected until per-classifier data selectors are modeled",
        "CTLocParams defaults are visible, but localisation.enabled=true is rejected until a target-motion click-train localisation runtime exists",
        "the low-level average-spectrum FFT length is a fixed port implementation detail and is not exposed as a Java setting",
        "database/binary/JSON logging, offline-task state, warnings, side-panel counters and display graphics are outside the scientific runtime contract"
      ],
      "type":"object",
      "additionalProperties":false,
      "properties":{
        "algorithm":{"const":"mht"},
        "channelGroups":{
          "type":"array","maxItems":32,"uniqueItems":true,
          "items":{"type":"integer","minimum":1,"maximum":4294967295}
        },
        "dataSelector":{
          "type":"object","additionalProperties":false,
          "properties":{
            "enabled":{"type":"boolean"},
            "useEchoes":{"type":"boolean"},
            "minimumAmplitudeDb":{
              "const":0,
              "description":"Non-zero calibrated dB selection is unsupported until click units carry PAMGuard-calibrated amplitude"
            },
            "includedClickTypes":{
              "type":"array","maxItems":256,"uniqueItems":true,
              "items":{"type":"integer","minimum":0,"maximum":255}
            }
          },
          "required":["enabled","useEchoes","minimumAmplitudeDb","includedClickTypes"]
        },
        "kernel":{
          "type":"object","additionalProperties":false,
          "properties":{
            "nHold":{"type":"integer","minimum":1},
            "nPruneback":{"type":"integer","minimum":0},
            "nPrunebackStart":{"type":"integer","minimum":0},
            "maxCoast":{"type":"integer","minimum":0}
          },
          "required":["nHold","nPruneback","nPrunebackStart","maxCoast"]
        },
        "chi2":{
          "type":"object","additionalProperties":false,
          "properties":{
            "maximumIciSeconds":{"type":"number","exclusiveMinimum":0},
            "coastPenalty":{"type":"number","minimum":0},
            "newTrackPenalty":{"type":"number","minimum":0},
            "newTrackClicks":{"type":"integer","minimum":0},
            "longTrackExponent":{"type":"number","minimum":0},
            "lowIciExponent":{"type":"number","minimum":0},
            "electricalNoiseFilter":{
              "type":"object","additionalProperties":false,
              "properties":{
                "enabled":{"type":"boolean"},
                "minimumChi2":{"type":"number","minimum":0},
                "dataUnits":{"type":"integer","minimum":1}
              },
              "required":["enabled","minimumChi2","dataUnits"]
            },
            "variables":{
              "type":"object","additionalProperties":false,
              "properties":{
                "idi":{"$ref":"#/$defs/idiVariable"},
                "amplitude":{"$ref":"#/$defs/amplitudeVariable"},
                "bearing":{"$ref":"#/$defs/bearingVariable"},
                "correlation":{"$ref":"#/$defs/commonVariable"},
                "timeDelay":{"$ref":"#/$defs/commonVariable"},
                "length":{"$ref":"#/$defs/commonVariable"},
                "peakFrequency":{"$ref":"#/$defs/commonVariable"}
              },
              "required":["idi","amplitude","bearing","correlation","timeDelay","length","peakFrequency"]
            }
          },
          "required":["maximumIciSeconds","coastPenalty","newTrackPenalty","newTrackClicks","longTrackExponent","lowIciExponent","electricalNoiseFilter","variables"]
        },
        "classifier":{
          "type":"object","additionalProperties":false,
          "properties":{
            "runClassifier":{"type":"boolean"},
            "preClassifier":{"$ref":"#/$defs/preClassifier"},
            "idi":{"$ref":"#/$defs/idiClassifier"},
            "bearing":{"$ref":"#/$defs/bearingClassifier"},
            "spectrumTemplate":{"$ref":"#/$defs/templateClassifier"}
          },
          "required":["runClassifier","preClassifier","idi","bearing","spectrumTemplate"]
        },
        "localisation":{
          "type":"object","additionalProperties":false,
          "properties":{
            "enabled":{
              "const":false,
              "x-pamguard-port-status":"unsupported",
              "description":"Target-motion click-train localisation is not implemented"
            },
            "minimumDataUnits":{"type":"integer","minimum":0},
            "minimumAngleRangeRadians":{"type":"number","minimum":0}
          },
          "required":["enabled","minimumDataUnits","minimumAngleRangeRadians"]
        }
      },
      "required":["algorithm","channelGroups","dataSelector","kernel","chi2","classifier","localisation"],
      "$defs":{
        "commonVariable":{
          "type":"object","additionalProperties":false,
          "properties":{
            "enabled":{"type":"boolean"},
            "error":{"type":"number","exclusiveMinimum":0},
            "minimumError":{"type":"number","exclusiveMinimum":0}
          },
          "required":["enabled","error","minimumError"]
        },
        "idiVariable":{
          "type":"object","additionalProperties":false,
          "properties":{
            "enabled":{"type":"boolean"},
            "error":{"type":"number","exclusiveMinimum":0},
            "minimumError":{"type":"number","exclusiveMinimum":0},
            "minimumIdiSeconds":{"type":"number","minimum":0}
          },
          "required":["enabled","error","minimumError","minimumIdiSeconds"]
        },
        "amplitudeVariable":{
          "type":"object","additionalProperties":false,
          "properties":{
            "enabled":{"type":"boolean"},
            "error":{"type":"number","exclusiveMinimum":0},
            "minimumError":{"type":"number","exclusiveMinimum":0},
            "jumpEnabled":{"type":"boolean"},
            "maximumJumpDb":{"type":"number","minimum":0}
          },
          "required":["enabled","error","minimumError","jumpEnabled","maximumJumpDb"]
        },
        "bearingVariable":{
          "type":"object","additionalProperties":false,
          "properties":{
            "enabled":{"type":"boolean"},
            "errorRadians":{"type":"number","exclusiveMinimum":0},
            "minimumErrorRadians":{"type":"number","exclusiveMinimum":0},
            "jumpEnabled":{"type":"boolean"},
            "maximumJumpRadians":{"type":"number","minimum":0},
            "jumpDirection":{"enum":["both","positive","negative"]}
          },
          "required":["enabled","errorRadians","minimumErrorRadians","jumpEnabled","maximumJumpRadians","jumpDirection"]
        },
        "preClassifier":{
          "type":"object","additionalProperties":false,
          "properties":{
            "chi2Threshold":{"type":"number","minimum":0},
            "minimumClicks":{"type":"integer","minimum":0},
            "minimumSelectedPercentage":{"const":0},
            "minimumTimeSeconds":{"type":"number","minimum":0},
            "speciesFlag":{"type":"integer"}
          },
          "required":["chi2Threshold","minimumClicks","minimumSelectedPercentage","minimumTimeSeconds","speciesFlag"]
        },
        "idiClassifier":{
          "type":"object","additionalProperties":false,
          "properties":{
            "enabled":{"type":"boolean"},
            "useMedianIdi":{"type":"boolean"},
            "minimumMedianIdi":{"type":"number","minimum":0},
            "maximumMedianIdi":{"type":"number","minimum":0},
            "useMeanIdi":{"type":"boolean"},
            "minimumMeanIdi":{"type":"number","minimum":0},
            "maximumMeanIdi":{"type":"number","minimum":0},
            "useStdIdi":{"type":"boolean"},
            "minimumStdIdi":{"type":"number","minimum":0},
            "maximumStdIdi":{"type":"number","minimum":0},
            "speciesFlag":{"type":"integer"}
          },
          "required":["enabled","useMedianIdi","minimumMedianIdi","maximumMedianIdi","useMeanIdi","minimumMeanIdi","maximumMeanIdi","useStdIdi","minimumStdIdi","maximumStdIdi","speciesFlag"]
        },
        "bearingClassifier":{
          "type":"object","additionalProperties":false,
          "properties":{
            "enabled":{"type":"boolean"},
            "minimumBearingRadians":{"type":"number"},
            "maximumBearingRadians":{"type":"number"},
            "useMean":{"type":"boolean"},
            "minimumMeanDerivative":{"type":"number"},
            "maximumMeanDerivative":{"type":"number"},
            "useMedian":{"type":"boolean"},
            "minimumMedianDerivative":{"type":"number"},
            "maximumMedianDerivative":{"type":"number"},
            "useStd":{"type":"boolean"},
            "minimumStdDerivative":{"type":"number"},
            "maximumStdDerivative":{"type":"number"},
            "speciesFlag":{"type":"integer"}
          },
          "required":["enabled","minimumBearingRadians","maximumBearingRadians","useMean","minimumMeanDerivative","maximumMeanDerivative","useMedian","minimumMedianDerivative","maximumMedianDerivative","useStd","minimumStdDerivative","maximumStdDerivative","speciesFlag"]
        },
        "templateClassifier":{
          "type":"object","additionalProperties":false,
          "properties":{
            "enabled":{"type":"boolean"},
            "name":{"type":"string","minLength":1},
            "sampleRateHz":{"type":"number","exclusiveMinimum":0},
            "spectrum":{"type":"array","minItems":2,"items":{"type":"number"}},
            "correlationThreshold":{"type":"number","minimum":-1,"maximum":1},
            "speciesFlag":{"type":"integer"}
          },
          "required":["enabled","name","sampleRateHz","spectrum","correlationThreshold","speciesFlag"]
        }
      }
    })";
}

} // namespace pamguard::core
