#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include <json.hpp>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ProjectProjection.h"
#include "pamguard/project/SoundOutputControlledUnit.h"

namespace {

using Json = nlohmann::json;
using namespace pamguard::project;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ControlledUnitInstance make_unit(
    const ControlledUnitDescriptor& descriptor,
    std::string id,
    std::string name) {
    return {
        std::move(id),
        descriptor.id,
        descriptor.descriptor_version,
        {
            descriptor.runtime_recipe.id,
            descriptor.runtime_recipe.version,
        },
        std::move(name),
        descriptor.settings.version,
        descriptor.settings.default_settings_json,
        {},
    };
}

bool has_issue(
    const ProjectProjectionResult& projection,
    const std::string& code,
    ProjectionIssueClass issue_class) {
    return std::any_of(
        projection.issues.begin(),
        projection.issues.end(),
        [&](const auto& issue) {
            return issue.code == code &&
                issue.issue_class == issue_class;
        });
}

template <typename Callback>
void require_settings_error(
    Callback&& callback,
    const std::string& message) {
    try {
        callback();
    }
    catch (const SoundOutputSettingsError&) {
        return;
    }
    throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        const auto descriptor =
            make_sound_output_controlled_unit_descriptor();
        require(
            descriptor.id == "pamguard.sound-output" &&
                descriptor.java_authority.class_name ==
                    "soundPlayback.PlaybackControl" &&
                descriptor.settings.version == 1 &&
                descriptor.settings.whole_tree_change_policy ==
                    SettingsChangePolicy::LiveSafe &&
                descriptor.public_roles.size() == 1 &&
                descriptor.public_roles[0].id == "audio" &&
                descriptor.public_roles[0].cardinality ==
                    RoleCardinality::ExactlyOne &&
                !descriptor.public_roles[0]
                    .default_provider_controlled_unit_type_id &&
                descriptor.runtime_recipe.children.size() == 1 &&
                descriptor.runtime_recipe.children[0].runtime_type_id ==
                    "pamguard.sound-output",
            "Sound Output controlled-unit ownership contract changed");

        const auto settings = sound_output_settings_from_json(
            descriptor.settings.default_settings_json,
            descriptor.settings.version);
        require(
            settings.channel_bitmap == 0 &&
                settings.default_sample_rate &&
                settings.playback_rate_hz == 48000.0 &&
                settings.playback_speed == 1.0 &&
                settings.playback_gain_db == 0.0 &&
                settings.hp_filter == 0.0,
            "PlaybackParameters Java defaults changed");

        const auto encoded =
            Json::parse(descriptor.settings.default_settings_json);
        require(
            !encoded.contains("deviceId") &&
                !encoded.contains("deviceNumber") &&
                !encoded.contains("deviceType"),
            "Host-specific output device leaked into portable settings");

        auto configured = encoded;
        configured["channelBitmap"] = 3;
        configured["defaultSampleRate"] = false;
        configured["playbackRateHz"] = 96000;
        configured["playbackSpeed"] = 0.5;
        configured["playbackGainDb"] = 12;
        configured["hpFilter"] = 0.125;
        const auto decoded = sound_output_settings_from_json(
            configured.dump(),
            1);
        require(
            decoded.channel_bitmap == 3 &&
                !decoded.default_sample_rate &&
                decoded.playback_rate_hz == 96000.0 &&
                decoded.playback_speed == 0.5 &&
                decoded.playback_gain_db == 12.0 &&
                decoded.hp_filter == 0.125,
            "Sound Output settings did not round-trip");

        auto host_leak = configured;
        host_leak["deviceId"] = "browser-device";
        require_settings_error(
            [&] {
                (void)sound_output_settings_from_json(
                    host_leak.dump(),
                    1);
            },
            "Sound Output accepted a host device in portable settings");
        auto invalid_bitmap = configured;
        invalid_bitmap["channelBitmap"] = -1;
        require_settings_error(
            [&] {
                (void)sound_output_settings_from_json(
                    invalid_bitmap.dump(),
                    1);
            },
            "Sound Output accepted a negative channel bitmap");
        auto invalid_filter = configured;
        invalid_filter["hpFilter"] = 0.5001;
        require_settings_error(
            [&] {
                (void)sound_output_settings_from_json(
                    invalid_filter.dump(),
                    1);
            },
            "Sound Output accepted a high-pass fraction above Nyquist");
        require_settings_error(
            [&] {
                (void)sound_output_settings_from_json(
                    configured.dump(),
                    2);
            },
            "Sound Output accepted an unsupported settings version");

        ControlledUnitRegistry controlled;
        register_builtin_controlled_units(controlled);
        const auto* registered =
            controlled.find_controlled_unit("pamguard.sound-output");
        const auto* acquisition =
            controlled.find_controlled_unit("pamguard.acquisition");
        const auto* array_manager =
            controlled.find_global_settings("pamguard.array-manager");
        require(
            registered && acquisition && array_manager,
            "Sound Output was not registered with its project dependencies");

        pamguard::core::ModuleRegistry runtime;
        pamguard::core::register_builtin_module_types(runtime);
        ProjectDocument project;
        project.project_id =
            "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
        project.metadata = {
            "Sound Output projection",
            "Portable playback settings contract",
        };
        project.mode = ProjectMode::Normal;
        project.descriptor_set = {
            "pamguard-2.02.18e",
            1,
        };
        project.global_settings.components.push_back({
            array_manager->id,
            array_manager->settings.version,
            array_manager->settings.default_settings_json,
        });
        auto acquisition_unit = make_unit(
            *acquisition,
            "11111111-1111-4111-8111-111111111111",
            "Sound Acquisition");
        auto sound_output = make_unit(
            *registered,
            "22222222-2222-4222-8222-222222222222",
            "Sound Output");
        sound_output.bindings.push_back({
            "audio",
            {{
                acquisition_unit.id,
                "rawAudio",
            }},
        });
        project.controlled_units.push_back(
            std::move(acquisition_unit));
        project.controlled_units.push_back(
            std::move(sound_output));

        const auto unconfigured =
            project_document_to_runtime_graph(
                project,
                controlled,
                runtime);
        require(
            unconfigured.editor_valid() &&
                unconfigured.needs_configuration() &&
                !unconfigured.runnable() &&
                has_issue(
                    unconfigured,
                    "sound-output-no-channels",
                    ProjectionIssueClass::NeedsConfiguration) &&
                unconfigured.graph.modules.size() == 2 &&
                unconfigured.graph.connections.size() == 1,
            "Java channelBitmap=0 was not retained as a saveable "
            "NeedsConfiguration project");

        auto selected_channels = Json::parse(
            project.controlled_units[1].settings_json);
        selected_channels["channelBitmap"] = 3;
        project.controlled_units[1].settings_json =
            selected_channels.dump();
        const auto runnable =
            project_document_to_runtime_graph(
                project,
                controlled,
                runtime);
        require(
            runnable.runnable() &&
                !has_issue(
                    runnable,
                    "sound-output-no-channels",
                    ProjectionIssueClass::NeedsConfiguration),
            "Configured Sound Output did not produce a runnable project");

        auto missing_source = project;
        missing_source.controlled_units[1].bindings.clear();
        const auto missing_source_projection =
            project_document_to_runtime_graph(
                missing_source,
                controlled,
                runtime);
        require(
            missing_source_projection.editor_valid() &&
                missing_source_projection.needs_configuration() &&
                has_issue(
                    missing_source_projection,
                    "missing-required-binding",
                    ProjectionIssueClass::NeedsConfiguration),
            "Sound Output without a raw-audio binding was not a saveable "
            "NeedsConfiguration project");

        auto leaked_host = project;
        auto leaked_settings = Json::parse(
            leaked_host.controlled_units[1].settings_json);
        leaked_settings["deviceId"] = "browser-device";
        leaked_host.controlled_units[1].settings_json =
            leaked_settings.dump();
        const auto leaked_projection =
            project_document_to_runtime_graph(
                leaked_host,
                controlled,
                runtime);
        require(
            !leaked_projection.editor_valid() &&
                has_issue(
                    leaked_projection,
                    "invalid-settings",
                    ProjectionIssueClass::EditorInvalid),
            "Project projection accepted a host output device");

        std::cout
            << "Sound Output settings validated PlaybackParameters "
               "defaults, portable/host separation, strict ranges, and "
               "NeedsConfiguration projection\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "Sound Output settings check failed: "
            << error.what() << "\n";
        return 1;
    }
}
