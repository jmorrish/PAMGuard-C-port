#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

inline constexpr std::string_view
    kSoundRecorderRuntimeSettingsAdapterId =
        "pamguard.sound-recorder-settings.v1";

/**
 * Host-owned deployment state required to run one Sound Recorder.
 *
 * This is deliberately not part of SoundRecorderSettings or the portable
 * project document. The service must supply the binding for the active host.
 */
struct SoundRecorderDeploymentBinding {
    std::string output_folder;

    bool operator==(
        const SoundRecorderDeploymentBinding&) const = default;
};

class SoundRecorderRuntimeSettingsError final
    : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

/**
 * Java-authoritative Sound Recorder controlled-unit descriptor.
 *
 * The selected RawDataUnit source is represented only by the public rawAudio
 * graph binding. No trigger role is exposed until detector-owned
 * RecorderTrigger capability wiring exists.
 */
[[nodiscard]] ControlledUnitDescriptor
make_sound_recorder_controlled_unit_descriptor();

/**
 * Project portable RecorderSettings plus an explicit host output-folder
 * binding into the low-level recorder settings object.
 *
 * An absent binding must be handled by project projection as
 * NeedsConfiguration with code "sound-recorder-output-folder-unbound".
 * Passing an empty folder here is therefore an error; this function never
 * invents a relative or process-working-directory fallback.
 */
[[nodiscard]] std::string
sound_recorder_runtime_settings_json(
    std::string_view portable_settings_json,
    std::uint32_t settings_version,
    const SoundRecorderDeploymentBinding& deployment);

} // namespace pamguard::project
