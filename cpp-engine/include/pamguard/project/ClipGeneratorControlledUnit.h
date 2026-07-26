#pragma once

#include "pamguard/core/ClipGeneratorSettings.h"
#include "pamguard/project/ControlledUnitRegistry.h"
#include "pamguard/project/ProjectDocument.h"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pamguard::project {

inline constexpr std::string_view
    kClipGeneratorControlledUnitTypeId =
        "pamguard.clip-generator";
inline constexpr std::string_view
    kClipGeneratorAcousticDataUnitType =
        "pamguard.acoustic-data-unit";
inline constexpr std::string_view
    kClipGeneratorTriggerCapability =
        "clip-trigger";
inline constexpr std::string_view
    kClipGeneratorSpectrogramMarkCapability =
        "spectrogram-mark";
inline constexpr std::string_view
    kClipGeneratorRuntimeSettingsAdapterId =
        "pamguard.clip-generator-settings.v1";

/**
 * Java-authoritative descriptor for clipgenerator.ClipControl in
 * PAMGuard 2.02.18e.
 *
 * The triggers role deliberately models Java's AcousticDataUnit +
 * PamDataBlock.canClipGenerate contract. The source keeps its concrete data
 * type; pamguard.acoustic-data-unit is the polymorphic input contract and
 * clip-trigger is the eligibility capability.
 */
[[nodiscard]] ControlledUnitDescriptor
make_clip_generator_controlled_unit_descriptor();

/**
 * One public output which may be shown in the Clip Generator trigger table.
 *
 * Candidate discovery must include acoustic outputs which are ineligible, not
 * just compatible outputs. That is how the browser can honestly show that
 * Click Detector is present while Java explicitly disables clip generation
 * from its output.
 */
struct ClipGeneratorTriggerSourceCandidate {
    SourceReference source;
    ControlledUnitTypeId controlled_unit_type_id;
    /** Projected block identity; never persisted in controlled-unit settings. */
    std::string runtime_block_id;
    /** Concrete source data type, retained for runtime payload dispatch. */
    std::string data_type;
    std::vector<std::string> capabilities;

    bool operator==(
        const ClipGeneratorTriggerSourceCandidate&) const = default;
};

enum class ClipGeneratorTriggerEligibility {
    Eligible,
    ClickDetectorExplicitlyDisabled,
    MissingClipTriggerCapability,
};

struct ClipGeneratorTriggerAssessment {
    ClipGeneratorTriggerEligibility eligibility =
        ClipGeneratorTriggerEligibility::
            MissingClipTriggerCapability;
    bool spectrogram_mark = false;
    /** Java ClipControl defaults Spectrogram Marks to record everything. */
    bool default_use_data_budget = true;
    std::string reason;

    [[nodiscard]] bool eligible() const noexcept {
        return eligibility ==
            ClipGeneratorTriggerEligibility::Eligible;
    }
};

/**
 * Apply the pinned Java source-eligibility rules.
 *
 * Click Detector remains ineligible even if stale metadata accidentally
 * advertises clip-trigger. A Spectrogram Mark is identified explicitly by the
 * spectrogram-mark capability and is eligible only when it also advertises
 * clip-trigger.
 */
[[nodiscard]] ClipGeneratorTriggerAssessment
assess_clip_generator_trigger_source(
    const ClipGeneratorTriggerSourceCandidate& candidate);

class ClipGeneratorProjectionError final
    : public std::invalid_argument {
public:
    ClipGeneratorProjectionError(
        std::string code,
        std::string message);

    [[nodiscard]] const std::string& code() const noexcept;

private:
    std::string code_;
};

struct ClipGeneratorResolvedTriggerPolicy {
    SourceReference source;
    std::string runtime_block_id;
    std::string source_data_type;
    bool spectrogram_mark = false;
    core::ClipGeneratorTriggerPolicySettings policy;

    bool operator==(
        const ClipGeneratorResolvedTriggerPolicy&) const = default;
};

/**
 * Context-aware result of the Clip Generator settings adapter.
 *
 * required_history_seconds is derived from the largest enabled Java
 * pre+post window. It is not an independently persisted setting.
 */
struct ClipGeneratorRuntimeSettingsProjection {
    std::string settings_json;
    double required_history_seconds = 0.0;
    std::vector<ClipGeneratorResolvedTriggerPolicy> trigger_policies;

    bool operator==(
        const ClipGeneratorRuntimeSettingsProjection&) const = default;
};

/**
 * Resolve canonical receiver-owned policies to projected runtime blocks.
 *
 * This is the context-aware half of
 * pamguard.clip-generator-settings.v1. It enforces exact set equality between
 * the public triggers binding and settings triggerPolicies, rejects
 * ineligible sources, and adds runtimeBlockId/sourceDataType only to the
 * generated low-level settings. Stable {unitId, outputRole} references remain
 * the sole persisted source identity.
 *
 * Candidate catalogues may also contain unbound/ineligible rows for UI
 * discovery. Every bound source must resolve to exactly one candidate.
 */
[[nodiscard]] ClipGeneratorRuntimeSettingsProjection
project_clip_generator_runtime_settings(
    std::string_view canonical_settings_json,
    std::uint32_t settings_version,
    std::span<const SourceReference> bound_trigger_sources,
    std::span<const ClipGeneratorTriggerSourceCandidate>
        source_candidates);

} // namespace pamguard::project
