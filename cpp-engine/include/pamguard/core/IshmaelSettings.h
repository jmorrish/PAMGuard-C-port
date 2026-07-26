#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pamguard::core {

/**
 * Stable portable names for PamView.dialog.GroupedSourcePanel's
 * GROUP_SINGLES, GROUP_ALL, and GROUP_USER modes.
 */
enum class IshmaelSourceGrouping {
    Singles,
    All,
    User,
};

struct IshmaelGroupedSourceSettings {
    std::uint32_t channel_bitmap = 0;
    IshmaelSourceGrouping grouping_type =
        IshmaelSourceGrouping::All;
    /** Java null is normalized to an empty portable array. */
    std::vector<int> channel_groups;

    bool operator==(
        const IshmaelGroupedSourceSettings&) const = default;
};

struct IshmaelPeakSettings {
    double threshold = 1.0;
    double min_time_seconds = 0.0;
    double max_time_seconds = 99999.0;
    double refractory_time_seconds = 0.0;

    bool operator==(const IshmaelPeakSettings&) const = default;
};

struct IshmaelEnergySumSettings {
    IshmaelGroupedSourceSettings source;
    IshmaelPeakSettings peak;
    double f0_hz = 0.0;
    double f1_hz = 1000.0;
    double ratio_f0_hz = 1000.0;
    double ratio_f1_hz = 2000.0;
    bool use_ratio = false;
    bool adaptive_threshold = false;
    double long_filter = 0.0001;
    bool use_log = false;
    double spike_decay = 100.0;
    bool output_smoothing = false;
    double short_filter = 0.1;

    bool operator==(const IshmaelEnergySumSettings&) const = default;
};

struct IshmaelSgramCorrSettings {
    IshmaelGroupedSourceSettings source;
    IshmaelPeakSettings peak;
    std::vector<std::array<double, 4>> segments;
    double spread_hz = 100.0;
    bool use_log = false;

    bool operator==(const IshmaelSgramCorrSettings&) const = default;
};

struct IshmaelMatchFilterSettings {
    IshmaelGroupedSourceSettings source;
    IshmaelPeakSettings peak;
    /**
     * Java kernelFilenameList order: element zero is active and later
     * elements are recent choices. Portable projects retain basenames only.
     */
    std::vector<std::string> kernel_filename_list;
    /**
     * First-channel waveform read from the active Java audio file. Java
     * ignores the file sample rate, so no kernel rate is persisted here.
     */
    std::vector<double> kernel_samples;

    bool operator==(
        const IshmaelMatchFilterSettings&) const = default;
};

struct IshmaelFftGeometry {
    std::size_t fft_length = 0;
    std::size_t fft_hop = 0;

    bool operator==(const IshmaelFftGeometry&) const = default;
};

class IshmaelSettingsError final : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

[[nodiscard]] IshmaelEnergySumSettings
ishmael_energy_sum_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);
[[nodiscard]] IshmaelSgramCorrSettings
ishmael_sgram_corr_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);
[[nodiscard]] IshmaelMatchFilterSettings
ishmael_match_filter_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] std::string ishmael_energy_sum_settings_to_json(
    const IshmaelEnergySumSettings& settings,
    std::uint32_t settings_version);
[[nodiscard]] std::string ishmael_sgram_corr_settings_to_json(
    const IshmaelSgramCorrSettings& settings,
    std::uint32_t settings_version);
[[nodiscard]] std::string ishmael_match_filter_settings_to_json(
    const IshmaelMatchFilterSettings& settings,
    std::uint32_t settings_version);

[[nodiscard]] std::string
ishmael_energy_sum_default_settings_json();
[[nodiscard]] std::string
ishmael_sgram_corr_default_settings_json();
[[nodiscard]] std::string
ishmael_match_filter_default_settings_json();

[[nodiscard]] std::string_view
ishmael_energy_sum_settings_schema_json() noexcept;
[[nodiscard]] std::string_view
ishmael_sgram_corr_settings_schema_json() noexcept;
[[nodiscard]] std::string_view
ishmael_match_filter_settings_schema_json() noexcept;

/**
 * Compute the channels on which Java's shared IshPeakProcess runs. Group IDs
 * are normalized before their lowest selected channels are chosen, avoiding
 * IshDetControl.getActiveChannels' sparse-group-index bug.
 */
[[nodiscard]] std::uint32_t ishmael_active_channel_bitmap(
    const IshmaelGroupedSourceSettings& source) noexcept;

/**
 * MatchFiltProcess2's no-group fallback is channel zero. Configured groups use
 * the same first-channel semantics as the shared peak process.
 */
[[nodiscard]] std::vector<std::size_t>
ishmael_match_filter_channels(
    const IshmaelGroupedSourceSettings& source);

[[nodiscard]] bool ishmael_energy_sum_ready(
    const IshmaelEnergySumSettings& settings) noexcept;
[[nodiscard]] bool ishmael_sgram_corr_ready(
    const IshmaelSgramCorrSettings& settings) noexcept;
[[nodiscard]] bool ishmael_match_filter_ready(
    const IshmaelMatchFilterSettings& settings) noexcept;

/** Pure controlled-unit to low-level-runtime settings adapters. */
[[nodiscard]] std::string
ishmael_energy_sum_runtime_settings_json(
    const IshmaelEnergySumSettings& settings,
    std::optional<IshmaelFftGeometry> source_geometry = std::nullopt);
[[nodiscard]] std::string
ishmael_sgram_corr_runtime_settings_json(
    const IshmaelSgramCorrSettings& settings,
    std::optional<IshmaelFftGeometry> source_geometry = std::nullopt);
[[nodiscard]] std::string
ishmael_match_filter_runtime_settings_json(
    const IshmaelMatchFilterSettings& settings);

[[nodiscard]] std::string
ishmael_energy_sum_runtime_default_settings_json();
[[nodiscard]] std::string
ishmael_sgram_corr_runtime_default_settings_json();
[[nodiscard]] std::string
ishmael_match_filter_runtime_default_settings_json();

[[nodiscard]] std::string_view
ishmael_energy_sum_runtime_schema_json() noexcept;
[[nodiscard]] std::string_view
ishmael_sgram_corr_runtime_schema_json() noexcept;
[[nodiscard]] std::string_view
ishmael_match_filter_runtime_schema_json() noexcept;

} // namespace pamguard::core
