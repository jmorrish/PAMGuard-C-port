#include "pamguard/core/IshmaelSettings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include <json.hpp>

namespace pamguard::core {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumChannels = 32;
constexpr std::size_t kMaximumKernelFilenameHistory = 10;

Json parse_settings(
    std::string_view settings_json,
    std::string_view unit_name) {
    try {
        return Json::parse(settings_json);
    }
    catch (const std::exception& error) {
        throw IshmaelSettingsError(
            std::string(unit_name) +
            " settings are not valid JSON: " + error.what());
    }
}

void require_version(
    std::uint32_t settings_version,
    std::string_view unit_name) {
    if (settings_version != 1) {
        throw IshmaelSettingsError(
            "Unsupported " + std::string(unit_name) +
            " settings version");
    }
}

void require_exact_fields(
    const Json& value,
    const std::set<std::string>& expected,
    std::string_view context) {
    if (!value.is_object() || value.size() != expected.size()) {
        throw IshmaelSettingsError(
            std::string(context) +
            " must contain exactly the supported fields");
    }
    for (const auto& [name, _] : value.items()) {
        if (!expected.contains(name)) {
            throw IshmaelSettingsError(
                std::string(context) +
                " contains unknown field '" + name + "'");
        }
    }
}

std::uint32_t unsigned_32(
    const Json& value,
    std::string_view context) {
    if (!value.is_number_integer() &&
        !value.is_number_unsigned()) {
        throw IshmaelSettingsError(
            std::string(context) +
            " must be an unsigned 32-bit channel bitmap");
    }
    try {
        std::uint64_t wide = 0;
        if (value.is_number_unsigned()) {
            wide = value.get<std::uint64_t>();
        }
        else {
            const auto signed_wide =
                value.get<std::int64_t>();
            if (signed_wide < 0) {
                throw IshmaelSettingsError(
                    std::string(context) +
                    " must be non-negative");
            }
            wide = static_cast<std::uint64_t>(signed_wide);
        }
        if (wide >
            std::numeric_limits<std::uint32_t>::max()) {
            throw IshmaelSettingsError(
                std::string(context) +
                " exceeds PAMGuard's 32-channel bitmap");
        }
        return static_cast<std::uint32_t>(wide);
    }
    catch (const IshmaelSettingsError&) {
        throw;
    }
    catch (const std::exception&) {
        throw IshmaelSettingsError(
            std::string(context) +
            " must be an unsigned 32-bit channel bitmap");
    }
}

int bounded_integer(
    const Json& value,
    int minimum,
    int maximum,
    std::string_view context) {
    if (!value.is_number_integer() &&
        !value.is_number_unsigned()) {
        throw IshmaelSettingsError(
            std::string(context) + " must be an integer");
    }
    try {
        const auto wide = value.get<std::int64_t>();
        if (wide < minimum || wide > maximum) {
            throw IshmaelSettingsError(
                std::string(context) + " must be between " +
                std::to_string(minimum) + " and " +
                std::to_string(maximum));
        }
        return static_cast<int>(wide);
    }
    catch (const IshmaelSettingsError&) {
        throw;
    }
    catch (const std::exception&) {
        throw IshmaelSettingsError(
            std::string(context) + " must be between " +
            std::to_string(minimum) + " and " +
            std::to_string(maximum));
    }
}

double finite_number(
    const Json& value,
    std::string_view context) {
    if (!value.is_number()) {
        throw IshmaelSettingsError(
            std::string(context) + " must be a number");
    }
    const auto decoded = value.get<double>();
    if (!std::isfinite(decoded)) {
        throw IshmaelSettingsError(
            std::string(context) + " must be finite");
    }
    return decoded;
}

bool boolean_value(
    const Json& value,
    std::string_view context) {
    if (!value.is_boolean()) {
        throw IshmaelSettingsError(
            std::string(context) + " must be boolean");
    }
    return value.get<bool>();
}

IshmaelSourceGrouping grouping_from_json(
    const Json& value) {
    if (!value.is_string()) {
        throw IshmaelSettingsError(
            "Ishmael groupingType must be a string");
    }
    const auto token = value.get<std::string>();
    if (token == "singles") {
        return IshmaelSourceGrouping::Singles;
    }
    if (token == "all") {
        return IshmaelSourceGrouping::All;
    }
    if (token == "user") {
        return IshmaelSourceGrouping::User;
    }
    throw IshmaelSettingsError(
        "Ishmael groupingType must be singles, all, or user");
}

std::string_view grouping_to_json(
    IshmaelSourceGrouping grouping) {
    switch (grouping) {
    case IshmaelSourceGrouping::Singles:
        return "singles";
    case IshmaelSourceGrouping::All:
        return "all";
    case IshmaelSourceGrouping::User:
        return "user";
    }
    throw IshmaelSettingsError(
        "Ishmael groupingType cannot be serialized");
}

std::vector<int> channel_groups_from_json(
    const Json& value) {
    if (!value.is_array() ||
        value.size() > kMaximumChannels) {
        throw IshmaelSettingsError(
            "Ishmael channelGroups must be an array with at most 32 entries");
    }
    std::vector<int> groups;
    groups.reserve(value.size());
    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        groups.push_back(bounded_integer(
            value.at(index),
            0,
            31,
            "Ishmael channelGroups[" +
                std::to_string(index) + "]"));
    }
    return groups;
}

IshmaelGroupedSourceSettings source_from_json(
    const Json& settings) {
    IshmaelGroupedSourceSettings source;
    source.channel_bitmap = unsigned_32(
        settings.at("channelBitmap"),
        "Ishmael channelBitmap");
    source.grouping_type =
        grouping_from_json(settings.at("groupingType"));
    source.channel_groups =
        channel_groups_from_json(
            settings.at("channelGroups"));
    if (source.grouping_type ==
        IshmaelSourceGrouping::User) {
        for (std::size_t channel = 0;
             channel < kMaximumChannels;
             ++channel) {
            if ((source.channel_bitmap &
                 (std::uint32_t{1} << channel)) != 0 &&
                channel >= source.channel_groups.size()) {
                throw IshmaelSettingsError(
                    "Ishmael user grouping requires an assignment for every selected channel");
            }
        }
    }
    return source;
}

IshmaelPeakSettings peak_from_json(
    const Json& settings) {
    IshmaelPeakSettings peak;
    peak.threshold = finite_number(
        settings.at("threshold"),
        "Ishmael threshold");
    peak.min_time_seconds = finite_number(
        settings.at("minTimeSeconds"),
        "Ishmael minTimeSeconds");
    peak.max_time_seconds = finite_number(
        settings.at("maxTimeSeconds"),
        "Ishmael maxTimeSeconds");
    peak.refractory_time_seconds = finite_number(
        settings.at("refractoryTimeSeconds"),
        "Ishmael refractoryTimeSeconds");
    if (peak.threshold < 0.0 ||
        peak.min_time_seconds < 0.0 ||
        peak.max_time_seconds < 0.0 ||
        peak.refractory_time_seconds < 0.0) {
        throw IshmaelSettingsError(
            "Ishmael peak threshold and times must be non-negative");
    }
    if (peak.max_time_seconds != 0.0 &&
        peak.max_time_seconds < peak.min_time_seconds) {
        throw IshmaelSettingsError(
            "Ishmael maxTimeSeconds must be zero (disabled) or at least minTimeSeconds");
    }
    return peak;
}

void add_source_and_peak(
    Json& settings,
    const IshmaelGroupedSourceSettings& source,
    const IshmaelPeakSettings& peak) {
    settings["channelBitmap"] = source.channel_bitmap;
    settings["groupingType"] =
        grouping_to_json(source.grouping_type);
    settings["channelGroups"] = source.channel_groups;
    settings["threshold"] = peak.threshold;
    settings["minTimeSeconds"] =
        peak.min_time_seconds;
    settings["maxTimeSeconds"] =
        peak.max_time_seconds;
    settings["refractoryTimeSeconds"] =
        peak.refractory_time_seconds;
}

void validate_source_and_peak(
    const IshmaelGroupedSourceSettings& source,
    const IshmaelPeakSettings& peak) {
    Json value;
    add_source_and_peak(value, source, peak);
    value["segments"] = Json::array();
    value["spreadHz"] = 100.0;
    value["useLog"] = false;
    (void) value;
    if (source.channel_groups.size() > kMaximumChannels) {
        throw IshmaelSettingsError(
            "Ishmael channelGroups cannot exceed 32 entries");
    }
    for (const auto group : source.channel_groups) {
        if (group < 0 || group >= 32) {
            throw IshmaelSettingsError(
                "Ishmael channelGroups values must be between 0 and 31");
        }
    }
    if (source.grouping_type ==
        IshmaelSourceGrouping::User) {
        for (std::size_t channel = 0;
             channel < kMaximumChannels;
             ++channel) {
            if ((source.channel_bitmap &
                 (std::uint32_t{1} << channel)) != 0 &&
                channel >= source.channel_groups.size()) {
                throw IshmaelSettingsError(
                    "Ishmael user grouping requires an assignment for every selected channel");
            }
        }
    }
    if (!std::isfinite(peak.threshold) ||
        peak.threshold < 0.0 ||
        !std::isfinite(peak.min_time_seconds) ||
        peak.min_time_seconds < 0.0 ||
        !std::isfinite(peak.max_time_seconds) ||
        peak.max_time_seconds < 0.0 ||
        !std::isfinite(peak.refractory_time_seconds) ||
        peak.refractory_time_seconds < 0.0 ||
        (peak.max_time_seconds != 0.0 &&
         peak.max_time_seconds <
             peak.min_time_seconds)) {
        throw IshmaelSettingsError(
            "Ishmael peak settings cannot be serialized");
    }
}

Json energy_json(
    const IshmaelEnergySumSettings& settings) {
    validate_source_and_peak(
        settings.source,
        settings.peak);
    Json value;
    add_source_and_peak(
        value,
        settings.source,
        settings.peak);
    value["f0Hz"] = settings.f0_hz;
    value["f1Hz"] = settings.f1_hz;
    value["ratioF0Hz"] = settings.ratio_f0_hz;
    value["ratioF1Hz"] = settings.ratio_f1_hz;
    value["useRatio"] = settings.use_ratio;
    value["adaptiveThreshold"] =
        settings.adaptive_threshold;
    value["longFilter"] = settings.long_filter;
    value["useLog"] = settings.use_log;
    value["spikeDecay"] = settings.spike_decay;
    value["outputSmoothing"] =
        settings.output_smoothing;
    value["shortFilter"] = settings.short_filter;
    return value;
}

Json sgram_json(
    const IshmaelSgramCorrSettings& settings) {
    validate_source_and_peak(
        settings.source,
        settings.peak);
    Json segments = Json::array();
    for (const auto& segment : settings.segments) {
        segments.push_back({
            segment[0],
            segment[1],
            segment[2],
            segment[3],
        });
    }
    Json value;
    add_source_and_peak(
        value,
        settings.source,
        settings.peak);
    value["segments"] = std::move(segments);
    value["spreadHz"] = settings.spread_hz;
    value["useLog"] = settings.use_log;
    return value;
}

bool portable_basename(std::string_view name) {
    return !name.empty() &&
        name != "." &&
        name != ".." &&
        name.find('/') == std::string_view::npos &&
        name.find('\\') == std::string_view::npos &&
        name.find(':') == std::string_view::npos;
}

Json match_json(
    const IshmaelMatchFilterSettings& settings) {
    validate_source_and_peak(
        settings.source,
        settings.peak);
    Json value;
    add_source_and_peak(
        value,
        settings.source,
        settings.peak);
    value["kernelFilenameList"] =
        settings.kernel_filename_list;
    value["kernelSamples"] = settings.kernel_samples;
    return value;
}

std::set<std::string> common_fields() {
    return {
        "channelBitmap",
        "groupingType",
        "channelGroups",
        "threshold",
        "minTimeSeconds",
        "maxTimeSeconds",
        "refractoryTimeSeconds",
    };
}

} // namespace

IshmaelEnergySumSettings
ishmael_energy_sum_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(settings_version, "Ishmael Energy Sum");
    const auto value =
        parse_settings(settings_json, "Ishmael Energy Sum");
    auto expected = common_fields();
    expected.insert({
        "f0Hz",
        "f1Hz",
        "ratioF0Hz",
        "ratioF1Hz",
        "useRatio",
        "adaptiveThreshold",
        "longFilter",
        "useLog",
        "spikeDecay",
        "outputSmoothing",
        "shortFilter",
    });
    require_exact_fields(
        value,
        expected,
        "Ishmael Energy Sum settings");

    IshmaelEnergySumSettings settings;
    settings.source = source_from_json(value);
    settings.peak = peak_from_json(value);
    settings.f0_hz = finite_number(
        value.at("f0Hz"),
        "Ishmael Energy Sum f0Hz");
    settings.f1_hz = finite_number(
        value.at("f1Hz"),
        "Ishmael Energy Sum f1Hz");
    settings.ratio_f0_hz = finite_number(
        value.at("ratioF0Hz"),
        "Ishmael Energy Sum ratioF0Hz");
    settings.ratio_f1_hz = finite_number(
        value.at("ratioF1Hz"),
        "Ishmael Energy Sum ratioF1Hz");
    settings.use_ratio = boolean_value(
        value.at("useRatio"),
        "Ishmael Energy Sum useRatio");
    settings.adaptive_threshold = boolean_value(
        value.at("adaptiveThreshold"),
        "Ishmael Energy Sum adaptiveThreshold");
    settings.long_filter = finite_number(
        value.at("longFilter"),
        "Ishmael Energy Sum longFilter");
    settings.use_log = boolean_value(
        value.at("useLog"),
        "Ishmael Energy Sum useLog");
    settings.spike_decay = finite_number(
        value.at("spikeDecay"),
        "Ishmael Energy Sum spikeDecay");
    settings.output_smoothing = boolean_value(
        value.at("outputSmoothing"),
        "Ishmael Energy Sum outputSmoothing");
    settings.short_filter = finite_number(
        value.at("shortFilter"),
        "Ishmael Energy Sum shortFilter");
    if (settings.f0_hz < 0.0 ||
        settings.f1_hz < settings.f0_hz ||
        settings.ratio_f0_hz < 0.0 ||
        settings.ratio_f1_hz <
            settings.ratio_f0_hz) {
        throw IshmaelSettingsError(
            "Ishmael Energy Sum frequency bands must be non-negative and ordered");
    }
    if (settings.long_filter < 0.0 ||
        settings.short_filter < 0.0 ||
        settings.spike_decay < 1.0) {
        throw IshmaelSettingsError(
            "Ishmael Energy Sum filter constants must be non-negative and spikeDecay must be at least one");
    }
    if (settings.use_ratio &&
        settings.adaptive_threshold) {
        throw IshmaelSettingsError(
            "Ishmael Energy Sum energy ratio and adaptive threshold are mutually exclusive in the Java pane");
    }
    return settings;
}

IshmaelSgramCorrSettings
ishmael_sgram_corr_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(
        settings_version,
        "Ishmael Spectrogram Correlation");
    const auto value = parse_settings(
        settings_json,
        "Ishmael Spectrogram Correlation");
    auto expected = common_fields();
    expected.insert({
        "segments",
        "spreadHz",
        "useLog",
    });
    require_exact_fields(
        value,
        expected,
        "Ishmael Spectrogram Correlation settings");

    IshmaelSgramCorrSettings settings;
    settings.source = source_from_json(value);
    settings.peak = peak_from_json(value);
    if (!value.at("segments").is_array()) {
        throw IshmaelSettingsError(
            "Ishmael Spectrogram Correlation segments must be an array");
    }
    settings.segments.reserve(
        value.at("segments").size());
    for (std::size_t index = 0;
         index < value.at("segments").size();
         ++index) {
        const auto& encoded =
            value.at("segments").at(index);
        if (!encoded.is_array() ||
            encoded.size() != 4) {
            throw IshmaelSettingsError(
                "Ishmael Spectrogram Correlation segments[" +
                std::to_string(index) +
                "] must contain t0, f0, t1, and f1");
        }
        std::array<double, 4> segment{};
        for (std::size_t field = 0;
             field < segment.size();
             ++field) {
            segment[field] = finite_number(
                encoded.at(field),
                "Ishmael Spectrogram Correlation segment value");
        }
        if (segment[0] > segment[2] ||
            segment[0] < 0.0 ||
            segment[1] < 0.0 ||
            segment[3] < 0.0) {
            throw IshmaelSettingsError(
                "Ishmael Spectrogram Correlation segment times must be non-negative and ordered, and frequencies must be non-negative");
        }
        settings.segments.push_back(segment);
    }
    settings.spread_hz = finite_number(
        value.at("spreadHz"),
        "Ishmael Spectrogram Correlation spreadHz");
    if (!(settings.spread_hz > 0.0)) {
        throw IshmaelSettingsError(
            "Ishmael Spectrogram Correlation spreadHz must be positive");
    }
    settings.use_log = boolean_value(
        value.at("useLog"),
        "Ishmael Spectrogram Correlation useLog");
    return settings;
}

IshmaelMatchFilterSettings
ishmael_match_filter_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version) {
    require_version(
        settings_version,
        "Ishmael Matched Filter");
    const auto value = parse_settings(
        settings_json,
        "Ishmael Matched Filter");
    auto expected = common_fields();
    expected.insert({
        "kernelFilenameList",
        "kernelSamples",
    });
    require_exact_fields(
        value,
        expected,
        "Ishmael Matched Filter settings");

    IshmaelMatchFilterSettings settings;
    settings.source = source_from_json(value);
    settings.peak = peak_from_json(value);
    const auto& names =
        value.at("kernelFilenameList");
    if (!names.is_array() ||
        names.size() >
            kMaximumKernelFilenameHistory) {
        throw IshmaelSettingsError(
            "Ishmael Matched Filter kernelFilenameList must contain at most ten basenames");
    }
    std::set<std::string> unique_names;
    settings.kernel_filename_list.reserve(names.size());
    for (std::size_t index = 0;
         index < names.size();
         ++index) {
        if (!names.at(index).is_string()) {
            throw IshmaelSettingsError(
                "Ishmael Matched Filter kernelFilenameList entries must be strings");
        }
        auto name = names.at(index).get<std::string>();
        if (!portable_basename(name)) {
            throw IshmaelSettingsError(
                "Ishmael Matched Filter kernel filenames must be portable basenames");
        }
        if (!unique_names.emplace(name).second) {
            throw IshmaelSettingsError(
                "Ishmael Matched Filter kernel filename history cannot contain duplicates");
        }
        settings.kernel_filename_list.push_back(
            std::move(name));
    }
    const auto& samples = value.at("kernelSamples");
    if (!samples.is_array()) {
        throw IshmaelSettingsError(
            "Ishmael Matched Filter kernelSamples must be an array");
    }
    settings.kernel_samples.reserve(samples.size());
    for (std::size_t index = 0;
         index < samples.size();
         ++index) {
        settings.kernel_samples.push_back(
            finite_number(
                samples.at(index),
                "Ishmael Matched Filter kernelSamples[" +
                    std::to_string(index) + "]"));
    }
    return settings;
}

std::string ishmael_energy_sum_settings_to_json(
    const IshmaelEnergySumSettings& settings,
    std::uint32_t settings_version) {
    require_version(settings_version, "Ishmael Energy Sum");
    const auto encoded = energy_json(settings).dump();
    (void) ishmael_energy_sum_settings_from_json(
        encoded,
        settings_version);
    return encoded;
}

std::string ishmael_sgram_corr_settings_to_json(
    const IshmaelSgramCorrSettings& settings,
    std::uint32_t settings_version) {
    require_version(
        settings_version,
        "Ishmael Spectrogram Correlation");
    const auto encoded = sgram_json(settings).dump();
    (void) ishmael_sgram_corr_settings_from_json(
        encoded,
        settings_version);
    return encoded;
}

std::string ishmael_match_filter_settings_to_json(
    const IshmaelMatchFilterSettings& settings,
    std::uint32_t settings_version) {
    require_version(
        settings_version,
        "Ishmael Matched Filter");
    const auto encoded = match_json(settings).dump();
    (void) ishmael_match_filter_settings_from_json(
        encoded,
        settings_version);
    return encoded;
}

std::string ishmael_energy_sum_default_settings_json() {
    return energy_json(IshmaelEnergySumSettings{}).dump();
}

std::string ishmael_sgram_corr_default_settings_json() {
    return sgram_json(IshmaelSgramCorrSettings{}).dump();
}

std::string ishmael_match_filter_default_settings_json() {
    return match_json(IshmaelMatchFilterSettings{}).dump();
}

std::uint32_t ishmael_active_channel_bitmap(
    const IshmaelGroupedSourceSettings& source) noexcept {
    if (source.channel_bitmap == 0) {
        return 0;
    }
    if (source.grouping_type ==
        IshmaelSourceGrouping::Singles) {
        return source.channel_bitmap;
    }
    if (source.grouping_type ==
        IshmaelSourceGrouping::All) {
        for (std::size_t channel = 0;
             channel < kMaximumChannels;
             ++channel) {
            const auto bit =
                std::uint32_t{1} << channel;
            if ((source.channel_bitmap & bit) != 0) {
                return bit;
            }
        }
        return 0;
    }

    std::array<bool, kMaximumChannels> used_groups{};
    std::uint32_t active = 0;
    for (std::size_t channel = 0;
         channel < kMaximumChannels;
         ++channel) {
        const auto bit = std::uint32_t{1} << channel;
        if ((source.channel_bitmap & bit) == 0 ||
            channel >= source.channel_groups.size()) {
            continue;
        }
        const auto group =
            source.channel_groups[channel];
        if (group < 0 || group >= 32 ||
            used_groups[static_cast<std::size_t>(group)]) {
            continue;
        }
        used_groups[static_cast<std::size_t>(group)] = true;
        active |= bit;
    }
    return active;
}

std::vector<std::size_t> ishmael_match_filter_channels(
    const IshmaelGroupedSourceSettings& source) {
    auto active = ishmael_active_channel_bitmap(source);
    if (active == 0) {
        active = 1;
    }
    std::vector<std::size_t> channels;
    for (std::size_t channel = 0;
         channel < kMaximumChannels;
         ++channel) {
        if ((active &
             (std::uint32_t{1} << channel)) != 0) {
            channels.push_back(channel);
        }
    }
    return channels;
}

bool ishmael_energy_sum_ready(
    const IshmaelEnergySumSettings& settings) noexcept {
    return settings.source.channel_bitmap != 0;
}

bool ishmael_sgram_corr_ready(
    const IshmaelSgramCorrSettings& settings) noexcept {
    return settings.source.channel_bitmap != 0 &&
        !settings.segments.empty();
}

bool ishmael_match_filter_ready(
    const IshmaelMatchFilterSettings& settings) noexcept {
    return !settings.kernel_filename_list.empty() &&
        !settings.kernel_samples.empty();
}

std::string ishmael_energy_sum_runtime_settings_json(
    const IshmaelEnergySumSettings& settings,
    std::optional<IshmaelFftGeometry> source_geometry) {
    (void) ishmael_energy_sum_settings_to_json(settings, 1);
    Json runtime{
        {"channelBitmap", settings.source.channel_bitmap},
        {"activeChannelBitmap",
         ishmael_active_channel_bitmap(settings.source)},
        {"f0Hz", settings.f0_hz},
        {"f1Hz", settings.f1_hz},
        {"ratioF0Hz", settings.ratio_f0_hz},
        {"ratioF1Hz", settings.ratio_f1_hz},
        {"useRatio", settings.use_ratio},
        {"adaptiveThreshold",
         settings.adaptive_threshold},
        {"longFilter", settings.long_filter},
        {"useLog", settings.use_log},
        {"spikeDecay", settings.spike_decay},
        {"outputSmoothing",
         settings.output_smoothing},
        {"shortFilter", settings.short_filter},
        {"threshold", settings.peak.threshold},
        {"minTimeSeconds",
         settings.peak.min_time_seconds},
        {"maxTimeSeconds",
         settings.peak.max_time_seconds},
        {"refractoryTimeSeconds",
         settings.peak.refractory_time_seconds},
    };
    if (source_geometry) {
        runtime["fftLength"] =
            source_geometry->fft_length;
        runtime["fftHop"] = source_geometry->fft_hop;
    }
    return runtime.dump();
}

std::string ishmael_sgram_corr_runtime_settings_json(
    const IshmaelSgramCorrSettings& settings,
    std::optional<IshmaelFftGeometry> source_geometry) {
    (void) ishmael_sgram_corr_settings_to_json(settings, 1);
    Json segments = Json::array();
    for (const auto& segment : settings.segments) {
        segments.push_back({
            segment[0],
            segment[1],
            segment[2],
            segment[3],
        });
    }
    Json runtime{
        {"channelBitmap", settings.source.channel_bitmap},
        {"activeChannelBitmap",
         ishmael_active_channel_bitmap(settings.source)},
        {"segments", std::move(segments)},
        {"spreadHz", settings.spread_hz},
        {"useLog", settings.use_log},
        {"threshold", settings.peak.threshold},
        {"minTimeSeconds",
         settings.peak.min_time_seconds},
        {"maxTimeSeconds",
         settings.peak.max_time_seconds},
        {"refractoryTimeSeconds",
         settings.peak.refractory_time_seconds},
    };
    if (source_geometry) {
        runtime["fftLength"] =
            source_geometry->fft_length;
        runtime["fftHop"] = source_geometry->fft_hop;
    }
    return runtime.dump();
}

std::string ishmael_match_filter_runtime_settings_json(
    const IshmaelMatchFilterSettings& settings) {
    (void) ishmael_match_filter_settings_to_json(settings, 1);
    return Json{
        {"kernel", settings.kernel_samples},
        {"channels",
         ishmael_match_filter_channels(settings.source)},
        {"threshold", settings.peak.threshold},
        {"minTimeSeconds",
         settings.peak.min_time_seconds},
        {"maxTimeSeconds",
         settings.peak.max_time_seconds},
        {"refractoryTimeSeconds",
         settings.peak.refractory_time_seconds},
    }.dump();
}

std::string ishmael_energy_sum_runtime_default_settings_json() {
    return ishmael_energy_sum_runtime_settings_json(
        IshmaelEnergySumSettings{});
}

std::string ishmael_sgram_corr_runtime_default_settings_json() {
    return ishmael_sgram_corr_runtime_settings_json(
        IshmaelSgramCorrSettings{});
}

std::string ishmael_match_filter_runtime_default_settings_json() {
    return ishmael_match_filter_runtime_settings_json(
        IshmaelMatchFilterSettings{});
}

std::string_view
ishmael_energy_sum_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClasses":[
                "IshmaelDetector.EnergySumParams",
                "IshmaelDetector.IshDetParams",
                "PamView.GroupedSourceParameters"
            ],
            "dialogClasses":[
                "IshmaelDetector.layoutFX.IshPaneFX",
                "IshmaelDetector.layoutFX.EnergySumPane",
                "IshmaelDetector.layoutFX.PeakPickingPane"
            ],
            "processClasses":[
                "IshmaelDetector.EnergySumProcess",
                "IshmaelDetector.IshPeakProcess"
            ]
        },
        "x-pamguard-portable-deviations":[
            "GroupedSourceParameters.dataSource is represented by the public fft binding",
            "Java null channelGroups is normalized to an empty array",
            "groupingType uses stable names rather than Swing integer constants",
            "deprecated name/inputDataSource/channelList and display-only vscale are excluded",
            "dead IshDetParams.smoothing and EnergySumParams.dontUpgrade migration state are excluded",
            "finite non-negative dialog ranges, ordered frequency bands, and max-time ordering are enforced at the portable boundary",
            "the JavaFX pane's useRatio/adaptiveThreshold mutual exclusion is enforced at the portable boundary"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "channelBitmap":{"type":"integer","minimum":0,"maximum":4294967295},
            "groupingType":{"type":"string","enum":["singles","all","user"]},
            "channelGroups":{"type":"array","maxItems":32,"items":{"type":"integer","minimum":0,"maximum":31}},
            "threshold":{"type":"number","minimum":0},
            "minTimeSeconds":{"type":"number","minimum":0},
            "maxTimeSeconds":{"type":"number","minimum":0},
            "refractoryTimeSeconds":{"type":"number","minimum":0},
            "f0Hz":{"type":"number","minimum":0},
            "f1Hz":{"type":"number","minimum":0},
            "ratioF0Hz":{"type":"number","minimum":0},
            "ratioF1Hz":{"type":"number","minimum":0},
            "useRatio":{"type":"boolean"},
            "adaptiveThreshold":{"type":"boolean"},
            "longFilter":{"type":"number","minimum":0},
            "useLog":{"type":"boolean"},
            "spikeDecay":{"type":"number","minimum":1},
            "outputSmoothing":{"type":"boolean"},
            "shortFilter":{"type":"number","minimum":0}
        },
        "required":[
            "channelBitmap","groupingType","channelGroups",
            "threshold","minTimeSeconds","maxTimeSeconds","refractoryTimeSeconds",
            "f0Hz","f1Hz","ratioF0Hz","ratioF1Hz","useRatio",
            "adaptiveThreshold","longFilter","useLog","spikeDecay",
            "outputSmoothing","shortFilter"
        ],
        "x-pamguardConstraints":[
            {"id":"energy-band-order","kind":"less-than-or-equal","leftPointer":"/f0Hz","rightPointer":"/f1Hz"},
            {"id":"energy-ratio-band-order","kind":"less-than-or-equal","leftPointer":"/ratioF0Hz","rightPointer":"/ratioF1Hz"},
            {"id":"energy-ratio-adaptive-exclusive","kind":"not-both-true","leftPointer":"/useRatio","rightPointer":"/adaptiveThreshold"},
            {"id":"ishmael-max-time","kind":"zero-or-greater-than-or-equal","leftPointer":"/maxTimeSeconds","rightPointer":"/minTimeSeconds"},
            {"id":"ishmael-user-groups","kind":"selected-channel-assignments","bitmapPointer":"/channelBitmap","modePointer":"/groupingType","groupsPointer":"/channelGroups","mode":"user"}
        ]
    })";
}

std::string_view
ishmael_sgram_corr_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClasses":[
                "IshmaelDetector.SgramCorrParams",
                "IshmaelDetector.IshDetParams",
                "PamView.GroupedSourceParameters"
            ],
            "dialogClasses":[
                "IshmaelDetector.layoutFX.IshPaneFX",
                "IshmaelDetector.layoutFX.SpecCorrPane",
                "IshmaelDetector.layoutFX.PeakPickingPane"
            ],
            "processClasses":[
                "IshmaelDetector.SgramCorrProcess",
                "IshmaelDetector.IshPeakProcess"
            ]
        },
        "x-pamguard-known-authority-defect":"SpecCorrPane.getParams returns its backing params without calling applyParams; the portable settings implement the intended visible controls",
        "x-pamguard-portable-deviations":[
            "GroupedSourceParameters.dataSource is represented by the public fft binding",
            "Java null channelGroups is normalized to an empty array",
            "groupingType uses stable names rather than Swing integer constants",
            "deprecated compatibility and display-only fields are excluded",
            "empty constructor segments remain valid persisted settings but make the unit needs-configuration",
            "finite values, non-negative ordered segment times/frequencies, positive spread, and max-time ordering are enforced at the portable boundary"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "channelBitmap":{"type":"integer","minimum":0,"maximum":4294967295},
            "groupingType":{"type":"string","enum":["singles","all","user"]},
            "channelGroups":{"type":"array","maxItems":32,"items":{"type":"integer","minimum":0,"maximum":31}},
            "threshold":{"type":"number","minimum":0},
            "minTimeSeconds":{"type":"number","minimum":0},
            "maxTimeSeconds":{"type":"number","minimum":0},
            "refractoryTimeSeconds":{"type":"number","minimum":0},
            "segments":{
                "type":"array",
                "items":{
                    "type":"array",
                    "prefixItems":[
                        {"type":"number","minimum":0},
                        {"type":"number","minimum":0},
                        {"type":"number","minimum":0},
                        {"type":"number","minimum":0}
                    ],
                    "minItems":4,
                    "maxItems":4
                }
            },
            "spreadHz":{"type":"number","exclusiveMinimum":0},
            "useLog":{"type":"boolean"}
        },
        "required":[
            "channelBitmap","groupingType","channelGroups",
            "threshold","minTimeSeconds","maxTimeSeconds","refractoryTimeSeconds",
            "segments","spreadHz","useLog"
        ],
        "x-pamguardConstraints":[
            {"id":"sgram-segment-time-order","kind":"array-tuple-less-than-or-equal","arrayPointer":"/segments","leftIndex":0,"rightIndex":2},
            {"id":"ishmael-max-time","kind":"zero-or-greater-than-or-equal","leftPointer":"/maxTimeSeconds","rightPointer":"/minTimeSeconds"},
            {"id":"ishmael-user-groups","kind":"selected-channel-assignments","bitmapPointer":"/channelBitmap","modePointer":"/groupingType","groupsPointer":"/channelGroups","mode":"user"}
        ]
    })";
}

std::string_view
ishmael_match_filter_settings_schema_json() noexcept {
    return R"({
        "$schema":"https://json-schema.org/draft/2020-12/schema",
        "x-pamguard-authority":{
            "version":"2.02.18e",
            "commit":"dca55c81ef6f1498a8a3b926c69e7182afb915ee",
            "settingsClasses":[
                "IshmaelDetector.MatchFiltParams",
                "IshmaelDetector.IshDetParams",
                "PamView.GroupedSourceParameters"
            ],
            "dialogClass":"IshmaelDetector.MatchFiltParamsDialog",
            "processClasses":[
                "IshmaelDetector.MatchFiltProcess2",
                "IshmaelDetector.IshPeakProcess"
            ]
        },
        "x-pamguard-portable-deviations":[
            "GroupedSourceParameters.dataSource is represented by the public rawAudio binding",
            "Java null channelGroups is normalized to an empty array",
            "groupingType uses stable names rather than Swing integer constants",
            "deprecated compatibility and display-only fields are excluded",
            "host-specific absolute kernel paths are persisted as portable basenames",
            "the active kernel file's first-channel samples are embedded because a browser runtime cannot reopen the Java host path",
            "the Java kernel file sample rate is intentionally not persisted or used",
            "empty constructor filename history and samples remain valid persisted settings but make the unit needs-configuration",
            "finite samples and max-time ordering are enforced at the portable boundary"
        ],
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "channelBitmap":{"type":"integer","minimum":0,"maximum":4294967295},
            "groupingType":{"type":"string","enum":["singles","all","user"]},
            "channelGroups":{"type":"array","maxItems":32,"items":{"type":"integer","minimum":0,"maximum":31}},
            "threshold":{"type":"number","minimum":0},
            "minTimeSeconds":{"type":"number","minimum":0},
            "maxTimeSeconds":{"type":"number","minimum":0},
            "refractoryTimeSeconds":{"type":"number","minimum":0},
            "kernelFilenameList":{
                "type":"array",
                "maxItems":10,
                "uniqueItems":true,
                "items":{"type":"string","minLength":1,"pattern":"^[^/\\\\:]+$"}
            },
            "kernelSamples":{"type":"array","items":{"type":"number"}}
        },
        "required":[
            "channelBitmap","groupingType","channelGroups",
            "threshold","minTimeSeconds","maxTimeSeconds","refractoryTimeSeconds",
            "kernelFilenameList","kernelSamples"
        ],
        "x-pamguardConstraints":[
            {"id":"ishmael-max-time","kind":"zero-or-greater-than-or-equal","leftPointer":"/maxTimeSeconds","rightPointer":"/minTimeSeconds"},
            {"id":"ishmael-user-groups","kind":"selected-channel-assignments","bitmapPointer":"/channelBitmap","modePointer":"/groupingType","groupsPointer":"/channelGroups","mode":"user"}
        ]
    })";
}

std::string_view
ishmael_energy_sum_runtime_schema_json() noexcept {
    return R"({
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "channelBitmap":{"type":"integer","minimum":0,"maximum":4294967295},
            "activeChannelBitmap":{"type":"integer","minimum":0,"maximum":4294967295},
            "fftLength":{"type":"integer","minimum":2},
            "fftHop":{"type":"integer","minimum":1},
            "f0Hz":{"type":"number","minimum":0},
            "f1Hz":{"type":"number","minimum":0},
            "ratioF0Hz":{"type":"number","minimum":0},
            "ratioF1Hz":{"type":"number","minimum":0},
            "useRatio":{"type":"boolean"},
            "adaptiveThreshold":{"type":"boolean"},
            "longFilter":{"type":"number","minimum":0},
            "useLog":{"type":"boolean"},
            "spikeDecay":{"type":"number","minimum":1},
            "outputSmoothing":{"type":"boolean"},
            "shortFilter":{"type":"number","minimum":0},
            "threshold":{"type":"number","minimum":0},
            "minTimeSeconds":{"type":"number","minimum":0},
            "maxTimeSeconds":{"type":"number","minimum":0},
            "refractoryTimeSeconds":{"type":"number","minimum":0}
        },
        "required":[
            "channelBitmap","activeChannelBitmap",
            "f0Hz","f1Hz","ratioF0Hz","ratioF1Hz","useRatio",
            "adaptiveThreshold","longFilter","useLog","spikeDecay",
            "outputSmoothing","shortFilter","threshold",
            "minTimeSeconds","maxTimeSeconds","refractoryTimeSeconds"
        ]
    })";
}

std::string_view
ishmael_sgram_corr_runtime_schema_json() noexcept {
    return R"({
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "channelBitmap":{"type":"integer","minimum":0,"maximum":4294967295},
            "activeChannelBitmap":{"type":"integer","minimum":0,"maximum":4294967295},
            "fftLength":{"type":"integer","minimum":2},
            "fftHop":{"type":"integer","minimum":1},
            "segments":{
                "type":"array",
                "items":{
                    "type":"array",
                    "prefixItems":[
                        {"type":"number"},{"type":"number"},
                        {"type":"number"},{"type":"number"}
                    ],
                    "minItems":4,
                    "maxItems":4
                }
            },
            "spreadHz":{"type":"number","exclusiveMinimum":0},
            "useLog":{"type":"boolean"},
            "threshold":{"type":"number","minimum":0},
            "minTimeSeconds":{"type":"number","minimum":0},
            "maxTimeSeconds":{"type":"number","minimum":0},
            "refractoryTimeSeconds":{"type":"number","minimum":0}
        },
        "required":[
            "channelBitmap","activeChannelBitmap","segments",
            "spreadHz","useLog","threshold","minTimeSeconds",
            "maxTimeSeconds","refractoryTimeSeconds"
        ]
    })";
}

std::string_view
ishmael_match_filter_runtime_schema_json() noexcept {
    return R"({
        "type":"object",
        "additionalProperties":false,
        "properties":{
            "kernel":{"type":"array","items":{"type":"number"}},
            "channels":{"type":"array","items":{"type":"integer","minimum":0,"maximum":31},"uniqueItems":true},
            "threshold":{"type":"number","minimum":0},
            "minTimeSeconds":{"type":"number","minimum":0},
            "maxTimeSeconds":{"type":"number","minimum":0},
            "refractoryTimeSeconds":{"type":"number","minimum":0}
        },
        "required":[
            "kernel","channels","threshold","minTimeSeconds",
            "maxTimeSeconds","refractoryTimeSeconds"
        ]
    })";
}

} // namespace pamguard::core
