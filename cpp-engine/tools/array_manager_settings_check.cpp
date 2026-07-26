#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include <json.hpp>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ControlledUnitJson.h"
#include "pamguard/project/GlobalSettingsAdapters.h"
#include "pamguard/project/ProjectJson.h"
#include "pamguard/project/ProjectProjection.h"

namespace {

using Json = nlohmann::json;
using namespace pamguard::project;

constexpr const char* kProjectId =
    "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callback>
void require_adapter_error(
    Callback&& callback,
    const std::string& message) {
    try {
        callback();
    }
    catch (const GlobalSettingsAdapterError&) {
        return;
    }
    throw std::runtime_error(message);
}

bool has_issue(
    const ProjectProjectionResult& projection,
    const std::string& code) {
    for (const auto& issue : projection.issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

ProjectDocument project_with_array(
    const GlobalSettingsDescriptor& descriptor) {
    ProjectDocument project;
    project.project_id = kProjectId;
    project.metadata = {
        "Array geometry",
        "Global Array Manager contract fixture",
    };
    project.descriptor_set = {
        std::string(kControlledUnitDescriptorSetId),
        kControlledUnitDescriptorSetVersion,
    };
    project.global_settings.components.push_back({
        descriptor.id,
        descriptor.settings.version,
        descriptor.settings.default_settings_json,
    });
    return project;
}

} // namespace

int main() {
    try {
        ControlledUnitRegistry controlled;
        register_builtin_controlled_units(controlled);
        const auto* descriptor = controlled.find_global_settings(
            std::string(kArrayManagerGlobalSettingsTypeId));
        require(
            descriptor &&
                descriptor->required &&
                descriptor->settings.version ==
                    kArrayManagerSettingsVersion &&
                descriptor->adapter_id ==
                    kArrayManagerSettingsAdapterId &&
                descriptor->java_authority.class_name ==
                    "Array.ArrayManager",
            "Array Manager global descriptor identity changed");

        const auto defaults =
            array_manager_settings_to_geometry(
                descriptor->settings.default_settings_json,
                descriptor->settings.version);
        require(
            defaults.id == "Basic Linear Array" &&
                defaults.speed_of_sound_mps == 1500.0 &&
                defaults.speed_of_sound_error_mps == 0.0 &&
                defaults.streamers.size() == 1 &&
                defaults.streamers[0].id == 0 &&
                defaults.streamers[0].x_m == 0.0 &&
                defaults.streamers[0].y_m == 0.0 &&
                defaults.streamers[0].z_m == 0.0 &&
                defaults.streamers[0].x_error_m == 0.1 &&
                defaults.streamers[0].y_error_m == 0.1 &&
                defaults.streamers[0].z_error_m == 0.1 &&
                defaults.hydrophones.size() == 2,
            "Java Basic Linear Array root/streamer defaults changed");
        require(
            defaults.hydrophones[0].channel == 0 &&
                defaults.hydrophones[0].x_m == 0.0 &&
                defaults.hydrophones[0].y_m == 0.0 &&
                defaults.hydrophones[0].z_m == -5.0 &&
                defaults.hydrophones[0].sensitivity_db == -170.0 &&
                defaults.hydrophones[0].preamp_gain_db == 0.0 &&
                defaults.hydrophones[1].channel == 1 &&
                defaults.hydrophones[1].x_m == 0.0 &&
                defaults.hydrophones[1].y_m == -3.0 &&
                defaults.hydrophones[1].z_m == -5.0,
            "PamArray.createSimpleArray geometry defaults changed");

        auto project = project_with_array(*descriptor);
        const auto encoded =
            project_document_to_canonical_json(project);
        const auto decoded =
            project_document_from_json(encoded);
        const auto decoded_again =
            project_document_from_json(
                project_document_to_canonical_json(decoded));
        require(
            decoded_again == decoded &&
                project_document_to_canonical_json(decoded) ==
                    encoded &&
                Json::parse(
                    decoded.global_settings.components[0]
                        .settings_json) ==
                    Json::parse(
                        descriptor->settings
                            .default_settings_json),
            "Array Manager project JSON did not round-trip canonically");

        pamguard::core::ModuleRegistry runtime;
        pamguard::core::register_builtin_module_types(runtime);
        const auto projected =
            project_document_to_runtime_graph(
                decoded,
                controlled,
                runtime);
        require(
            projected.runnable() &&
                projected.array_geometry.has_value() &&
                projected.array_geometry->hydrophones.size() == 2 &&
                projected.graph.modules.empty(),
            "Blank project did not project one runnable typed array geometry");

        auto missing_array = project;
        missing_array.global_settings.components.clear();
        const auto missing_projection =
            project_document_to_runtime_graph(
                missing_array,
                controlled,
                runtime);
        require(
            !missing_projection.editor_valid() &&
                has_issue(
                    missing_projection,
                    "missing-required-global-settings"),
            "Projection accepted a project without mandatory Array Manager settings");

        auto mutated = Json::parse(
            descriptor->settings.default_settings_json);
        mutated["speedOfSoundMps"] = 1482.5;
        mutated["hydrophones"][1]["yM"] = -7.25;
        const auto changed =
            array_manager_settings_to_geometry(
                mutated.dump(),
                kArrayManagerSettingsVersion);
        require(
            changed.speed_of_sound_mps == 1482.5 &&
                changed.hydrophones[1].y_m == -7.25,
            "Array Manager adapter did not project submitted geometry");

        require_adapter_error(
            [&] {
                (void)array_manager_settings_to_geometry(
                    descriptor->settings.default_settings_json,
                    2);
            },
            "Array Manager adapter accepted an unknown version");

        auto unknown = mutated;
        unknown["unexpected"] = true;
        require_adapter_error(
            [&] {
                (void)array_manager_settings_to_geometry(
                    unknown.dump(),
                    1);
            },
            "Array Manager adapter accepted an unknown root field");

        auto noncontiguous = mutated;
        noncontiguous["hydrophones"][1]["channel"] = 2;
        require_adapter_error(
            [&] {
                (void)array_manager_settings_to_geometry(
                    noncontiguous.dump(),
                    1);
            },
            "Array Manager adapter accepted a non-contiguous channel ID");

        auto missing_streamer = mutated;
        missing_streamer["hydrophones"][0]["streamerId"] = 1;
        require_adapter_error(
            [&] {
                (void)array_manager_settings_to_geometry(
                    missing_streamer.dump(),
                    1);
            },
            "Array Manager adapter accepted a missing streamer reference");

        auto negative_error = mutated;
        negative_error["hydrophones"][0]["xErrorM"] = -0.1;
        require_adapter_error(
            [&] {
                (void)array_manager_settings_to_geometry(
                    negative_error.dump(),
                    1);
            },
            "Array Manager adapter accepted a negative coordinate error");

        require_adapter_error(
            [&] {
                (void)array_manager_settings_to_geometry(
                    R"({"arrayName":"a","arrayName":"b"})",
                    1);
            },
            "Array Manager adapter accepted duplicate JSON keys");

        std::cout
            << "Array Manager settings v1 validated Java Basic Linear "
               "Array defaults, canonical project round-trip, and typed "
               "localisation geometry\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "Array Manager settings check failed: "
            << error.what() << "\n";
        return 1;
    }
}
