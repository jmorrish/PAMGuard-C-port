#include "pamguard/project/ClipGeneratorControlledUnit.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json.hpp>

namespace {

using Json = nlohmann::json;
using pamguard::project::ClipGeneratorProjectionError;
using pamguard::project::ClipGeneratorTriggerEligibility;
using pamguard::project::ClipGeneratorTriggerSourceCandidate;
using pamguard::project::SourceReference;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const pamguard::project::PublicDataRoleDescriptor&
role(
    const pamguard::project::ControlledUnitDescriptor& descriptor,
    const std::string& id) {
    const auto found = std::find_if(
        descriptor.public_roles.begin(),
        descriptor.public_roles.end(),
        [&](const auto& candidate) {
            return candidate.id == id;
        });
    if (found == descriptor.public_roles.end()) {
        throw std::runtime_error(
            "Clip Generator descriptor omits role " + id);
    }
    return *found;
}

pamguard::project::PublicDataRoleDescriptor&
role(
    pamguard::project::ControlledUnitDescriptor& descriptor,
    const std::string& id) {
    const auto found = std::find_if(
        descriptor.public_roles.begin(),
        descriptor.public_roles.end(),
        [&](const auto& candidate) {
            return candidate.id == id;
        });
    if (found == descriptor.public_roles.end()) {
        throw std::runtime_error(
            "Clip Generator descriptor omits role " + id);
    }
    return *found;
}

void check_descriptor() {
    const auto descriptor =
        pamguard::project::
            make_clip_generator_controlled_unit_descriptor();
    require(
        descriptor.id == "pamguard.clip-generator" &&
            descriptor.descriptor_version == 1 &&
            descriptor.java_authority.registered_name ==
                "Clip generator" &&
            descriptor.java_authority.menu_group ==
                "Sound Processing" &&
            descriptor.java_authority.class_name ==
                "clipgenerator.ClipControl" &&
            descriptor.java_authority.tooltip ==
                "Generates and stores short clips of sound data in "
                "response to detections" &&
            descriptor.java_authority.help_point ==
                "sound_processing/ClipGenerator/docs/"
                "ClipGenerator.html" &&
            descriptor.instance_rules.minimum_instances == 0 &&
            !descriptor.instance_rules.maximum_instances &&
            descriptor.instance_rules.allowed_modes.size() == 3,
        "Clip Generator Java registration authority changed");

    const auto& audio = role(descriptor, "rawAudio");
    const auto& triggers = role(descriptor, "triggers");
    const auto& clips = role(descriptor, "clips");
    require(
        descriptor.public_roles.size() == 3 &&
            audio.direction ==
                pamguard::project::DataRoleDirection::Input &&
            audio.cardinality ==
                pamguard::project::RoleCardinality::ExactlyOne &&
            audio.data_type == "pamguard.raw-audio" &&
            audio.default_provider_controlled_unit_type_id ==
                "pamguard.acquisition" &&
            triggers.direction ==
                pamguard::project::DataRoleDirection::Input &&
            triggers.cardinality ==
                pamguard::project::RoleCardinality::ZeroOrMany &&
            triggers.data_type ==
                "pamguard.acoustic-data-unit" &&
            triggers.capabilities ==
                std::vector<std::string>{"clip-trigger"} &&
            triggers.java_data_class ==
                "PamguardMVC.AcousticDataUnit" &&
            clips.direction ==
                pamguard::project::DataRoleDirection::Output &&
            clips.data_type == "pamguard.audio-clip" &&
            clips.capabilities ==
                std::vector<std::string>{
                    "events",
                    "waveform",
                    "playable",
                },
        "Clip Generator typed public roles changed");

    require(
        descriptor.settings.version == 1 &&
            descriptor.settings.authority_classes ==
                std::vector<std::string>{
                    "clipgenerator.ClipSettings",
                    "clipgenerator.ClipGenSetting",
                } &&
            Json::parse(
                descriptor.settings.default_settings_json) ==
                Json{
                    {"storageMode", "binary"},
                    {"datedSubFolders", true},
                    {"triggerPolicies", Json::array()},
                } &&
            descriptor.settings.whole_tree_change_policy ==
                pamguard::project::SettingsChangePolicy::
                    StopRequired &&
            descriptor.settings.parity_status ==
                "java-fixture-validated",
        "Clip Generator canonical settings descriptor changed");

    const auto schema =
        Json::parse(
            descriptor.settings.settings_schema_json);
    const auto& storage_options =
        schema.at("properties")
            .at("storageMode")
            .at("enum");
    require(
        storage_options ==
                Json::array({
                    "wav-files",
                    "binary",
                    "both",
                }) &&
            std::find(
                storage_options.begin(),
                storage_options.end(),
                Json("annotation")) ==
                storage_options.end() &&
            schema.at("x-pamguard-portable-boundaries")
                .dump()
                .find("STORE_ANNOTATION") !=
                std::string::npos,
        "Unsupported Java annotation storage is not explicit");

    require(
        descriptor.runtime_recipe.id ==
                "pamguard.clip-generator.runtime" &&
            descriptor.runtime_recipe.children.size() == 1 &&
            descriptor.runtime_recipe.children.front().role_id ==
                "generator" &&
            descriptor.runtime_recipe.children.front()
                    .runtime_type_id ==
                "pamguard.clip-generator" &&
            descriptor.runtime_recipe.children.front()
                    .settings.source_pointer.empty() &&
            descriptor.runtime_recipe.children.front()
                    .settings.adapter_id ==
                "pamguard.clip-generator-settings.v1" &&
            descriptor.runtime_recipe.public_role_mappings.size() ==
                3 &&
            descriptor.availability ==
                pamguard::project::AvailabilityStatus::Available &&
            descriptor.parity_status == "experimental",
        "Clip Generator runtime expansion recipe changed");

    // Prove the descriptor is structurally valid against the exact low-level
    // contract Phase 5 must expose. Remove the default-provider reference only
    // because this focused checker does not register Acquisition.
    auto standalone = descriptor;
    role(standalone, "rawAudio")
        .default_provider_controlled_unit_type_id.reset();
    pamguard::project::ControlledUnitRegistry registry;
    registry.register_controlled_unit(
        std::move(standalone));
    const std::vector<
        pamguard::project::LowLevelTypeContract>
        low_level{
            {
                "pamguard.clip-generator",
                {
                    {
                        "audio",
                        pamguard::project::
                            DataRoleDirection::Input,
                        "pamguard.raw-audio",
                        {"sampled"},
                    },
                    {
                        "triggers",
                        pamguard::project::
                            DataRoleDirection::Input,
                        "pamguard.acoustic-data-unit",
                        {"clip-trigger"},
                    },
                    {
                        "clips",
                        pamguard::project::
                            DataRoleDirection::Output,
                        "pamguard.audio-clip",
                        {
                            "events",
                            "waveform",
                            "playable",
                        },
                    },
                },
            },
        };
    require(
        registry.validate_against(low_level).valid(),
        "Clip Generator descriptor is not structurally valid against "
        "its declared low-level contract");
}

ClipGeneratorTriggerSourceCandidate candidate(
    std::string unit_id,
    std::string output_role,
    std::string controlled_type,
    std::string block_id,
    std::string data_type,
    std::vector<std::string> capabilities) {
    return {
        {
            std::move(unit_id),
            std::move(output_role),
        },
        std::move(controlled_type),
        std::move(block_id),
        std::move(data_type),
        std::move(capabilities),
    };
}

void check_source_eligibility() {
    const auto click = candidate(
        "click-detector-1",
        "clicks",
        "pamguard.click-detector",
        "click-detector-1:localiser:accepted",
        "pamguard.click",
        {
            "detections",
            "clip-trigger",
        });
    const auto click_assessment =
        pamguard::project::
            assess_clip_generator_trigger_source(click);
    require(
        !click_assessment.eligible() &&
            click_assessment.eligibility ==
                ClipGeneratorTriggerEligibility::
                    ClickDetectorExplicitlyDisabled,
        "Click Detector became an eligible Clip Generator source");

    const auto mark = candidate(
        "clip-generator-1",
        "spectrogramMarks",
        "pamguard.clip-generator",
        "clip-generator-1:generator:spectrogram-marks",
        "pamguard.spectrogram-mark",
        {
            "clip-trigger",
            "spectrogram-mark",
        });
    const auto mark_assessment =
        pamguard::project::
            assess_clip_generator_trigger_source(mark);
    require(
        mark_assessment.eligible() &&
            mark_assessment.spectrogram_mark &&
            !mark_assessment.default_use_data_budget,
        "Spectrogram Marks lost their no-budget eligible default");

    const auto ishmael = candidate(
        "ishmael-1",
        "detections",
        "pamguard.ishmael-energy-sum",
        "ishmael-1:detector:detections",
        "pamguard.ishmael-detection",
        {
            "detections",
            "clip-trigger",
        });
    const auto ishmael_assessment =
        pamguard::project::
            assess_clip_generator_trigger_source(ishmael);
    require(
        ishmael_assessment.eligible() &&
            !ishmael_assessment.spectrogram_mark &&
            ishmael_assessment.default_use_data_budget,
        "Capability-driven detector trigger eligibility changed");

    const auto fft = candidate(
        "fft-1",
        "fft",
        "pamguard.fft",
        "fft-1:fft-process:fft",
        "pamguard.fft",
        {"frequency-domain"});
    require(
        !pamguard::project::
             assess_clip_generator_trigger_source(fft)
                 .eligible(),
        "A source without clip-trigger became eligible");
}

Json policy(
    const std::string& unit_id,
    const std::string& output_role,
    double before,
    double after,
    bool use_budget) {
    return {
        {
            "triggerSource",
            {
                {"unitId", unit_id},
                {"outputRole", output_role},
            },
        },
        {"enabled", true},
        {"secondsBeforeTrigger", before},
        {"secondsAfterTrigger", after},
        {
            "channelSelection",
            "detection-channels-only",
        },
        {"clipPrefix", nullptr},
        {"useDataBudget", use_budget},
        {"dataBudgetKilobytes", 10240},
        {"budgetPeriodHours", 24.0},
    };
}

void require_projection_error(
    const Json& settings,
    const std::vector<SourceReference>& bindings,
    const std::vector<ClipGeneratorTriggerSourceCandidate>&
        candidates,
    const std::string& expected_code) {
    bool rejected = false;
    try {
        (void) pamguard::project::
            project_clip_generator_runtime_settings(
                settings.dump(),
                1,
                bindings,
                candidates);
    }
    catch (const ClipGeneratorProjectionError& error) {
        rejected = error.code() == expected_code;
    }
    require(
        rejected,
        "Clip Generator projection did not report the expected error");
}

void check_project_projection_adapter() {
    const SourceReference whistle{
        "whistle-1",
        "contours",
    };
    const SourceReference mark{
        "clip-generator-1",
        "spectrogramMarks",
    };
    const std::vector<
        ClipGeneratorTriggerSourceCandidate>
        candidates{
            candidate(
                whistle.unit_id,
                whistle.output_role,
                "pamguard.whistles-moans",
                "whistle-1:contour-connect:contours",
                "pamguard.whistle-contour",
                {
                    "detections",
                    "overlay",
                    "clip-trigger",
                }),
            candidate(
                mark.unit_id,
                mark.output_role,
                "pamguard.clip-generator",
                "clip-generator-1:generator:"
                "spectrogram-marks",
                "pamguard.spectrogram-mark",
                {
                    "clip-trigger",
                    "spectrogram-mark",
                }),
            candidate(
                "click-1",
                "clicks",
                "pamguard.click-detector",
                "click-1:localiser:accepted",
                "pamguard.click",
                {"detections"}),
        };
    const Json settings{
        {"storageMode", "both"},
        {"datedSubFolders", false},
        {
            "triggerPolicies",
            Json::array({
                policy(
                    whistle.unit_id,
                    whistle.output_role,
                    1.25,
                    2.5,
                    true),
                policy(
                    mark.unit_id,
                    mark.output_role,
                    0.0,
                    0.0,
                    false),
            }),
        },
    };

    // Set equality is order-independent; policy order remains canonical.
    const std::vector<SourceReference> bindings{
        mark,
        whistle,
    };
    const auto projection =
        pamguard::project::
            project_clip_generator_runtime_settings(
                settings.dump(),
                1,
                bindings,
                candidates);
    const auto runtime =
        Json::parse(projection.settings_json);
    require(
        projection.required_history_seconds == 3.75 &&
            projection.trigger_policies.size() == 2 &&
            projection.trigger_policies[0].source ==
                whistle &&
            projection.trigger_policies[0].runtime_block_id ==
                "whistle-1:contour-connect:contours" &&
            !projection.trigger_policies[0].spectrogram_mark &&
            projection.trigger_policies[1].source == mark &&
            projection.trigger_policies[1].spectrogram_mark &&
            runtime.at("storageMode") == "both" &&
            runtime.at("datedSubFolders") == false &&
            runtime.at("requiredHistorySeconds") == 3.75 &&
            runtime.at("triggerPolicies").size() == 2 &&
            runtime.at("triggerPolicies")
                    .at(0)
                    .at("runtimeBlockId") ==
                "whistle-1:contour-connect:contours" &&
            runtime.at("triggerPolicies")
                    .at(0)
                    .at("sourceDataType") ==
                "pamguard.whistle-contour" &&
            runtime.at("triggerPolicies")
                    .at(1)
                    .at("spectrogramMark") == true,
        "Clip Generator context-aware runtime adapter changed");

    const Json defaults{
        {"storageMode", "binary"},
        {"datedSubFolders", true},
        {"triggerPolicies", Json::array()},
    };
    const auto empty =
        pamguard::project::
            project_clip_generator_runtime_settings(
                defaults.dump(),
                1,
                {},
                candidates);
    require(
        empty.required_history_seconds == 0.0 &&
            empty.trigger_policies.empty() &&
            Json::parse(empty.settings_json)
                    .at("triggerPolicies")
                    .empty(),
        "Clip Generator zero-trigger projection is not runnable");

    require_projection_error(
        settings,
        {whistle},
        candidates,
        "trigger-policy-binding-mismatch");
    require_projection_error(
        settings,
        {
            whistle,
            whistle,
        },
        candidates,
        "duplicate-trigger-binding");

    const SourceReference click{
        "click-1",
        "clicks",
    };
    auto click_settings = defaults;
    click_settings["triggerPolicies"].push_back(
        policy(
            click.unit_id,
            click.output_role,
            0.0,
            0.0,
            true));
    require_projection_error(
        click_settings,
        {click},
        candidates,
        "click-detector-trigger-ineligible");

    const SourceReference fft{
        "fft-1",
        "fft",
    };
    auto fft_settings = defaults;
    fft_settings["triggerPolicies"].push_back(
        policy(
            fft.unit_id,
            fft.output_role,
            0.0,
            0.0,
            true));
    auto candidates_with_fft = candidates;
    candidates_with_fft.push_back(
        candidate(
            fft.unit_id,
            fft.output_role,
            "pamguard.fft",
            "fft-1:fft-process:fft",
            "pamguard.fft",
            {"frequency-domain"}));
    require_projection_error(
        fft_settings,
        {fft},
        candidates_with_fft,
        "missing-clip-trigger-capability");
}

} // namespace

int main() {
    try {
        check_descriptor();
        check_source_eligibility();
        check_project_projection_adapter();
        std::cout
            << "Clip Generator controlled-unit authority, typed "
               "multi-source receiver ownership, source eligibility, "
               "unsupported annotation boundary, and context-aware "
               "runtime projection passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
