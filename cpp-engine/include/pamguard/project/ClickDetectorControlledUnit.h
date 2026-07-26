#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "pamguard/core/AnalysisConfig.h"
#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

class ClickDetectorSettingsError final : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

/** Java ClickControl as one operator-visible controlled-unit bundle. */
[[nodiscard]] ControlledUnitDescriptor
make_click_detector_controlled_unit_descriptor();

/** Static Click display provider owned by each Click Detector instance. */
[[nodiscard]] DisplayProviderDescriptor
make_click_display_provider_descriptor();

/** Strictly validate the complete portable Click Detector settings object. */
void validate_click_detector_settings_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

/**
 * Project one canonical settings subtree into a hidden runtime adapter.
 *
 * The localiser projection consumes the global Array Manager geometry; array
 * coordinates are never duplicated in Click Detector project settings.
 */
[[nodiscard]] std::string click_detector_runtime_settings_json(
    std::string_view settings_json,
    std::uint32_t settings_version,
    std::string_view child_role,
    const core::ArrayConfiguration& array_geometry);

/** Strict validation for operational (not aesthetic) Click display settings. */
void validate_click_display_settings_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

} // namespace pamguard::project
