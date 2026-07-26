#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "pamguard/core/AnalysisConfig.h"

namespace pamguard::project {

inline constexpr std::string_view kArrayManagerGlobalSettingsTypeId =
    "pamguard.array-manager";
inline constexpr std::uint32_t kArrayManagerSettingsVersion = 1;
inline constexpr std::string_view kArrayManagerSettingsAdapterId =
    "pamguard.array-manager-settings.v1";

class GlobalSettingsAdapterError : public std::invalid_argument {
public:
    explicit GlobalSettingsAdapterError(const std::string& message)
        : std::invalid_argument(message) {}
};

/**
 * Validate and project the complete Array Manager settings object.
 *
 * The returned ArrayConfiguration is the one typed geometry representation
 * consumed by localisation runtime projections. It deliberately contains no
 * Click Detector settings, so geometry has one project authority.
 */
[[nodiscard]] core::ArrayConfiguration
array_manager_settings_to_geometry(
    std::string_view settings_json,
    std::uint32_t settings_version);

} // namespace pamguard::project
