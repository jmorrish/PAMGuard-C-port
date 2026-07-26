#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json.hpp>

#include "pamguard/core/SoundRecorderSettings.h"
#include "pamguard/project/ControlledUnitRegistry.h"
#include "pamguard/project/SoundRecorderControlledUnit.h"

namespace {

using Json = nlohmann::json;
using pamguard::project::ControlledUnitDescriptor;
using pamguard::project::ControlledUnitRegistry;
using pamguard::project::DataRoleDirection;
using pamguard::project::LowLevelPortContract;
using pamguard::project::LowLevelTypeContract;
using pamguard::project::RunMode;
using pamguard::project::SoundRecorderDeploymentBinding;
using pamguard::project::SoundRecorderRuntimeSettingsError;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Json read_fixture(const std::string& path) {
    std::ifstream input(path);
    require(
        static_cast<bool>(input),
        "Could not open Sound Recorder fixture: " + path);
    return Json::parse(input);
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
        "Missing Sound Recorder settings surface: " + surface);
    return *found;
}

void check_descriptor(
    const ControlledUnitDescriptor& descriptor,
    const Json& fixture) {
    require(
        descriptor.id == "pamguard.sound-recorder" &&
            descriptor.descriptor_version == 1 &&
            descriptor.java_authority.registered_name ==
                "Sound recorder" &&
            descriptor.java_authority.menu_group ==
                "Sound Processing" &&
            descriptor.java_authority.class_name ==
                "SoundRecorder.RecorderControl" &&
            descriptor.java_authority.relationship == "direct",
        "Sound Recorder Java authority identity changed");
    require(
        descriptor.instance_rules.minimum_instances == 0 &&
            !descriptor.instance_rules.maximum_instances &&
            descriptor.instance_rules.allowed_modes ==
                std::vector<RunMode>{
                    RunMode::Normal,
                    RunMode::Mixed,
                    RunMode::Viewer,
                },
        "Sound Recorder Java instance rules changed");

    require(
        descriptor.public_roles.size() == 2,
        "Sound Recorder must expose exactly audio and recording events");
    const auto& input = descriptor.public_roles.at(0);
    const auto& output = descriptor.public_roles.at(1);
    require(
        input.id == "rawAudio" &&
            input.direction == DataRoleDirection::Input &&
            input.data_type == "pamguard.raw-audio" &&
            input.java_data_class ==
                "PamDetection.RawDataUnit" &&
            input.default_provider_controlled_unit_type_id ==
                std::optional<std::string>{
                    "pamguard.acquisition"} &&
            output.id == "recordingEvents" &&
            output.direction == DataRoleDirection::Output &&
            output.data_type == "pamguard.recording-event",
        "Sound Recorder public role contract changed");
    require(
        std::none_of(
            descriptor.public_roles.begin(),
            descriptor.public_roles.end(),
            [](const auto& role) {
                return role.id.find("trigger") !=
                    std::string::npos;
            }),
        "Sound Recorder exposed triggers before receiver-owned wiring");

    const auto portable_default = Json::parse(
        descriptor.settings.default_settings_json);
    require(
        portable_default ==
            fixture.at("portableSettingsDefaults") &&
            descriptor.settings.version == 1 &&
            descriptor.settings.parity_status ==
                "java-fixture-validated" &&
            !portable_default.contains("rawDataSource") &&
            !portable_default.contains("outputFolder") &&
            portable_default.at("operationMode") == "idle",
        "Sound Recorder portable defaults or omission boundary changed");
    require(
        descriptor.settings.settings_schema_json ==
            pamguard::core::
                sound_recorder_settings_schema_json(),
        "Sound Recorder descriptor did not reuse the canonical schema");

    require(
        section(descriptor, "settings.tabs").labels ==
            std::vector<std::string>{
                "Control",
                "Files and Folders",
                "Triggered Recordings",
            } &&
            section(
                descriptor,
                "settings.control.sections").labels ==
                std::vector<std::string>{
                    "Raw data source",
                    "Recorder controls",
                    "PAMGuard Startup Options",
                    "Audio buffer",
                    "Automatic recordings duty cycle settings",
                } &&
            section(descriptor, "runtime.actions").labels ==
                std::vector<std::string>{
                    "Off",
                    "Continuous",
                } &&
            section(
                descriptor,
                "settings.files-and-folders.sections").labels ==
                std::vector<std::string>{
                    "Output file location, names and format",
                    "Maximum file lengths",
                },
        "Sound Recorder editor/Java section or action order changed");

    require(
        descriptor.runtime_recipe.id ==
                "pamguard.sound-recorder.runtime" &&
            descriptor.runtime_recipe.children.size() == 1 &&
            descriptor.runtime_recipe.public_role_mappings.size() ==
                2,
        "Sound Recorder runtime recipe shape changed");
    const auto& child =
        descriptor.runtime_recipe.children.front();
    require(
        child.role_id == "recorder-process" &&
            child.runtime_type_id ==
                "pamguard.sound-recorder" &&
            child.settings.source_pointer.empty() &&
            child.settings.adapter_id ==
                pamguard::project::
                    kSoundRecorderRuntimeSettingsAdapterId,
        "Sound Recorder runtime child or adapter changed");
}

void check_registry_compatibility(
    ControlledUnitDescriptor descriptor) {
    // The isolated checker has no Acquisition descriptor. Its exact default
    // provider identity was asserted above; clear only that cross-registry
    // reference before validating this recipe's own runtime port contract.
    descriptor.public_roles.at(0)
        .default_provider_controlled_unit_type_id.reset();
    ControlledUnitRegistry registry;
    registry.register_controlled_unit(
        std::move(descriptor));
    const auto validation = registry.validate_against(
        std::vector<LowLevelTypeContract>{
            {
                "pamguard.sound-recorder",
                {
                    {
                        "input",
                        DataRoleDirection::Input,
                        "pamguard.raw-audio",
                        {"sampled"},
                    },
                    {
                        "recordings",
                        DataRoleDirection::Output,
                        "pamguard.recording-event",
                        {
                            "events",
                            "recordings",
                        },
                    },
                },
            },
        });
    std::string evidence;
    for (const auto& issue : validation.issues) {
        evidence +=
            " [" + issue.code + "] " + issue.message;
    }
    require(
        validation.valid(),
        "Sound Recorder recipe is incompatible with its low-level "
        "ports:" + evidence);
}

void check_runtime_adapter(
    const ControlledUnitDescriptor& descriptor) {
    bool rejected_missing_binding = false;
    try {
        (void) pamguard::project::
            sound_recorder_runtime_settings_json(
                descriptor.settings.default_settings_json,
                1,
                {});
    }
    catch (const SoundRecorderRuntimeSettingsError&) {
        rejected_missing_binding = true;
    }
    require(
        rejected_missing_binding,
        "Sound Recorder invented a fallback output folder");

    const auto runtime = Json::parse(
        pamguard::project::
            sound_recorder_runtime_settings_json(
                descriptor.settings.default_settings_json,
                1,
                SoundRecorderDeploymentBinding{
                    R"(D:\PAMDeploy\Recordings)",
                }));
    require(
        runtime.size() == 3 &&
            runtime.at("directory") ==
                R"(D:\PAMDeploy\Recordings)" &&
            runtime.at("startTransport") == "off" &&
            runtime.at("settings") ==
                Json::parse(
                    descriptor.settings.default_settings_json) &&
            !runtime.contains("filePrefix") &&
            !runtime.contains("segmentSeconds"),
        "Sound Recorder adapter lost the deployment boundary, safe "
        "start, or canonical settings ownership");

    const std::vector<std::string> invalid_initials = {
        "bad<name",
        "bad>name",
        "bad:name",
        "bad\"name",
        "bad/name",
        R"(bad\name)",
        "bad|name",
        "bad?name",
        "bad*name",
        std::string{"bad\nname"},
        std::string{"bad"} + char{0x7F} + "name",
        std::string{"bad\0name", 8},
    };
    for (const auto& initials : invalid_initials) {
        auto invalid = Json::parse(
            descriptor.settings.default_settings_json);
        invalid["fileInitials"] = initials;
        bool rejected = false;
        try {
            (void) pamguard::project::
                sound_recorder_runtime_settings_json(
                    invalid.dump(),
                    1,
                    SoundRecorderDeploymentBinding{
                        R"(D:\PAMDeploy\Recordings)",
                    });
        }
        catch (const SoundRecorderRuntimeSettingsError&) {
            rejected = true;
        }
        require(
            rejected,
            "Sound Recorder accepted host-invalid fileInitials");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(
            argc == 2,
            "Usage: sound_recorder_controlled_unit_check "
            "<settings-defaults.json>");
        const auto fixture = read_fixture(argv[1]);
        const auto descriptor =
            pamguard::project::
                make_sound_recorder_controlled_unit_descriptor();
        check_descriptor(descriptor, fixture);
        check_registry_compatibility(descriptor);
        check_runtime_adapter(descriptor);
        std::cout
            << "Sound Recorder controlled-unit descriptor, "
               "deployment boundary, and safe-idle adapter checks "
               "passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
