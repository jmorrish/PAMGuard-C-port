#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <json.hpp>

#include "pamguard/project/AuralListeningControlledUnit.h"
#include "pamguard/project/ControlledUnitRegistry.h"

namespace {

using Json = nlohmann::json;
using pamguard::project::AuralListeningProjectAdapterError;
using pamguard::project::AvailabilityStatus;
using pamguard::project::ControlledUnitDescriptor;
using pamguard::project::ControlledUnitRegistry;
using pamguard::project::DataRoleDirection;
using pamguard::project::LowLevelPortContract;
using pamguard::project::LowLevelTypeContract;
using pamguard::project::RunMode;
using pamguard::project::SettingsChangePolicy;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const pamguard::project::SettingsSectionDescriptor&
section(
    const ControlledUnitDescriptor& descriptor,
    const std::string& surface) {
    const auto found = std::find_if(
        descriptor.settings.sections.begin(),
        descriptor.settings.sections.end(),
        [&](const auto& value) {
            return value.surface == surface;
        });
    require(
        found != descriptor.settings.sections.end(),
        "Missing Aural Listening surface: " + surface);
    return *found;
}

void require_adapter_error(
    std::string_view expected_code,
    const auto& action) {
    bool rejected = false;
    try {
        action();
    }
    catch (const AuralListeningProjectAdapterError& error) {
        rejected = error.code() == expected_code;
    }
    require(
        rejected,
        "Aural Listening adapter did not reject with code " +
            std::string(expected_code));
}

void check_descriptor(
    const ControlledUnitDescriptor& descriptor) {
    require(
        descriptor.id == "pamguard.aural-listening" &&
            descriptor.descriptor_version == 1 &&
            descriptor.java_authority.registered_name ==
                "Aural Listening Form" &&
            descriptor.java_authority.menu_group == "Utilities" &&
            descriptor.java_authority.class_name ==
                "listening.ListeningControl" &&
            descriptor.java_authority.relationship == "direct" &&
            descriptor.java_authority.tooltip ==
                "Creates a form for the user to manually log things they "
                "hear" &&
            descriptor.java_authority.help_point ==
                "utilities/listening/docs/Listening_Overview.html",
        "Aural Listening pinned Java authority identity changed");
    require(
        descriptor.instance_rules.minimum_instances == 0 &&
            descriptor.instance_rules.maximum_instances ==
                std::nullopt &&
            descriptor.instance_rules.allowed_modes ==
                std::vector<RunMode>{
                    RunMode::Normal,
                    RunMode::Mixed,
                    RunMode::Viewer,
                } &&
            descriptor.instance_rules.mode_overrides.empty(),
        "Aural Listening Java multiplicity or run modes changed");

    require(
        descriptor.public_roles.size() == 2 &&
            std::none_of(
                descriptor.public_roles.begin(),
                descriptor.public_roles.end(),
                [](const auto& role) {
                    return role.direction ==
                        DataRoleDirection::Input;
                }),
        "Aural Listening must have no input and exactly two outputs");
    const auto& effort = descriptor.public_roles.at(0);
    const auto& things = descriptor.public_roles.at(1);
    require(
        effort.id == "effort" &&
            effort.name == "Listening Effort" &&
            effort.direction == DataRoleDirection::Output &&
            effort.data_type == "pamguard.listening-effort" &&
            things.id == "thingsHeard" &&
            things.name == "Things Heard" &&
            things.direction == DataRoleDirection::Output &&
            things.data_type == "pamguard.thing-heard",
        "Aural Listening Java output block identities changed");
    require(
        effort.data_type != "pamguard.operator-event" &&
            things.data_type != "pamguard.operator-event",
        "Aural Listening must not claim parity through GraphOperatorEvent");

    require(
        descriptor.settings.version == 1 &&
            descriptor.settings.authority_classes ==
                std::vector<std::string>{
                    "listening.ListeningParameters",
                    "listening.SpeciesItem",
                } &&
            descriptor.settings.whole_tree_change_policy ==
                SettingsChangePolicy::LiveSafe &&
            descriptor.settings.parity_status ==
                "java-source-validated-operational-settings",
        "Aural Listening settings authority changed");
    require(
        section(
            descriptor,
            "menu.detection").labels ==
                std::vector<std::string>{
                    "<unit name> Settings...",
                },
        "Aural Listening Detection menu action changed");
    require(
        section(
            descriptor,
            "settings.species-sound-types.actions").labels ==
                std::vector<std::string>{
                    "Add ...",
                    "Remove",
                    "Edit ...",
                    "Move up",
                    "Move Down",
                    "Edit symbol (double-click Sym; Java display "
                    "preference deferred)",
                },
        "Aural Listening configuration action order changed");
    require(
        section(
            descriptor,
            "operator.sections").labels ==
                std::vector<std::string>{
                    "Effort",
                    "Things Heard",
                    "History",
                } &&
            section(
                descriptor,
                "operator.effort.actions").labels ==
                std::vector<std::string>{
                    "On Effort",
                    "Off Effort",
                    "Hydrophones Monitored :",
                } &&
            section(
                descriptor,
                "operator.history.columns").labels ==
                std::vector<std::string>{
                    "Time",
                    "Species",
                    "Volume",
                    "Comment",
                },
        "Aural Listening operator surface order changed");

    require(
        descriptor.runtime_recipe.id ==
                "pamguard.aural-listening.runtime" &&
            descriptor.runtime_recipe.children.size() == 1 &&
            descriptor.runtime_recipe.public_role_mappings.size() ==
                2 &&
            descriptor.runtime_recipe.children.front().role_id ==
                "listening-process" &&
            descriptor.runtime_recipe.children.front().
                runtime_type_id ==
                "pamguard.aural-listening" &&
            descriptor.runtime_recipe.children.front().
                settings.adapter_id ==
                "pamguard.aural-listening-settings.v1" &&
            descriptor.runtime_recipe.children.front().
                availability == AvailabilityStatus::Unavailable &&
            descriptor.availability ==
                AvailabilityStatus::Unavailable &&
            descriptor.parity_status ==
                "experimental",
        "Aural Listening runtime claim boundary changed");
}

void check_settings() {
    const auto defaults_json =
        pamguard::project::
            aural_listening_default_settings_json();
    const auto defaults = Json::parse(defaults_json);
    require(
        defaults ==
            Json{
                {"maximumVolume", 5},
                {"hydrophoneBitmap", 3},
                {
                    "species",
                    {
                        "Sperm Whale",
                        "Dolphin Clicks",
                        "Dolphin Whistles",
                        "Ship Noise",
                        "Airguns",
                        "Other Noise",
                    },
                },
                {
                    "effortStatuses",
                    {
                        "On Effort",
                        "Off Effort",
                    },
                },
            },
        "Aural Listening pinned ListeningParameters defaults changed");

    const auto decoded =
        pamguard::project::
            aural_listening_settings_from_json(
                defaults_json,
                1);
    require(
        decoded.maximum_volume == 5 &&
            decoded.hydrophone_bitmap == 3 &&
            decoded.species ==
                std::vector<std::string>{
                    "Sperm Whale",
                    "Dolphin Clicks",
                    "Dolphin Whistles",
                    "Ship Noise",
                    "Airguns",
                    "Other Noise",
                } &&
            decoded.effort_statuses ==
                std::vector<std::string>{
                    "On Effort",
                    "Off Effort",
                } &&
            pamguard::project::
                aural_listening_settings_to_json(
                    decoded,
                    1) == defaults_json,
        "Aural Listening settings did not round-trip canonically");

    const auto schema = Json::parse(
        pamguard::project::
            aural_listening_settings_schema_json());
    require(
        schema.at("type") == "object" &&
            schema.at("additionalProperties") == false &&
            schema.at("required").size() == 4 &&
            schema.at("properties").at("maximumVolume").
                at("minimum") == 0 &&
            schema.at("properties").at("hydrophoneBitmap").
                at("maximum") ==
                std::numeric_limits<std::uint32_t>::max(),
        "Aural Listening closed canonical settings schema changed");

    const auto runtime = Json::parse(
        pamguard::project::
            aural_listening_runtime_settings_json(
                defaults_json,
                1));
    require(
        runtime.at("settings") == defaults &&
            runtime.at("naturalLifetimeSeconds") == 10'800 &&
            runtime.at("speciesMaximumUtf16CodeUnits") == 50 &&
            runtime.at("databaseCommentUtf16CodeUnits") == 50,
        "Aural Listening Java process/storage constants changed");

    require_adapter_error(
        "unsupported-aural-listening-settings-version",
        [&] {
            static_cast<void>(
                pamguard::project::
                    aural_listening_settings_from_json(
                        defaults_json,
                        2));
        });
    require_adapter_error(
        "invalid-aural-listening-settings",
        [] {
            static_cast<void>(
                pamguard::project::
                    aural_listening_settings_from_json(
                        R"({"maximumVolume":5,"hydrophoneBitmap":3,"species":[],"effortStatuses":["On Effort"],"defaultCategory":"listening"})",
                        1));
        });

    // Java String.length() counts a supplementary scalar as two UTF-16 code
    // units. Forty-nine ASCII units plus U+1F40B therefore exceeds 50.
    const std::string too_long_species =
        std::string(49, 'a') + "\xF0\x9F\x90\x8B";
    require_adapter_error(
        "invalid-aural-listening-species",
        [&] {
            const auto value = defaults;
            auto altered = value;
            altered["species"] = Json::array({too_long_species});
            static_cast<void>(
                pamguard::project::
                    aural_listening_settings_from_json(
                        altered.dump(),
                        1));
        });
}

void check_distinct_events() {
    const auto settings =
        pamguard::project::
            aural_listening_settings_from_json(
                pamguard::project::
                    aural_listening_default_settings_json(),
                1);

    const auto effort =
        pamguard::project::
            aural_listening_effort_entry_from_action_json(
                settings,
                R"({"statusIndex":0,"hydrophoneBitmap":5})",
                1'722'000'000'100LL);
    require(
        effort.time_milliseconds == 1'722'000'000'100LL &&
            effort.status == "On Effort" &&
            effort.channel_bitmap == 5 &&
            Json::parse(
                pamguard::project::
                    aural_listening_effort_entry_to_json(
                        effort)) ==
                Json{
                    {"timeMilliseconds", 1'722'000'000'100LL},
                    {"status", "On Effort"},
                    {"channelBitmap", 5},
                },
        "Aural Listening effort action/data semantics changed");

    const auto heard =
        pamguard::project::
            aural_listening_thing_heard_from_species_action_json(
                settings,
                R"({"speciesIndex":2,"volume":5,"hydrophoneBitmap":3,"comment":"bearing east"})",
                1'722'000'000'200LL);
    require(
        heard.time_milliseconds == 1'722'000'000'200LL &&
            heard.species_index == 2 &&
            heard.species_name ==
                std::optional<std::string>{"Dolphin Whistles"} &&
            heard.volume == 5 &&
            heard.channel_bitmap == 3 &&
            heard.comment == "bearing east",
        "Aural Listening species/volume action semantics changed");
    const auto heard_json = Json::parse(
        pamguard::project::
            aural_listening_thing_heard_entry_to_json(
                heard));
    require(
        heard_json.at("speciesName") == "Dolphin Whistles" &&
            heard_json.at("speciesIndex") == 2 &&
            heard_json.at("volume") == 5,
        "Aural Listening Things Heard public data shape changed");

    const auto comment =
        pamguard::project::
            aural_listening_thing_heard_from_comment_action_json(
                R"({"hydrophoneBitmap":1,"comment":"faint click train"})",
                1'722'000'000'300LL);
    require(
        comment.species_index == -1 &&
            !comment.species_name.has_value() &&
            comment.volume == -1 &&
            comment.channel_bitmap == 1 &&
            Json::parse(
                pamguard::project::
                    aural_listening_thing_heard_entry_to_json(
                        comment)).at("speciesName").is_null(),
        "Aural Listening comment-only -1 sentinel semantics changed");

    require_adapter_error(
        "aural-listening-effort-status-out-of-range",
        [&] {
            static_cast<void>(
                pamguard::project::
                    aural_listening_effort_entry_from_action_json(
                        settings,
                        R"({"statusIndex":2,"hydrophoneBitmap":3})",
                        1));
        });
    require_adapter_error(
        "aural-listening-species-out-of-range",
        [&] {
            static_cast<void>(
                pamguard::project::
                    aural_listening_thing_heard_from_species_action_json(
                        settings,
                        R"({"speciesIndex":6,"volume":1,"hydrophoneBitmap":3,"comment":""})",
                        1));
        });
    require_adapter_error(
        "aural-listening-volume-out-of-range",
        [&] {
            static_cast<void>(
                pamguard::project::
                    aural_listening_thing_heard_from_species_action_json(
                        settings,
                        R"({"speciesIndex":0,"volume":6,"hydrophoneBitmap":3,"comment":""})",
                        1));
        });
    require_adapter_error(
        "invalid-aural-listening-comment-action",
        [&] {
            static_cast<void>(
                pamguard::project::
                    aural_listening_thing_heard_from_comment_action_json(
                        R"({"hydrophoneBitmap":3,"comment":"x","timeMilliseconds":0})",
                        1));
        });
}

void check_registry_boundary(
    const ControlledUnitDescriptor& descriptor) {
    ControlledUnitRegistry registry;
    registry.register_controlled_unit(descriptor);
    const auto structural = registry.validate();
    std::string structural_message =
        "Aural Listening descriptor failed structural registry validation";
    for (const auto& issue : structural.issues) {
        structural_message +=
            " [" + issue.code + ": " + issue.message + "]";
    }
    require(
        structural.valid(),
        structural_message);

    auto available_contract_descriptor = descriptor;
    available_contract_descriptor.availability =
        AvailabilityStatus::Available;
    available_contract_descriptor.runtime_recipe.children.front().
        availability = AvailabilityStatus::Available;
    ControlledUnitRegistry available_contract_registry;
    available_contract_registry.register_controlled_unit(
        std::move(available_contract_descriptor));

    const std::vector<LowLevelTypeContract> dedicated_contract{
        {
            "pamguard.aural-listening",
            {
                {
                    "effort",
                    DataRoleDirection::Output,
                    "pamguard.listening-effort",
                    {
                        "events",
                        "effort",
                        "hydrophone-selection",
                    },
                },
                {
                    "things-heard",
                    DataRoleDirection::Output,
                    "pamguard.thing-heard",
                    {
                        "events",
                        "detections",
                        "annotations",
                        "hydrophone-selection",
                    },
                },
            },
        },
    };
    require(
        available_contract_registry
            .validate_against(dedicated_contract)
            .valid(),
        "Aural Listening descriptor rejected its dedicated runtime "
        "contract");

    const std::vector<LowLevelTypeContract> legacy_generic_contract{
        {
            "pamguard.aural-listening",
            {
                {
                    "events",
                    DataRoleDirection::Output,
                    "pamguard.operator-event",
                    {
                        "events",
                        "annotations",
                    },
                },
            },
        },
    };
    const auto mismatch =
        available_contract_registry.validate_against(
            legacy_generic_contract);
    require(
        std::any_of(
            mismatch.issues.begin(),
            mismatch.issues.end(),
            [](const auto& issue) {
                return issue.code == "missing-low-level-port";
            }),
        "Aural Listening silently accepted GraphOperatorEvent runtime");
    require(
        registry.validate_against(legacy_generic_contract).valid(),
        "Unavailable Aural Listening foundation was incorrectly bound "
        "to the pre-existing generic runtime");
}

} // namespace

int main() {
    try {
        const auto descriptor =
            pamguard::project::
                make_aural_listening_controlled_unit_descriptor();
        check_descriptor(descriptor);
        check_settings();
        check_distinct_events();
        check_registry_boundary(descriptor);
        std::cout
            << "Aural Listening controlled-unit descriptor, settings, "
               "and dedicated effort/Things Heard checks passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "Aural Listening controlled-unit check failed: "
            << error.what() << '\n';
        return 1;
    }
}
