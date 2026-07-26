#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <json.hpp>

#include "pamguard/project/ControlledUnitRegistry.h"
#include "pamguard/project/UserInputControlledUnit.h"

namespace {

using Json = nlohmann::json;
using pamguard::project::AvailabilityStatus;
using pamguard::project::ControlledUnitDescriptor;
using pamguard::project::ControlledUnitRegistry;
using pamguard::project::DataRoleDirection;
using pamguard::project::LowLevelPortContract;
using pamguard::project::LowLevelTypeContract;
using pamguard::project::RunMode;
using pamguard::project::SettingsChangePolicy;
using pamguard::project::UserInputProjectAdapterError;

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
        "Missing User Input surface: " + surface);
    return *found;
}

void require_adapter_error(
    std::string_view expected_code,
    const auto& action) {
    bool rejected = false;
    try {
        action();
    }
    catch (const UserInputProjectAdapterError& error) {
        rejected = error.code() == expected_code;
    }
    require(
        rejected,
        "User Input adapter did not reject with code " +
            std::string(expected_code));
}

void check_descriptor(
    const ControlledUnitDescriptor& descriptor) {
    require(
        descriptor.id == "pamguard.user-input" &&
            descriptor.descriptor_version == 1 &&
            descriptor.java_authority.registered_name ==
                "User input" &&
            descriptor.java_authority.menu_group == "Utilities" &&
            descriptor.java_authority.class_name ==
                "UserInput.UserInputController" &&
            descriptor.java_authority.relationship == "direct" &&
            descriptor.java_authority.tooltip ==
                "Creates a form for the user to type comments into" &&
            descriptor.java_authority.help_point ==
                "utilities/userInputHelp/docs/userInput.html",
        "User Input pinned Java authority identity changed");
    require(
        descriptor.instance_rules.minimum_instances == 0 &&
            descriptor.instance_rules.maximum_instances ==
                std::optional<std::size_t>{1} &&
            descriptor.instance_rules.allowed_modes ==
                std::vector<RunMode>{
                    RunMode::Normal,
                    RunMode::Mixed,
                    RunMode::Viewer,
                } &&
            descriptor.instance_rules.mode_overrides.empty(),
        "User Input pinned Java multiplicity or run modes changed");

    require(
        descriptor.public_roles.size() == 1,
        "User Input must expose exactly one Java data block");
    const auto& output = descriptor.public_roles.front();
    require(
        output.id == "entries" &&
            output.name == "User Input Data" &&
            output.direction == DataRoleDirection::Output &&
            output.data_type == "pamguard.user-input-data" &&
            output.capabilities ==
                std::vector<std::string>{
                    "events",
                    "annotations",
                },
        "User Input public data-block contract changed");
    require(
        output.data_type != "pamguard.operator-event",
        "User Input must not claim parity through GraphOperatorEvent");

    const auto schema =
        Json::parse(descriptor.settings.settings_schema_json);
    require(
        descriptor.settings.version == 1 &&
            descriptor.settings.authority_classes ==
                std::vector<std::string>{
                    "UserInput.UserInputController",
                } &&
            Json::parse(
                descriptor.settings.default_settings_json) ==
                Json::object() &&
            schema.at("type") == "object" &&
            schema.at("additionalProperties") == false &&
            schema.at("maxProperties") == 0 &&
            descriptor.settings.defaults.empty() &&
            descriptor.settings.whole_tree_change_policy ==
                SettingsChangePolicy::LiveSafe &&
            descriptor.settings.parity_status ==
                "java-source-validated-no-settings",
        "User Input must retain Java's no-settings contract");
    require(
        section(
            descriptor,
            "operator.form.sections").labels ==
                std::vector<std::string>{
                    "Enter Comment",
                    "Entries:",
                } &&
            section(
                descriptor,
                "operator.form.actions").labels ==
                std::vector<std::string>{
                    "Submit comment",
                    "Clear comment",
                },
        "User Input Java form labels or order changed");

    require(
        descriptor.runtime_recipe.id ==
                "pamguard.user-input.runtime" &&
            descriptor.runtime_recipe.children.size() == 1 &&
            descriptor.runtime_recipe.public_role_mappings.size() ==
                1 &&
            descriptor.runtime_recipe.children.front().role_id ==
                "user-input" &&
            descriptor.runtime_recipe.children.front().
                runtime_type_id == "pamguard.user-input" &&
            descriptor.runtime_recipe.children.front().
                settings.adapter_id ==
                "pamguard.user-input-settings.v1" &&
            descriptor.runtime_recipe.children.front().
                availability == AvailabilityStatus::Unavailable &&
            descriptor.runtime_recipe.public_role_mappings.front().
                runtime_endpoint.port_id == "events" &&
            descriptor.availability ==
                AvailabilityStatus::Unavailable &&
            descriptor.parity_status ==
                "experimental",
        "User Input runtime claim boundary changed");
}

void check_project_adapter() {
    const auto runtime = Json::parse(
        pamguard::project::user_input_runtime_settings_json(
            "{}",
            1));
    require(
        runtime ==
            Json{
                {
                    "channelBitmap",
                    pamguard::project::
                        kUserInputChannelBitmap,
                },
                {
                    "maximumCommentUtf16CodeUnits",
                    pamguard::project::
                        kUserInputMaximumCommentUtf16CodeUnits,
                },
                {
                    "naturalLifetimeSeconds",
                    pamguard::project::
                        kUserInputNaturalLifetimeSeconds,
                },
                {
                    "requiredHistoryMilliseconds",
                    pamguard::project::
                        kUserInputRequiredHistoryMilliseconds,
                },
            },
        "User Input fixed Java process constants changed");

    const auto entry =
        pamguard::project::
            user_input_data_entry_from_submit_action_json(
                R"({"comment":"  visual contact - north  "})",
                1'722'000'123'456LL);
    require(
        entry.time_milliseconds == 1'722'000'123'456LL &&
            entry.user_string ==
                "  visual contact - north  " &&
            entry.channel_bitmap == 0xFFFFu,
        "User Input submit adapter altered Java timestamp/comment data");
    require(
        Json::parse(
            pamguard::project::user_input_data_entry_to_json(
                entry)) ==
            Json{
                {"channelBitmap", 65'535},
                {
                    "timeMilliseconds",
                    1'722'000'123'456LL,
                },
                {
                    "userString",
                    "  visual contact - north  ",
                },
            },
        "User Input dedicated data JSON changed");

    require_adapter_error(
        "unsupported-user-input-settings-version",
        [] {
            static_cast<void>(
                pamguard::project::
                    user_input_runtime_settings_json("{}", 2));
        });
    require_adapter_error(
        "invalid-user-input-settings",
        [] {
            static_cast<void>(
                pamguard::project::
                    user_input_runtime_settings_json(
                        R"({"defaultCategory":"annotation"})",
                        1));
        });
    require_adapter_error(
        "invalid-user-input-submit-action",
        [] {
            static_cast<void>(
                pamguard::project::
                    user_input_data_entry_from_submit_action_json(
                        R"({"comment":"x","category":"annotation"})",
                        1));
        });
    require_adapter_error(
        "empty-user-input-comment",
        [] {
            static_cast<void>(
                pamguard::project::
                    user_input_data_entry_from_submit_action_json(
                        R"({"comment":""})",
                        1));
        });

    // Java String.length() counts a supplementary scalar as two UTF-16 code
    // units. This boundary case proves the adapter uses that authority rather
    // than byte or Unicode-scalar count.
    std::string boundary(
        pamguard::project::
            kUserInputMaximumCommentUtf16CodeUnits - 1,
        'a');
    boundary += "\xF0\x9F\x90\x8B"; // U+1F40B WHALE, two UTF-16 units.
    require_adapter_error(
        "user-input-comment-too-long",
        [&] {
            const auto action =
                Json{{"comment", boundary}}.dump();
            static_cast<void>(
                pamguard::project::
                    user_input_data_entry_from_submit_action_json(
                        action,
                        1));
        });
}

void check_registry_boundary(
    const ControlledUnitDescriptor& descriptor) {
    ControlledUnitRegistry registry;
    registry.register_controlled_unit(descriptor);
    require(
        registry.validate().valid(),
        "User Input descriptor failed structural registry validation");

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
            "pamguard.user-input",
            {
                {
                    "events",
                    DataRoleDirection::Output,
                    "pamguard.user-input-data",
                    {
                        "events",
                        "annotations",
                    },
                },
            },
        },
    };
    require(
        available_contract_registry
            .validate_against(dedicated_contract)
            .valid(),
        "User Input descriptor rejected its dedicated runtime contract");

    const std::vector<LowLevelTypeContract> legacy_generic_contract{
        {
            "pamguard.user-input",
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
                return issue.code ==
                    "low-level-data-type-mismatch";
            }),
        "User Input descriptor silently accepted GraphOperatorEvent");
    require(
        registry.validate_against(legacy_generic_contract).valid(),
        "Unavailable User Input foundation was incorrectly bound to the "
        "pre-existing generic runtime");
}

} // namespace

int main() {
    try {
        const auto descriptor =
            pamguard::project::
                make_user_input_controlled_unit_descriptor();
        check_descriptor(descriptor);
        check_project_adapter();
        check_registry_boundary(descriptor);
        std::cout
            << "User Input controlled-unit descriptor and exact "
               "timestamp/comment adapter checks passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "User Input controlled-unit check failed: "
            << error.what() << '\n';
        return 1;
    }
}
