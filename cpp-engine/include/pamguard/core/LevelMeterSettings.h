#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace pamguard::core {

/**
 * Stored values from levelMeter.LevelMeterParams.
 *
 * Keep the integer values because LevelMeterDialog persists the selected
 * JComboBox index directly.
 */
enum class LevelMeterScaleReference : std::uint32_t {
    FullScale = 0,
    Volts = 1,
    Micropascal = 2,
};

/** Stored radio-button values from levelMeter.LevelMeterParams. */
enum class LevelMeterScaleType : std::uint32_t {
    Peak = 0,
    Rms = 1,
};

/**
 * Portable Level Meter settings.
 *
 * Java's dataName is represented by the controlled unit's rawAudio binding,
 * so it must not be duplicated here.
 */
struct LevelMeterSettings {
    int min_level_db = -80;
    LevelMeterScaleReference scale_reference =
        LevelMeterScaleReference::FullScale;
    LevelMeterScaleType scale_type = LevelMeterScaleType::Peak;

    bool operator==(const LevelMeterSettings&) const = default;
};

class LevelMeterSettingsError final : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

[[nodiscard]] LevelMeterSettings level_meter_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] std::string level_meter_settings_to_json(
    const LevelMeterSettings& settings,
    std::uint32_t settings_version = 1);

[[nodiscard]] std::string level_meter_default_settings_json();

[[nodiscard]] std::string_view
level_meter_settings_schema_json() noexcept;

} // namespace pamguard::core
