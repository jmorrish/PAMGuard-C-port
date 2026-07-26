#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "pamguard/detectors/SpectrogramNoiseReducer.h"

namespace pamguard::core {

enum class WhistleSourceGrouping {
    Singles,
    All,
    User,
};

/**
 * Portable scientific settings for
 * whistlesAndMoans.WhistleToneParameters.
 *
 * GroupedSourceParameters.dataSource is represented by the public FFT
 * binding. Display-only fields, background storage, and localisation settings
 * are deliberately outside this detector-runtime contract.
 */
struct WhistleMoanSettings {
    std::uint32_t channel_bitmap = 0;
    WhistleSourceGrouping grouping_type =
        WhistleSourceGrouping::All;
    std::vector<int> channel_groups;
    double min_frequency_hz = 0.0;
    double max_frequency_hz = 0.0;
    int connect_type = 8;
    std::size_t min_length = 10;
    std::size_t min_pixels = 20;
    bool keep_shape_stubs = false;
    int fragmentation_method = 3;
    std::size_t max_cross_length = 5;
    detectors::SpectrogramNoiseConfig noise_reduction;
};

class WhistleMoanSettingsError final
    : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

[[nodiscard]] WhistleMoanSettings
whistle_moan_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] std::string whistle_moan_settings_to_json(
    const WhistleMoanSettings& settings,
    std::uint32_t settings_version);

[[nodiscard]] std::string
whistle_moan_default_settings_json();

[[nodiscard]] std::string_view
whistle_moan_settings_schema_json() noexcept;

/**
 * Pure recipe adapters for the two Java-owned processes.
 */
[[nodiscard]] std::string
whistle_moan_noise_runtime_settings_json(
    const WhistleMoanSettings& settings);

[[nodiscard]] std::string
whistle_moan_contour_runtime_settings_json(
    const WhistleMoanSettings& settings);

[[nodiscard]] WhistleMoanSettings
whistle_moan_contour_runtime_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] std::string
whistle_moan_contour_runtime_default_settings_json();

[[nodiscard]] std::string_view
whistle_moan_contour_runtime_schema_json() noexcept;

[[nodiscard]] bool whistle_moan_local_noise_ready(
    const WhistleMoanSettings& settings) noexcept;

} // namespace pamguard::core
