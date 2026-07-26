#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "pamguard/dsp/IirFilter.h"

namespace pamguard::core {

/**
 * Portable operator settings for Filters.FilterParameters_2.
 *
 * Java's rawDataSource string is represented by the controlled unit's public
 * rawAudio binding. FilterParams is flattened because the low-level runtime
 * consumes exactly one filter parameter object.
 */
struct StandaloneFilterSettings {
    std::uint32_t channel_bitmap = 0;
    dsp::IirFilterParams filter;
};

/**
 * Portable operator settings for decimator.DecimatorParams.
 *
 * rawDataSource is represented by the public rawAudio binding. The output
 * sample rate is integral because AudioChunk sample-rate metadata is integral.
 */
struct DecimatorSettings {
    std::uint32_t output_sample_rate_hz = 2000;
    std::uint32_t channel_bitmap = 0;
    dsp::IirFilterParams filter;
    int interpolation = 0;
};

class FilterDecimatorSettingsError final
    : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

[[nodiscard]] dsp::IirFilterParams
standalone_filter_default_params();

[[nodiscard]] dsp::IirFilterParams
decimator_default_filter_params(
    std::uint32_t output_sample_rate_hz = 2000);

[[nodiscard]] StandaloneFilterSettings
standalone_filter_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] DecimatorSettings
decimator_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] std::string standalone_filter_settings_to_json(
    const StandaloneFilterSettings& settings,
    std::uint32_t settings_version);

[[nodiscard]] std::string decimator_settings_to_json(
    const DecimatorSettings& settings,
    std::uint32_t settings_version);

[[nodiscard]] std::string
standalone_filter_default_settings_json();

[[nodiscard]] std::string decimator_default_settings_json();

[[nodiscard]] std::string_view
standalone_filter_settings_schema_json() noexcept;

[[nodiscard]] std::string_view
decimator_settings_schema_json() noexcept;

} // namespace pamguard::core
