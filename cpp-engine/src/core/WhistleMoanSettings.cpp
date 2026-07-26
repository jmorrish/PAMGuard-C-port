#include "pamguard/core/WhistleMoanSettings.h"

#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include <json.hpp>

namespace pamguard::core {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumChannels = 32;
constexpr std::size_t kMaximumShapeLimit =
    std::numeric_limits<std::uint32_t>::max();

Json parse_json(
    std::string_view settings_json,
    std::string_view context) {
    try {
        return Json::parse(settings_json);
    }
    catch (const std::exception& error) {
        throw WhistleMoanSettingsError(
            std::string(context) +
            " is not valid JSON: " + error.what());
    }
}

void require_version(std::uint32_t settings_version) {
    if (settings_version != 1) {
        throw WhistleMoanSettingsError(
            "Unsupported Whistle and Moan settings version");
    }
}

void require_exact_fields(
    const Json& value,
    const std::set<std::string>& expected,
    std::string_view context) {
    if (!value.is_object() || value.size() != expected.size()) {
        throw WhistleMoanSettingsError(
            std::string(context) +
            " must contain exactly the supported fields");
    }
    for (const auto& [name, _] : value.items()) {
        if (!expected.contains(name)) {
            throw WhistleMoanSettingsError(
                std::string(context) +
                " contains unknown field '" + name + "'");
        }
    }
}

double finite_number(
    const Json& value,
    std::string_view context) {
    if (!value.is_number()) {
        throw WhistleMoanSettingsError(
            std::string(context) + " must be a number");
    }
    const auto result = value.get<double>();
    if (!std::isfinite(result)) {
        throw WhistleMoanSettingsError(
            std::string(context) + " must be finite");
    }
    return result;
}

std::uint32_t unsigned_32(
    const Json& value,
    std::string_view context) {
    if (!value.is_number_integer() &&
        !value.is_number_unsigned()) {
        throw WhistleMoanSettingsError(
            std::string(context) + " must be an integer");
    }
    try {
        const auto result = value.get<std::uint64_t>();
        if (result >
            std::numeric_limits<std::uint32_t>::max()) {
            throw WhistleMoanSettingsError(
                std::string(context) +
                " must fit PAMGuard's 32-channel range");
        }
        return static_cast<std::uint32_t>(result);
    }
    catch (const WhistleMoanSettingsError&) {
        throw;
    }
    catch (const std::exception&) {
        throw WhistleMoanSettingsError(
            std::string(context) +
            " must be a non-negative 32-bit integer");
    }
}

std::size_t bounded_size(
    const Json& value,
    std::size_t minimum,
    std::size_t maximum,
    std::string_view context) {
    if (!value.is_number_integer() &&
        !value.is_number_unsigned()) {
        throw WhistleMoanSettingsError(
            std::string(context) + " must be an integer");
    }
    try {
        const auto result = value.get<std::uint64_t>();
        if (result < minimum || result > maximum) {
            throw WhistleMoanSettingsError(
                std::string(context) + " must be between " +
                std::to_string(minimum) + " and " +
                std::to_string(maximum));
        }
        return static_cast<std::size_t>(result);
    }
    catch (const WhistleMoanSettingsError&) {
        throw;
    }
    catch (const std::exception&) {
        throw WhistleMoanSettingsError(
            std::string(context) +
            " must be a non-negative integer");
    }
}

int bounded_integer(
    const Json& value,
    int minimum,
    int maximum,
    std::string_view context) {
    if (!value.is_number_integer()) {
        throw WhistleMoanSettingsError(
            std::string(context) + " must be an integer");
    }
    try {
        const auto result = value.get<std::int64_t>();
        if (result < minimum || result > maximum) {
            throw WhistleMoanSettingsError(
                std::string(context) + " must be between " +
                std::to_string(minimum) + " and " +
                std::to_string(maximum));
        }
        return static_cast<int>(result);
    }
    catch (const WhistleMoanSettingsError&) {
        throw;
    }
    catch (const std::exception&) {
        throw WhistleMoanSettingsError(
            std::string(context) + " must be between " +
            std::to_string(minimum) + " and " +
            std::to_string(maximum));
    }
}

bool boolean_value(
    const Json& value,
    std::string_view context) {
    if (!value.is_boolean()) {
        throw WhistleMoanSettingsError(
            std::string(context) + " must be a boolean");
    }
    return value.get<bool>();
}

WhistleSourceGrouping grouping_type(const Json& value) {
    if (!value.is_string()) {
        throw WhistleMoanSettingsError(
            "Whistle groupingType must be a string");
    }
    const auto name = value.get<std::string>();
    if (name == "singles") {
        return WhistleSourceGrouping::Singles;
    }
    if (name == "all") {
        return WhistleSourceGrouping::All;
    }
    if (name == "user") {
        return WhistleSourceGrouping::User;
    }
    throw WhistleMoanSettingsError(
        "Whistle groupingType must be singles, all, or user");
}

std::string_view grouping_name(WhistleSourceGrouping value) {
    switch (value) {
    case WhistleSourceGrouping::Singles:
        return "singles";
    case WhistleSourceGrouping::All:
        return "all";
    case WhistleSourceGrouping::User:
        return "user";
    }
    throw WhistleMoanSettingsError(
        "Whistle groupingType cannot be serialized");
}

std::vector<int> channel_groups(const Json& value) {
    if (!value.is_array() ||
        value.size() > kMaximumChannels) {
        throw WhistleMoanSettingsError(
            "Whistle channelGroups must be an array with at most 32 entries");
    }
    std::vector<int> result;
    result.reserve(value.size());
    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        result.push_back(bounded_integer(
            value.at(index),
            0,
            31,
            "Whistle channelGroups[" +
                std::to_string(index) + "]"));
    }
    return result;
}

detectors::SpectrogramNoiseConfig noise_from_json(
    const Json& value) {
    require_exact_fields(
        value,
        {
            "medianFilter",
            "medianFilterLength",
            "averageSubtraction",
            "updateConstant",
            "kernelSmoothing",
            "threshold",
            "thresholdDb",
            "finalOutput",
        },
        "Whistle noiseReduction");

    detectors::SpectrogramNoiseConfig result;
    result.run_median_filter = boolean_value(
        value.at("medianFilter"),
        "Whistle noiseReduction.medianFilter");
    result.median_filter_length = bounded_integer(
        value.at("medianFilterLength"),
        3,
        std::numeric_limits<int>::max(),
        "Whistle noiseReduction.medianFilterLength");
    if ((result.median_filter_length % 2) == 0) {
        throw WhistleMoanSettingsError(
            "Whistle medianFilterLength must be odd");
    }
    result.run_average_subtraction = boolean_value(
        value.at("averageSubtraction"),
        "Whistle noiseReduction.averageSubtraction");
    result.average_update_constant = finite_number(
        value.at("updateConstant"),
        "Whistle noiseReduction.updateConstant");
    if (!(result.average_update_constant > 0.0) ||
        result.average_update_constant > 0.5) {
        throw WhistleMoanSettingsError(
            "Whistle updateConstant must be greater than 0 and at most 0.5");
    }
    result.run_kernel_smoothing = boolean_value(
        value.at("kernelSmoothing"),
        "Whistle noiseReduction.kernelSmoothing");
    result.run_threshold = boolean_value(
        value.at("threshold"),
        "Whistle noiseReduction.threshold");
    result.threshold_db = finite_number(
        value.at("thresholdDb"),
        "Whistle noiseReduction.thresholdDb");
    if (!(result.threshold_db > 0.0)) {
        throw WhistleMoanSettingsError(
            "Whistle thresholdDb must be positive");
    }
    result.threshold_final_output = bounded_integer(
        value.at("finalOutput"),
        detectors::SpectrogramNoiseConfig::kOutputBinary,
        detectors::SpectrogramNoiseConfig::kOutputRaw,
        "Whistle noiseReduction.finalOutput");
    return result;
}

Json noise_to_json(
    const detectors::SpectrogramNoiseConfig& noise) {
    Json result{
        {"medianFilter", noise.run_median_filter},
        {"medianFilterLength",
         noise.median_filter_length},
        {"averageSubtraction",
         noise.run_average_subtraction},
        {"updateConstant",
         noise.average_update_constant},
        {"kernelSmoothing",
         noise.run_kernel_smoothing},
        {"threshold", noise.run_threshold},
        {"thresholdDb", noise.threshold_db},
        {"finalOutput",
         noise.threshold_final_output},
    };
    (void) noise_from_json(result);
    return result;
}

void validate_group_assignments(
    const WhistleMoanSettings& settings) {
    if (settings.channel_groups.size() >
        kMaximumChannels) {
        throw WhistleMoanSettingsError(
            "Whistle channelGroups cannot contain more than 32 entries");
    }
    for (const auto group : settings.channel_groups) {
        if (group < 0 || group >= 32) {
            throw WhistleMoanSettingsError(
                "Whistle channelGroups values must be between 0 and 31");
        }
    }
    if (settings.grouping_type !=
        WhistleSourceGrouping::User) {
        return;
    }
    for (std::size_t channel = 0;
         channel < kMaximumChannels;
         ++channel) {
        if ((settings.channel_bitmap &
             (std::uint32_t{1} << channel)) != 0 &&
            channel >= settings.channel_groups.size()) {
            throw WhistleMoanSettingsError(
                "User grouping requires an assignment for every selected channel");
        }
    }
}

WhistleMoanSettings settings_from_value(
    const Json& value,
    bool include_noise) {
    auto expected = std::set<std::string>{
        "channelBitmap",
        "groupingType",
        "channelGroups",
        "minFrequencyHz",
        "maxFrequencyHz",
        "connectType",
        "minLength",
        "minPixels",
        "keepShapeStubs",
        "fragmentationMethod",
        "maxCrossLength",
    };
    if (include_noise) {
        expected.insert("noiseReduction");
    }
    require_exact_fields(
        value,
        expected,
        include_noise
            ? "Whistle and Moan settings"
            : "Whistle contour runtime settings");

    WhistleMoanSettings result;
    result.channel_bitmap = unsigned_32(
        value.at("channelBitmap"),
        "Whistle channelBitmap");
    result.grouping_type =
        grouping_type(value.at("groupingType"));
    result.channel_groups =
        channel_groups(value.at("channelGroups"));
    result.min_frequency_hz = finite_number(
        value.at("minFrequencyHz"),
        "Whistle minFrequencyHz");
    result.max_frequency_hz = finite_number(
        value.at("maxFrequencyHz"),
        "Whistle maxFrequencyHz");
    if (result.min_frequency_hz < 0.0 ||
        result.max_frequency_hz < 0.0 ||
        (result.max_frequency_hz > 0.0 &&
         result.min_frequency_hz >
             result.max_frequency_hz)) {
        throw WhistleMoanSettingsError(
            "Whistle frequencies must be non-negative and ordered when maxFrequencyHz is non-zero");
    }
    result.connect_type = bounded_integer(
        value.at("connectType"),
        4,
        8,
        "Whistle connectType");
    if (result.connect_type != 4 &&
        result.connect_type != 8) {
        throw WhistleMoanSettingsError(
            "Whistle connectType must be 4 or 8");
    }
    result.min_length = bounded_size(
        value.at("minLength"),
        1,
        kMaximumShapeLimit,
        "Whistle minLength");
    result.min_pixels = bounded_size(
        value.at("minPixels"),
        1,
        kMaximumShapeLimit,
        "Whistle minPixels");
    result.keep_shape_stubs = boolean_value(
        value.at("keepShapeStubs"),
        "Whistle keepShapeStubs");
    result.fragmentation_method = bounded_integer(
        value.at("fragmentationMethod"),
        0,
        3,
        "Whistle fragmentationMethod");
    result.max_cross_length = bounded_size(
        value.at("maxCrossLength"),
        1,
        kMaximumShapeLimit,
        "Whistle maxCrossLength");
    if (include_noise) {
        result.noise_reduction =
            noise_from_json(value.at("noiseReduction"));
    }
    validate_group_assignments(result);
    return result;
}

Json contour_to_json(
    const WhistleMoanSettings& settings) {
    validate_group_assignments(settings);
    Json result{
        {"channelBitmap", settings.channel_bitmap},
        {"groupingType",
         grouping_name(settings.grouping_type)},
        {"channelGroups", settings.channel_groups},
        {"minFrequencyHz",
         settings.min_frequency_hz},
        {"maxFrequencyHz",
         settings.max_frequency_hz},
        {"connectType", settings.connect_type},
        {"minLength", settings.min_length},
        {"minPixels", settings.min_pixels},
        {"keepShapeStubs",
         settings.keep_shape_stubs},
        {"fragmentationMethod",
         settings.fragmentation_method},
        {"maxCrossLength",
         settings.max_cross_length},
    };
    (void) settings_from_value(result, false);
    return result;
}

} // namespace

WhistleMoanSettings whistle_moan_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(settings_version);
    return settings_from_value(
        parse_json(
            settings_json,
            "Whistle and Moan settings"),
        true);
}

std::string whistle_moan_settings_to_json(
    const WhistleMoanSettings& settings,
    std::uint32_t settings_version) {
    require_version(settings_version);
    auto result = contour_to_json(settings);
    result["noiseReduction"] =
        noise_to_json(settings.noise_reduction);
    (void) settings_from_value(result, true);
    return result.dump();
}

std::string whistle_moan_default_settings_json() {
    return whistle_moan_settings_to_json(
        WhistleMoanSettings{},
        1);
}

std::string whistle_moan_noise_runtime_settings_json(
    const WhistleMoanSettings& settings) {
    return noise_to_json(settings.noise_reduction).dump();
}

std::string whistle_moan_contour_runtime_settings_json(
    const WhistleMoanSettings& settings) {
    return contour_to_json(settings).dump();
}

WhistleMoanSettings
whistle_moan_contour_runtime_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(settings_version);
    return settings_from_value(
        parse_json(
            settings_json,
            "Whistle contour runtime settings"),
        false);
}

std::string
whistle_moan_contour_runtime_default_settings_json() {
    return whistle_moan_contour_runtime_settings_json(
        WhistleMoanSettings{});
}

bool whistle_moan_local_noise_ready(
    const WhistleMoanSettings& settings) noexcept {
    return settings.noise_reduction.run_median_filter &&
        settings.noise_reduction.run_average_subtraction &&
        settings.noise_reduction.run_threshold;
}

std::string_view
whistle_moan_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClass":"whistlesAndMoans.WhistleToneParameters",
            "groupedSourceClass":"PamView.GroupedSourceParameters",
            "noiseSettingsClass":"spectrogramNoiseReduction.SpectrogramNoiseSettings",
            "dialogClass":"whistlesAndMoans.WhistleToneDialog",
            "noiseProcessClass":"spectrogramNoiseReduction.SpectrogramNoiseProcess",
            "contourProcessClass":"whistlesAndMoans.WhistleToneConnectProcess"
        },
        "x-pamguard-portable-deviations":[
            "GroupedSourceParameters.dataSource is represented by the public fft binding",
            "Java null channelGroups is normalized to a portable empty array",
            "the standard FFT path requires local median, average-subtraction, and threshold methods because upstream process annotations and Cepstrum sources are not yet modeled",
            "display preferences, recorder triggers, SQL/binary/JSON storage, datagrams, alarms, and side-panel counters are not runtime settings",
            "background spectra, contour SPL/noise summaries, time-delay bearings, and grouped localisation output are not yet exposed",
            "beamformer sequence maps are not yet modeled; channelBitmap currently addresses physical FFT channel indices",
            "the contour runtime requires a positive integral FFT sample rate because its portable sample metadata is uint32",
            "portable graph stop finalization flushes pending contour regions whereas Java WhistleToneConnectProcess.pamStop does not",
            "finite values, ordered frequency limits, complete user groups, odd median length, and implementation-safe integer ranges are enforced at the portable boundary",
            "the Java shallow-clone and dialog mutation/cancel behavior is not reproduced"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "channelBitmap":{
                "type":"integer",
                "minimum":0,
                "maximum":4294967295
            },
            "groupingType":{
                "type":"string",
                "enum":["singles","all","user"]
            },
            "channelGroups":{
                "type":"array",
                "maxItems":32,
                "items":{
                    "type":"integer",
                    "minimum":0,
                    "maximum":31
                }
            },
            "minFrequencyHz":{"type":"number","minimum":0},
            "maxFrequencyHz":{"type":"number","minimum":0},
            "connectType":{"type":"integer","enum":[4,8]},
            "minLength":{
                "type":"integer",
                "minimum":1,
                "maximum":4294967295
            },
            "minPixels":{
                "type":"integer",
                "minimum":1,
                "maximum":4294967295
            },
            "keepShapeStubs":{"type":"boolean"},
            "fragmentationMethod":{
                "type":"integer",
                "minimum":0,
                "maximum":3
            },
            "maxCrossLength":{
                "type":"integer",
                "minimum":1,
                "maximum":4294967295
            },
            "noiseReduction":{
                "type":"object",
                "additionalProperties":false,
                "properties":{
                    "medianFilter":{"type":"boolean"},
                    "medianFilterLength":{
                        "type":"integer",
                        "minimum":3,
                        "maximum":2147483647
                    },
                    "averageSubtraction":{"type":"boolean"},
                    "updateConstant":{
                        "type":"number",
                        "exclusiveMinimum":0,
                        "maximum":0.5
                    },
                    "kernelSmoothing":{"type":"boolean"},
                    "threshold":{"type":"boolean"},
                    "thresholdDb":{
                        "type":"number",
                        "exclusiveMinimum":0
                    },
                    "finalOutput":{
                        "type":"integer",
                        "minimum":0,
                        "maximum":2
                    }
                },
                "required":[
                    "medianFilter",
                    "medianFilterLength",
                    "averageSubtraction",
                    "updateConstant",
                    "kernelSmoothing",
                    "threshold",
                    "thresholdDb",
                    "finalOutput"
                ],
                "x-pamguardConstraints":[
                    {
                        "id":"median-filter-length-odd",
                        "kind":"odd-integer",
                        "pointer":"/medianFilterLength"
                    }
                ]
            }
        },
        "required":[
            "channelBitmap",
            "groupingType",
            "channelGroups",
            "minFrequencyHz",
            "maxFrequencyHz",
            "connectType",
            "minLength",
            "minPixels",
            "keepShapeStubs",
            "fragmentationMethod",
            "maxCrossLength",
            "noiseReduction"
        ],
        "x-pamguardConstraints":[
            {
                "id":"frequency-range-ordered-or-nyquist-sentinel",
                "kind":"ordered-or-zero-upper",
                "lowerPointer":"/minFrequencyHz",
                "upperPointer":"/maxFrequencyHz"
            },
            {
                "id":"user-group-assignments-cover-selected-channels",
                "kind":"bitmap-index-array-coverage",
                "bitmapPointer":"/channelBitmap",
                "arrayPointer":"/channelGroups",
                "whenPointer":"/groupingType",
                "whenValue":"user"
            }
        ]
    })";
}

std::string_view
whistle_moan_contour_runtime_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "channelBitmap":{
                "type":"integer",
                "minimum":0,
                "maximum":4294967295
            },
            "groupingType":{
                "type":"string",
                "enum":["singles","all","user"]
            },
            "channelGroups":{
                "type":"array",
                "maxItems":32,
                "items":{
                    "type":"integer",
                    "minimum":0,
                    "maximum":31
                }
            },
            "minFrequencyHz":{"type":"number","minimum":0},
            "maxFrequencyHz":{"type":"number","minimum":0},
            "connectType":{"type":"integer","enum":[4,8]},
            "minLength":{
                "type":"integer",
                "minimum":1,
                "maximum":4294967295
            },
            "minPixels":{
                "type":"integer",
                "minimum":1,
                "maximum":4294967295
            },
            "keepShapeStubs":{"type":"boolean"},
            "fragmentationMethod":{
                "type":"integer",
                "minimum":0,
                "maximum":3
            },
            "maxCrossLength":{
                "type":"integer",
                "minimum":1,
                "maximum":4294967295
            }
        },
        "required":[
            "channelBitmap",
            "groupingType",
            "channelGroups",
            "minFrequencyHz",
            "maxFrequencyHz",
            "connectType",
            "minLength",
            "minPixels",
            "keepShapeStubs",
            "fragmentationMethod",
            "maxCrossLength"
        ]
    })";
}

} // namespace pamguard::core
