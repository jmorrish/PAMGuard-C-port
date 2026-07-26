#include "pamguard/project/SoundRecorderControlledUnit.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <json.hpp>

#include "pamguard/core/SoundRecorderSettings.h"

namespace pamguard::project {

namespace {

using Json = nlohmann::json;

bool contains_invalid_host_filename_character(
    std::string_view value) noexcept {
    return std::any_of(
        value.begin(),
        value.end(),
        [](char character) {
            const auto byte =
                static_cast<unsigned char>(character);
            if (byte < 0x20 || byte == 0x7F) {
                return true;
            }
            switch (character) {
            case '<':
            case '>':
            case ':':
            case '"':
            case '/':
            case '\\':
            case '|':
            case '?':
            case '*':
                return true;
            default:
                return false;
            }
        });
}

InstanceRulesDescriptor unlimited_in_all_modes() {
    return {
        0,
        std::nullopt,
        {
            RunMode::Normal,
            RunMode::Mixed,
            RunMode::Viewer,
        },
        {},
    };
}

PublicDataRoleDescriptor raw_audio_input() {
    return {
        "rawAudio",
        "Raw data source",
        DataRoleDirection::Input,
        "pamguard.raw-audio",
        RoleCardinality::ExactlyOne,
        {"sampled"},
        "PamDetection.RawDataUnit",
        "pamguard.acquisition",
    };
}

PublicDataRoleDescriptor recording_events_output() {
    return {
        "recordingEvents",
        "Recording events",
        DataRoleDirection::Output,
        "pamguard.recording-event",
        RoleCardinality::ExactlyOne,
        {
            "events",
            "recordings",
        },
        {},
        std::nullopt,
    };
}

std::vector<SettingDefaultDescriptor> default_evidence() {
    return {
        {
            "/operationMode",
            "autoStart,startStatus",
            R"("idle")",
            {},
            "fresh RecorderSettings has autoStart false and "
            "startStatus BUTTON_OFF",
            "SoundRecorder.RecorderSettings#autoStart,"
            "SoundRecorder.RecorderSettings#startStatus",
        },
        {
            "/channelBitmap",
            "channelBitmap",
            "3",
            {},
            {},
            "SoundRecorder.RecorderSettings#channelBitmap",
        },
        {
            "/bitDepth",
            "bitDepth",
            "16",
            {},
            {},
            "SoundRecorder.RecorderSettings#bitDepth",
        },
        {
            "/enableBuffer",
            "enableBuffer",
            "false",
            {},
            {},
            "SoundRecorder.RecorderSettings#enableBuffer",
        },
        {
            "/bufferLengthSeconds",
            "bufferLength",
            "30",
            {},
            {},
            "SoundRecorder.RecorderSettings#bufferLength",
        },
        {
            "/fileInitials",
            "fileInitials",
            R"("PAM")",
            {},
            {},
            "SoundRecorder.RecorderSettings#fileInitials",
        },
        {
            "/fileType",
            "fileType",
            R"("WAVE")",
            {},
            "serialized AudioFileFormat.Type name",
            "SoundRecorder.RecorderSettings#fileType",
        },
        {
            "/autoIntervalSeconds",
            "autoInterval",
            "300",
            {},
            {},
            "SoundRecorder.RecorderSettings#autoInterval",
        },
        {
            "/autoDurationSeconds",
            "autoDuration",
            "10",
            {},
            {},
            "SoundRecorder.RecorderSettings#autoDuration",
        },
        {
            "/limitLengthSeconds",
            "limitLengthSeconds",
            "true",
            {},
            {},
            "SoundRecorder.RecorderSettings#limitLengthSeconds",
        },
        {
            "/maxLengthSeconds",
            "maxLengthSeconds",
            "3600",
            {},
            {},
            "SoundRecorder.RecorderSettings#maxLengthSeconds",
        },
        {
            "/roundFileStarts",
            "notRoundFileStarts",
            "true",
            {},
            "portable positive view is the inverse of Java's stored field",
            "SoundRecorder.RecorderSettings#isRoundFileStarts",
        },
        {
            "/limitLengthMegaBytes",
            "limitLengthMegaBytes",
            "true",
            {},
            {},
            "SoundRecorder.RecorderSettings#limitLengthMegaBytes",
        },
        {
            "/maxLengthMegaBytes",
            "maxLengthMegaBytes",
            "640",
            {},
            {},
            "SoundRecorder.RecorderSettings#maxLengthMegaBytes",
        },
        {
            "/datedSubFolders",
            "datedSubFolders",
            "true",
            {},
            {},
            "SoundRecorder.RecorderSettings#datedSubFolders",
        },
        {
            "/triggerPolicies",
            "recorderTriggerDatas",
            "[]",
            {},
            "fresh RecorderSettings has no registered receiver-owned "
            "RecorderTrigger policies",
            "SoundRecorder.RecorderSettings#recorderTriggerDatas",
        },
    };
}

} // namespace

std::string sound_recorder_runtime_settings_json(
    std::string_view portable_settings_json,
    std::uint32_t settings_version,
    const SoundRecorderDeploymentBinding& deployment) {
    if (deployment.output_folder.empty()) {
        throw SoundRecorderRuntimeSettingsError(
            "Sound Recorder requires an explicit host output-folder "
            "deployment binding");
    }
    if (deployment.output_folder.find('\0') != std::string::npos) {
        throw SoundRecorderRuntimeSettingsError(
            "Sound Recorder output-folder binding contains a null byte");
    }

    try {
        const auto decoded =
            core::sound_recorder_settings_from_json(
                portable_settings_json,
                settings_version);
        if (contains_invalid_host_filename_character(
                decoded.file_initials)) {
            throw SoundRecorderRuntimeSettingsError(
                "Sound Recorder fileInitials contains a character "
                "that is not valid in a host output filename");
        }
        const auto canonical = Json::parse(
            core::sound_recorder_settings_to_json(
                decoded,
                settings_version));
        return Json{
            {"directory", deployment.output_folder},
            // Runtime start is always idle. The saved operationMode remains
            // intact for Java compatibility and future scheduler support.
            {"startTransport", "off"},
            {"settings", canonical},
        }.dump();
    }
    catch (const SoundRecorderRuntimeSettingsError&) {
        throw;
    }
    catch (const core::SoundRecorderSettingsError& error) {
        throw SoundRecorderRuntimeSettingsError(error.what());
    }
    catch (const Json::exception& error) {
        throw SoundRecorderRuntimeSettingsError(
            std::string(
                "Sound Recorder runtime settings could not be encoded: ") +
            error.what());
    }
}

ControlledUnitDescriptor
make_sound_recorder_controlled_unit_descriptor() {
    return {
        "pamguard.sound-recorder",
        1,
        {
            "Sound recorder",
            "Sound Processing",
            "SoundRecorder.RecorderControl",
            "direct",
            "Records audio data to wav of AIF files",
            "sound_processing/soundRecorderHelp/docs/RecorderOverview.html",
            {
                "src/PamModel/PamModel.java",
                "src/SoundRecorder/RecorderControl.java",
                "src/SoundRecorder/RecorderSettings.java",
                "src/SoundRecorder/RecorderSettingsDialog.java",
                "src/SoundRecorder/RecorderProcess.java",
                "src/SoundRecorder/RecorderStorage.java",
                "src/SoundRecorder/PamAudioFileStorage.java",
                "src/SoundRecorder/RecorderDataUnit.java",
                "src/SoundRecorder/RecorderTabPanel.java",
                "src/SoundRecorder/RecorderSidePanel.java",
                "src/SoundRecorder/trigger/RecorderTrigger.java",
                "src/SoundRecorder/trigger/RecorderTriggerData.java",
                "src/SoundRecorder/trigger/TriggerOptionsDialog.java",
            },
        },
        unlimited_in_all_modes(),
        {
            raw_audio_input(),
            recording_events_output(),
        },
        {
            1,
            {
                "SoundRecorder.RecorderSettings",
                "SoundRecorder.trigger.RecorderTriggerData",
            },
            {
                "src/SoundRecorder/RecorderControl.java",
                "src/SoundRecorder/RecorderSettings.java",
                "src/SoundRecorder/RecorderSettingsDialog.java",
                "src/SoundRecorder/RecorderProcess.java",
                "src/SoundRecorder/RecorderTabPanel.java",
                "src/SoundRecorder/trigger/RecorderTrigger.java",
                "src/SoundRecorder/trigger/RecorderTriggerData.java",
                "src/SoundRecorder/trigger/TriggerOptionsDialog.java",
                "src/SoundRecorder/trigger/TriggerDecisionMaker.java",
            },
            core::sound_recorder_default_settings_json(),
            {
                {
                    "settings.tabs",
                    {
                        "Control",
                        "Files and Folders",
                        "Triggered Recordings",
                    },
                },
                {
                    "settings.control.sections",
                    {
                        "Raw data source",
                        "Recorder controls",
                        "PAMGuard Startup Options",
                        "Audio buffer",
                        "Automatic recordings duty cycle settings",
                    },
                },
                {
                    "runtime.actions",
                    {
                        "Off",
                        "Continuous",
                    },
                },
                {
                    "settings.files-and-folders.sections",
                    {
                        "Output file location, names and format",
                        "Maximum file lengths",
                    },
                },
                {
                    "settings.triggered-recordings.sections",
                    {
                        "Triggered recordings",
                        "Persisted trigger bookkeeping",
                    },
                },
                {
                    "settings.triggered-recordings.actions",
                    {
                        "Reset used budget",
                    },
                },
            },
            default_evidence(),
            SettingsChangePolicy::StopRequired,
            "java-fixture-validated",
            std::string(
                core::sound_recorder_settings_schema_json()),
        },
        {
            1,
            {
                {
                    "recorder-process",
                    "pamguard.sound-recorder",
                    {
                        "",
                        std::string(
                            kSoundRecorderRuntimeSettingsAdapterId),
                    },
                    true,
                    AvailabilityStatus::Available,
                    "safe-idle-foundation",
                },
            },
            {
                {
                    "rawAudio",
                    {
                        "recorder-process",
                        "input",
                    },
                },
                {
                    "recordingEvents",
                    {
                        "recorder-process",
                        "recordings",
                    },
                },
            },
            {},
            {},
            "pamguard.sound-recorder.runtime",
        },
        AvailabilityStatus::Available,
        "experimental",
    };
}

} // namespace pamguard::project
