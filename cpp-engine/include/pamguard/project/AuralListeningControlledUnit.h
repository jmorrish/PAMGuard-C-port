#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "pamguard/project/ControlledUnitRegistry.h"

namespace pamguard::project {

inline constexpr std::string_view
    kAuralListeningControlledUnitTypeId =
        "pamguard.aural-listening";
inline constexpr std::string_view
    kAuralListeningEffortDataType =
        "pamguard.listening-effort";
inline constexpr std::string_view
    kAuralListeningThingHeardDataType =
        "pamguard.thing-heard";
inline constexpr std::string_view
    kAuralListeningRuntimeSettingsAdapterId =
        "pamguard.aural-listening-settings.v1";

// Pinned listening.ListeningControl/ListeningProcess constants.
inline constexpr std::uint32_t
    kAuralListeningSpeciesMaximumUtf16CodeUnits = 50;
inline constexpr std::uint32_t
    kAuralListeningDatabaseCommentUtf16CodeUnits = 50;
inline constexpr std::uint64_t
    kAuralListeningNaturalLifetimeSeconds = 10'800;

/**
 * Operational, portable projection of Java ListeningParameters.
 *
 * Java stores each species as a SpeciesItem containing a name and an optional
 * PamSymbol. The symbol is a Java display preference (shape, size and colour),
 * so this web-port projection intentionally retains the ordered names and
 * leaves that explicitly excluded preference out of the project settings.
 */
struct AuralListeningSettings {
    /** Java nVolumes is the highest button label; buttons run from 0 through it. */
    std::uint32_t maximum_volume = 5;
    /** Initial selection for the operator's monitored-hydrophone checkboxes. */
    std::uint32_t hydrophone_bitmap = 3;
    std::vector<std::string> species{
        "Sperm Whale",
        "Dolphin Clicks",
        "Dolphin Whistles",
        "Ship Noise",
        "Airguns",
        "Other Noise",
    };
    std::vector<std::string> effort_statuses{
        "On Effort",
        "Off Effort",
    };

    bool operator==(const AuralListeningSettings&) const = default;
};

class AuralListeningProjectAdapterError final
    : public std::invalid_argument {
public:
    AuralListeningProjectAdapterError(
        std::string code,
        std::string message);

    [[nodiscard]] const std::string& code() const noexcept;

private:
    std::string code_;
};

/**
 * Java-authoritative Aural Listening controlled-unit descriptor for
 * PAMGuard 2.02.18e.
 *
 * The descriptor is unavailable until a dedicated runtime publishes the two
 * Java data shapes below. The existing GraphOperatorEvent runtime is not a
 * parity implementation.
 */
[[nodiscard]] ControlledUnitDescriptor
make_aural_listening_controlled_unit_descriptor();

[[nodiscard]] std::string
aural_listening_default_settings_json();

[[nodiscard]] std::string_view
aural_listening_settings_schema_json() noexcept;

[[nodiscard]] AuralListeningSettings
aural_listening_settings_from_json(
    std::string_view encoded,
    std::uint32_t settings_version);

[[nodiscard]] std::string
aural_listening_settings_to_json(
    const AuralListeningSettings& settings,
    std::uint32_t settings_version);

/**
 * Pure adapter named by kAuralListeningRuntimeSettingsAdapterId.
 *
 * It validates and canonicalizes the portable settings, then supplies the
 * fixed three-hour Java data-block lifetime and database string widths needed
 * by a future dedicated runtime.
 */
[[nodiscard]] std::string
aural_listening_runtime_settings_json(
    std::string_view portable_settings_json,
    std::uint32_t settings_version);

/**
 * Exact public data carried by one Java ListeningEffortData.
 *
 * The service owns time_milliseconds. Status is the selected ordered string,
 * not a generic category or on/off boolean, and channel_bitmap is the
 * operator's current monitored-hydrophone selection.
 */
struct AuralListeningEffortEntry {
    std::int64_t time_milliseconds = 0;
    std::string status;
    std::uint32_t channel_bitmap = 0;

    bool operator==(const AuralListeningEffortEntry&) const = default;
};

/**
 * Exact public data carried by one Java ThingHeard.
 *
 * A species/volume button stores a non-negative species index and volume.
 * Pressing Enter in the free-comment field stores Java's distinct
 * species_index == -1 and volume == -1 shape with no species item.
 */
struct AuralListeningThingHeardEntry {
    std::int64_t time_milliseconds = 0;
    std::int32_t species_index = -1;
    std::optional<std::string> species_name;
    std::int32_t volume = -1;
    std::uint32_t channel_bitmap = 0;
    std::string comment;

    bool operator==(const AuralListeningThingHeardEntry&) const = default;
};

/**
 * Convert {"statusIndex":N,"hydrophoneBitmap":B} to ListeningEffortData.
 * The timestamp is supplied separately so a client cannot spoof it.
 */
[[nodiscard]] AuralListeningEffortEntry
aural_listening_effort_entry_from_action_json(
    const AuralListeningSettings& settings,
    std::string_view action_json,
    std::int64_t time_milliseconds);

/**
 * Convert a Java species/volume button action. The document must contain
 * speciesIndex, volume, hydrophoneBitmap and comment exactly.
 */
[[nodiscard]] AuralListeningThingHeardEntry
aural_listening_thing_heard_from_species_action_json(
    const AuralListeningSettings& settings,
    std::string_view action_json,
    std::int64_t time_milliseconds);

/**
 * Convert Enter in Java's comment field. The document contains only
 * hydrophoneBitmap and comment; the exact -1 species/volume sentinels are
 * generated by this adapter.
 */
[[nodiscard]] AuralListeningThingHeardEntry
aural_listening_thing_heard_from_comment_action_json(
    std::string_view action_json,
    std::int64_t time_milliseconds);

[[nodiscard]] std::string
aural_listening_effort_entry_to_json(
    const AuralListeningEffortEntry& entry);

[[nodiscard]] std::string
aural_listening_thing_heard_entry_to_json(
    const AuralListeningThingHeardEntry& entry);

} // namespace pamguard::project
