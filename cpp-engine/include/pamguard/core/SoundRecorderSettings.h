#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pamguard::core {

/**
 * Portable semantic view of RecorderSettings.autoStart/startStatus.
 *
 * Java persists two fields for compatibility. The operator dialog presents
 * one mutually exclusive choice, so the portable contract stores that choice
 * once. Fresh RecorderSettings maps to Idle.
 */
enum class SoundRecorderOperationMode {
    Idle,
    Continuous,
    Cycle,
    RestoreLast,
};

/**
 * All persisted, non-transient fields from
 * SoundRecorder.trigger.RecorderTriggerData.
 *
 * The three bookkeeping values are part of Java's persisted trigger policy:
 * used_day_budget_bytes affects future trigger decisions, and the timestamps
 * preserve the last trigger interval across settings saves.
 */
struct SoundRecorderTriggerPolicySettings {
    std::string trigger_name;
    bool enabled = false;
    double seconds_before_trigger = 0.0;
    double seconds_after_trigger = 10.0;
    int min_detection_count = 1;
    int count_seconds = 0;
    int min_gap_between_triggers_seconds = 0;
    int max_total_trigger_length_seconds = 0;
    int day_budget_megabytes = 0;
    std::int64_t last_trigger_start_unix_ms = 0;
    std::int64_t last_trigger_end_unix_ms = 0;
    std::uint64_t used_day_budget_bytes = 0;

    bool operator==(
        const SoundRecorderTriggerPolicySettings&) const = default;
};

/**
 * Portable operational fields from SoundRecorder.RecorderSettings.
 *
 * rawDataSource is represented by the future controlled unit's rawAudio graph
 * binding and must not be duplicated here. outputFolder is deliberately
 * host/deployment owned: Java derives its constructor default from
 * PamFolders.getDefaultProjectFolder(), so it cannot be a deterministic,
 * portable project default. RecorderTriggerData.decisionMaker is transient
 * runtime state and is also intentionally absent.
 */
struct SoundRecorderSettings {
    SoundRecorderOperationMode operation_mode =
        SoundRecorderOperationMode::Idle;
    std::uint32_t channel_bitmap = 3;
    /** Java RecorderSettings.bitDepth; recording is PCM_SIGNED. */
    int bit_depth = 16;
    bool enable_buffer = false;
    int buffer_length_seconds = 30;
    std::string file_initials = "PAM";
    /** Java's serialized AudioFileFormat.Type name. */
    std::string file_type = "WAVE";
    int auto_interval_seconds = 300;
    int auto_duration_seconds = 10;
    bool limit_length_seconds = true;
    int max_length_seconds = 3600;
    bool round_file_starts = true;
    bool limit_length_megabytes = true;
    std::uint64_t max_length_megabytes = 640;
    bool dated_subfolders = true;
    std::vector<SoundRecorderTriggerPolicySettings> trigger_policies;

    bool operator==(const SoundRecorderSettings&) const = default;
};

class SoundRecorderSettingsError final
    : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

[[nodiscard]] SoundRecorderSettings
sound_recorder_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] std::string sound_recorder_settings_to_json(
    const SoundRecorderSettings& settings,
    std::uint32_t settings_version = 1);

[[nodiscard]] std::string
sound_recorder_default_settings_json();

[[nodiscard]] std::string_view
sound_recorder_settings_schema_json() noexcept;

} // namespace pamguard::core
