#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pamguard::core {

/**
 * Persisted ClipSettings.storageOption values that are executable in the
 * pinned Java ClipProcess.
 *
 * STORE_ANNOTATION (2) is deliberately absent: its dialog path is commented
 * out and ClipProcess does not construct a ClipDataUnit for that mode.
 */
enum class ClipGeneratorStorageMode : std::uint32_t {
    WavFiles = 0,
    Binary = 1,
    Both = 3,
};

/** Exact ClipGenSetting channel-selection values. */
enum class ClipGeneratorChannelSelection : std::uint32_t {
    DetectionChannelsOnly = 0,
    FirstDetectionChannelOnly = 1,
    AllChannels = 2,
};

/**
 * Stable project identity for one trigger-producing data-block role.
 *
 * Java keys ClipGenSetting rows by mutable PamDataBlock.dataName. The web
 * project model instead binds sources by producer instance and output role.
 */
struct ClipGeneratorTriggerSourceReference {
    std::string unit_id;
    std::string output_role;

    bool operator==(
        const ClipGeneratorTriggerSourceReference&) const = default;
};

/**
 * Receiver-owned policy for one source advertising the clip-trigger
 * capability.
 *
 * Field defaults reproduce new ClipGenSetting(dataName), apart from
 * trigger_source, which replaces Java's mutable dataName with a stable graph
 * reference. Policies belong to the Clip Generator receiver, not to the
 * producing detector.
 */
struct ClipGeneratorTriggerPolicySettings {
    ClipGeneratorTriggerSourceReference trigger_source;
    bool enabled = true;
    double seconds_before_trigger = 0.0;
    double seconds_after_trigger = 0.0;
    ClipGeneratorChannelSelection channel_selection =
        ClipGeneratorChannelSelection::DetectionChannelsOnly;
    std::optional<std::string> clip_prefix;
    bool use_data_budget = true;
    /** Exact Java ClipGenSetting.dataBudget unit: kibibytes. */
    int data_budget_kilobytes = 10 * 1024;
    double budget_period_hours = 24.0;

    bool operator==(
        const ClipGeneratorTriggerPolicySettings&) const = default;
};

/**
 * Portable operational fields from clipgenerator.ClipSettings.
 *
 * ClipSettings.dataSourceName is represented exclusively by the controlled
 * unit's rawAudio graph binding. Output paths are deployment-owned, and
 * Java-only display/migration fields are outside this scientific contract.
 * A fresh Java ClipSettings has no trigger policies; ClipControl adds one
 * policy only when an eligible source exists.
 */
struct ClipGeneratorSettings {
    ClipGeneratorStorageMode storage_mode =
        ClipGeneratorStorageMode::Binary;
    bool dated_subfolders = true;
    std::vector<ClipGeneratorTriggerPolicySettings> trigger_policies;

    bool operator==(const ClipGeneratorSettings&) const = default;
};

class ClipGeneratorSettingsError final
    : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

[[nodiscard]] ClipGeneratorSettings
clip_generator_settings_from_json(
    std::string_view settings_json,
    std::uint32_t settings_version);

[[nodiscard]] std::string clip_generator_settings_to_json(
    const ClipGeneratorSettings& settings,
    std::uint32_t settings_version = 1);

[[nodiscard]] std::string
clip_generator_default_settings_json();

[[nodiscard]] std::string_view
clip_generator_settings_schema_json() noexcept;

} // namespace pamguard::core
