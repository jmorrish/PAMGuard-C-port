#include "pamguard/project/ClipGeneratorControlledUnit.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include <json.hpp>

namespace pamguard::project {

namespace {

using Json = nlohmann::json;
using SourceKey = std::pair<std::string, std::string>;

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

SourceKey source_key(const SourceReference& source) {
    return {
        source.unit_id,
        source.output_role,
    };
}

SourceKey source_key(
    const core::ClipGeneratorTriggerSourceReference& source) {
    return {
        source.unit_id,
        source.output_role,
    };
}

bool has_capability(
    const ClipGeneratorTriggerSourceCandidate& candidate,
    std::string_view capability) {
    return std::find(
               candidate.capabilities.begin(),
               candidate.capabilities.end(),
               capability) != candidate.capabilities.end();
}

std::vector<SettingDefaultDescriptor> default_evidence() {
    return {
        {
            "/storageMode",
            "storageOption",
            R"("binary")",
            {},
            "Java integer STORE_BINARY=1 uses a stable portable name; "
            "STORE_ANNOTATION=2 is intentionally unavailable",
            "clipgenerator.ClipSettings#storageOption",
        },
        {
            "/datedSubFolders",
            "datedSubFolders",
            "true",
            {},
            {},
            "clipgenerator.ClipSettings#datedSubFolders",
        },
        {
            "/triggerPolicies",
            "clipGenSettings",
            "[]",
            {},
            "fresh ClipSettings.clipGenSettings is null; the portable "
            "canonical form is an empty receiver-owned array",
            "clipgenerator.ClipSettings#clipGenSettings",
        },
    };
}

PublicDataRoleDescriptor raw_audio_input() {
    return {
        "rawAudio",
        "Audio Data Source",
        DataRoleDirection::Input,
        "pamguard.raw-audio",
        RoleCardinality::ExactlyOne,
        {"sampled"},
        "PamDetection.RawDataUnit",
        "pamguard.acquisition",
    };
}

PublicDataRoleDescriptor triggers_input() {
    return {
        "triggers",
        "Data Triggers",
        DataRoleDirection::Input,
        std::string(kClipGeneratorAcousticDataUnitType),
        RoleCardinality::ZeroOrMany,
        {std::string(kClipGeneratorTriggerCapability)},
        "PamguardMVC.AcousticDataUnit",
        std::nullopt,
    };
}

PublicDataRoleDescriptor clips_output() {
    return {
        "clips",
        "Generated clips",
        DataRoleDirection::Output,
        "pamguard.audio-clip",
        RoleCardinality::ExactlyOne,
        {
            "events",
            "waveform",
            "playable",
        },
        {},
        std::nullopt,
    };
}

std::string projection_message(
    const SourceReference& source,
    std::string_view suffix) {
    return "Clip Generator trigger source '" +
        source.unit_id + "/" + source.output_role +
        "' " + std::string(suffix);
}

} // namespace

ControlledUnitDescriptor
make_clip_generator_controlled_unit_descriptor() {
    return {
        std::string(kClipGeneratorControlledUnitTypeId),
        1,
        {
            "Clip generator",
            "Sound Processing",
            "clipgenerator.ClipControl",
            "direct",
            "Generates and stores short clips of sound data in response "
            "to detections",
            "sound_processing/ClipGenerator/docs/ClipGenerator.html",
            {
                "src/PamModel/PamModel.java",
                "src/clipgenerator/ClipControl.java",
                "src/clipgenerator/ClipSettings.java",
                "src/clipgenerator/ClipGenSetting.java",
                "src/clipgenerator/ClipDialog.java",
                "src/clipgenerator/ClipGenSettingDialog.java",
                "src/clipgenerator/ClipProcess.java",
                "src/clipgenerator/StandardClipBudgetMaker.java",
                "src/clipgenerator/ClipSpectrogramMarkDataBlock.java",
                "src/PamguardMVC/PamDataBlock.java",
                "src/clickDetector/ClickDetector.java",
                "src/IshmaelDetector/IshPeakProcess.java",
                "src/whistlesAndMoans/"
                "WhistleToneConnectProcess.java",
                "src/clickTrainDetector/ClickTrainProcess.java",
                "src/clickTrainDetector/CTClassificationProcess.java",
            },
        },
        unlimited_in_all_modes(),
        {
            raw_audio_input(),
            triggers_input(),
            clips_output(),
        },
        {
            1,
            {
                "clipgenerator.ClipSettings",
                "clipgenerator.ClipGenSetting",
            },
            {
                "src/clipgenerator/ClipSettings.java",
                "src/clipgenerator/ClipGenSetting.java",
                "src/clipgenerator/ClipControl.java",
                "src/clipgenerator/ClipDialog.java",
                "src/clipgenerator/ClipGenSettingDialog.java",
                "src/clipgenerator/ClipProcess.java",
                "src/clipgenerator/StandardClipBudgetMaker.java",
                "src/clipgenerator/ClipSpectrogramMarkDataBlock.java",
            },
            core::clip_generator_default_settings_json(),
            {
                {
                    "settings.source",
                    {
                        "Audio Data Source",
                    },
                },
                {
                    "settings.storage",
                    {
                        "Storage options",
                        "Store in wav files",
                        "Store in binary files",
                        "Store data in sub folders by date",
                        "Annotation storage is unavailable",
                    },
                },
                {
                    "settings.data-triggers",
                    {
                        "Data Triggers",
                        "Data Name",
                        "Enabled",
                        "Settings",
                    },
                },
                {
                    "settings.clip-generation",
                    {
                        "Clip Generation",
                        "Channel selection",
                        "Detection Channels Only",
                        "First Detection Channel Only",
                        "All Channels",
                        "Time before trigger",
                        "Time after trigger",
                        "File initials",
                    },
                },
                {
                    "settings.data-budget",
                    {
                        "Data Budget",
                        "Record everything",
                        "Budget data",
                        "Data budget",
                        "Megabytes",
                        "Budget period",
                        "Hours",
                    },
                },
            },
            default_evidence(),
            SettingsChangePolicy::StopRequired,
            "java-fixture-validated",
            std::string(
                core::clip_generator_settings_schema_json()),
        },
        {
            1,
            {
                {
                    "generator",
                    "pamguard.clip-generator",
                    {
                        "",
                        std::string(
                            kClipGeneratorRuntimeSettingsAdapterId),
                    },
                    true,
                    AvailabilityStatus::Available,
                    "multi-source-runtime-required",
                },
            },
            {
                {
                    "rawAudio",
                    {
                        "generator",
                        "audio",
                    },
                },
                {
                    "triggers",
                    {
                        "generator",
                        "triggers",
                    },
                },
                {
                    "clips",
                    {
                        "generator",
                        "clips",
                    },
                },
            },
            {},
            {},
            "pamguard.clip-generator.runtime",
        },
        AvailabilityStatus::Available,
        "experimental",
    };
}

ClipGeneratorTriggerAssessment
assess_clip_generator_trigger_source(
    const ClipGeneratorTriggerSourceCandidate& candidate) {
    const auto spectrogram_mark = has_capability(
        candidate,
        kClipGeneratorSpectrogramMarkCapability);
    if (candidate.controlled_unit_type_id ==
        "pamguard.click-detector") {
        return {
            ClipGeneratorTriggerEligibility::
                ClickDetectorExplicitlyDisabled,
            spectrogram_mark,
            true,
            "PAMGuard 2.02.18e ClickDetector explicitly calls "
            "setCanClipGenerate(false) because click rates can create "
            "too many clips",
        };
    }
    if (!has_capability(
            candidate,
            kClipGeneratorTriggerCapability)) {
        return {
            ClipGeneratorTriggerEligibility::
                MissingClipTriggerCapability,
            spectrogram_mark,
            !spectrogram_mark,
            "The source does not advertise the Java-equivalent "
            "clip-trigger capability",
        };
    }
    return {
        ClipGeneratorTriggerEligibility::Eligible,
        spectrogram_mark,
        !spectrogram_mark,
        spectrogram_mark
            ? "Spectrogram Marks are eligible and new policies default "
              "to Record everything, matching ClipControl"
            : "The source advertises the Java-equivalent clip-trigger "
              "capability",
    };
}

ClipGeneratorProjectionError::ClipGeneratorProjectionError(
    std::string code,
    std::string message)
    : std::invalid_argument(std::move(message)),
      code_(std::move(code)) {}

const std::string&
ClipGeneratorProjectionError::code() const noexcept {
    return code_;
}

ClipGeneratorRuntimeSettingsProjection
project_clip_generator_runtime_settings(
    std::string_view canonical_settings_json,
    std::uint32_t settings_version,
    std::span<const SourceReference> bound_trigger_sources,
    std::span<const ClipGeneratorTriggerSourceCandidate>
        source_candidates) {
    core::ClipGeneratorSettings settings;
    try {
        settings =
            core::clip_generator_settings_from_json(
                canonical_settings_json,
                settings_version);
    }
    catch (const core::ClipGeneratorSettingsError& error) {
        throw ClipGeneratorProjectionError(
            "invalid-clip-generator-settings",
            error.what());
    }

    std::set<SourceKey> bound_keys;
    for (const auto& source : bound_trigger_sources) {
        if (!bound_keys.emplace(source_key(source)).second) {
            throw ClipGeneratorProjectionError(
                "duplicate-trigger-binding",
                projection_message(
                    source,
                    "is bound more than once"));
        }
    }

    std::map<
        SourceKey,
        const core::ClipGeneratorTriggerPolicySettings*>
        policies_by_source;
    for (const auto& policy : settings.trigger_policies) {
        policies_by_source.emplace(
            source_key(policy.trigger_source),
            &policy);
    }
    if (bound_keys.size() != policies_by_source.size()) {
        throw ClipGeneratorProjectionError(
            "trigger-policy-binding-mismatch",
            "Clip Generator triggerPolicies must correspond one-to-one "
            "with sources on the receiver-owned triggers binding");
    }
    for (const auto& source : bound_trigger_sources) {
        if (!policies_by_source.contains(source_key(source))) {
            throw ClipGeneratorProjectionError(
                "trigger-policy-binding-mismatch",
                projection_message(
                    source,
                    "has no receiver-owned trigger policy"));
        }
    }

    std::map<
        SourceKey,
        const ClipGeneratorTriggerSourceCandidate*>
        candidates_by_source;
    for (const auto& candidate : source_candidates) {
        if (!candidates_by_source.emplace(
                 source_key(candidate.source),
                 &candidate).second) {
            throw ClipGeneratorProjectionError(
                "duplicate-trigger-source-candidate",
                projection_message(
                    candidate.source,
                    "appears more than once in source discovery"));
        }
    }

    ClipGeneratorRuntimeSettingsProjection projection;
    Json runtime_policies = Json::array();
    const auto canonical =
        Json::parse(
            core::clip_generator_settings_to_json(
                settings,
                settings_version));

    for (std::size_t index = 0;
         index < settings.trigger_policies.size();
         ++index) {
        const auto& policy =
            settings.trigger_policies[index];
        const SourceReference source{
            policy.trigger_source.unit_id,
            policy.trigger_source.output_role,
        };
        const auto candidate =
            candidates_by_source.find(source_key(source));
        if (candidate == candidates_by_source.end()) {
            throw ClipGeneratorProjectionError(
                "unresolved-trigger-source",
                projection_message(
                    source,
                    "does not resolve to a projected output"));
        }
        const auto& resolved = *candidate->second;
        const auto assessment =
            assess_clip_generator_trigger_source(resolved);
        if (assessment.eligibility ==
            ClipGeneratorTriggerEligibility::
                ClickDetectorExplicitlyDisabled) {
            throw ClipGeneratorProjectionError(
                "click-detector-trigger-ineligible",
                projection_message(
                    source,
                    "is ineligible because Java ClickDetector disables "
                    "clip generation"));
        }
        if (!assessment.eligible()) {
            throw ClipGeneratorProjectionError(
                "missing-clip-trigger-capability",
                projection_message(
                    source,
                    "does not advertise clip-trigger"));
        }
        if (resolved.runtime_block_id.empty()) {
            throw ClipGeneratorProjectionError(
                "missing-trigger-runtime-block",
                projection_message(
                    source,
                    "has no projected runtime block"));
        }
        if (resolved.data_type.empty()) {
            throw ClipGeneratorProjectionError(
                "missing-trigger-data-type",
                projection_message(
                    source,
                    "has no concrete data type"));
        }

        projection.required_history_seconds = std::max(
            projection.required_history_seconds,
            policy.enabled
                ? policy.seconds_before_trigger +
                    policy.seconds_after_trigger
                : 0.0);
        projection.trigger_policies.push_back({
            source,
            resolved.runtime_block_id,
            resolved.data_type,
            assessment.spectrogram_mark,
            policy,
        });

        auto runtime_policy =
            canonical.at("triggerPolicies").at(index);
        runtime_policy["runtimeBlockId"] =
            resolved.runtime_block_id;
        runtime_policy["sourceDataType"] =
            resolved.data_type;
        runtime_policy["spectrogramMark"] =
            assessment.spectrogram_mark;
        runtime_policies.push_back(
            std::move(runtime_policy));
    }

    projection.settings_json = Json{
        {
            "storageMode",
            canonical.at("storageMode"),
        },
        {
            "datedSubFolders",
            canonical.at("datedSubFolders"),
        },
        {
            "requiredHistorySeconds",
            projection.required_history_seconds,
        },
        {
            "triggerPolicies",
            std::move(runtime_policies),
        },
    }.dump();
    return projection;
}

} // namespace pamguard::project
