#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

struct SoundOutputPortableSettings {
    std::uint32_t channel_bitmap = 0;
    bool default_sample_rate = true;
    double playback_rate_hz = 48000.0;
    double playback_speed = 1.0;
    double playback_gain_db = 0.0;
    double hp_filter = 0.0;

    bool operator==(const SoundOutputPortableSettings&) const = default;
};

class SoundOutputSettingsError final : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

/**
 * Java-authoritative Sound Output controlled-unit descriptor.
 *
 * The selected raw-data block is represented by the public input binding.
 * Browser/host output-device identifiers are deliberately excluded from the
 * portable project settings.
 */
[[nodiscard]] ControlledUnitDescriptor
make_sound_output_controlled_unit_descriptor();

/**
 * Strictly decode the portable part of PlaybackParameters. Physical browser
 * or host output-device identities are not accepted here.
 */
[[nodiscard]] SoundOutputPortableSettings
sound_output_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

} // namespace pamguard::project
