#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <json.hpp>

#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/ModuleGraph.h"
#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ClickDetectorControlledUnit.h"
#include "pamguard/project/ControlledUnitRegistry.h"
#include "pamguard/project/ProjectAuthority.h"
#include "pamguard/project/ProjectIdentity.h"

namespace {

using json = nlohmann::json;
using pamguard::project::AvailabilityStatus;
using pamguard::project::ControlledUnitDescriptor;
using pamguard::project::ControlledUnitRegistry;
using pamguard::project::DataRoleDirection;
using pamguard::project::DisplayProviderDescriptor;
using pamguard::project::LowLevelPortContract;
using pamguard::project::LowLevelTypeContract;
using pamguard::project::RoleCardinality;
using pamguard::project::RunMode;
using pamguard::project::SettingDefaultDescriptor;
using pamguard::project::SettingsChangePolicy;
using pamguard::project::SettingsSectionDescriptor;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string required_string(
    const json& value,
    const char* key,
    const std::string& context) {
    require(
        value.contains(key) && value.at(key).is_string() &&
            !value.at(key).get_ref<const std::string&>().empty(),
        context + " must contain a non-empty string '" + key + "'");
    return value.at(key).get<std::string>();
}

std::vector<std::string> required_string_array(
    const json& value,
    const char* key,
    const std::string& context,
    bool allow_empty = false) {
    require(
        value.contains(key) && value.at(key).is_array() &&
            (allow_empty || !value.at(key).empty()),
        context + " must contain an array '" + key + "'");
    std::vector<std::string> result;
    for (const auto& entry : value.at(key)) {
        require(
            entry.is_string() &&
                !entry.get_ref<const std::string&>().empty(),
            context + "." + key + " must contain non-empty strings");
        result.push_back(entry.get<std::string>());
    }
    require(
        std::set<std::string>(result.begin(), result.end()).size() ==
            result.size(),
        context + "." + key + " must not contain duplicates");
    return result;
}

const json& find_by_id(
    const json& array,
    const char* key,
    const std::string& id,
    const std::string& context) {
    require(array.is_array(), context + " must be an array");
    const json* match = nullptr;
    for (const auto& entry : array) {
        if (entry.value(key, std::string{}) != id) {
            continue;
        }
        require(!match, context + " contains duplicate ID '" + id + "'");
        match = &entry;
    }
    require(match, context + " does not contain ID '" + id + "'");
    return *match;
}

std::optional<std::size_t> manifest_maximum(
    const json& rules,
    const std::string& context) {
    require(
        rules.contains("maximumInstances"),
        context + " omits maximumInstances");
    if (rules.at("maximumInstances").is_null()) {
        return std::nullopt;
    }
    require(
        rules.at("maximumInstances").is_number_unsigned() ||
            (rules.at("maximumInstances").is_number_integer() &&
             rules.at("maximumInstances").get<std::int64_t>() >= 0),
        context + ".maximumInstances must be null or non-negative");
    return rules.at("maximumInstances").get<std::size_t>();
}

std::vector<std::string> mode_names(
    const std::vector<RunMode>& modes) {
    std::vector<std::string> result;
    result.reserve(modes.size());
    for (const auto mode : modes) {
        result.emplace_back(pamguard::project::to_string(mode));
    }
    return result;
}

std::vector<LowLevelTypeContract> low_level_catalogue() {
    pamguard::core::ModuleRegistry registry;
    pamguard::core::register_builtin_module_types(registry);
    std::vector<LowLevelTypeContract> result;
    for (const auto& type : registry.list()) {
        LowLevelTypeContract converted;
        converted.id = type.id;
        for (const auto& port : type.ports) {
            converted.ports.push_back({
                port.id,
                port.direction == pamguard::core::PortDirection::Input
                    ? DataRoleDirection::Input
                    : DataRoleDirection::Output,
                port.data_type,
                port.capabilities,
            });
        }
        result.push_back(std::move(converted));
    }
    return result;
}

bool has_issue(
    const pamguard::project::ControlledUnitRegistryValidation& validation,
    const std::string& code,
    const std::string& descriptor_id = {}) {
    return std::any_of(
        validation.issues.begin(),
        validation.issues.end(),
        [&](const auto& issue) {
            return issue.code == code &&
                (descriptor_id.empty() ||
                 issue.descriptor_id == descriptor_id);
        });
}

void require_clean_validation(
    const pamguard::project::ControlledUnitRegistryValidation& validation,
    const std::string& context) {
    if (validation.valid()) {
        return;
    }
    std::string message = context + ":";
    for (const auto& issue : validation.issues) {
        message += " [" + issue.code + "/" + issue.descriptor_id +
            "] " + issue.message;
    }
    throw std::runtime_error(message);
}

const SettingDefaultDescriptor& find_default(
    const std::vector<SettingDefaultDescriptor>& defaults,
    const std::string& authority_path) {
    const auto found = std::find_if(
        defaults.begin(),
        defaults.end(),
        [&](const auto& entry) {
            return entry.authority_path == authority_path;
        });
    require(
        found != defaults.end(),
        "Registry omits manifest default '" + authority_path + "'");
    return *found;
}

std::vector<SettingsSectionDescriptor> combined_sections(
    const ControlledUnitDescriptor& controlled_unit,
    const ControlledUnitRegistry& registry) {
    auto result = controlled_unit.settings.sections;
    for (const auto& provider_id :
         controlled_unit.runtime_recipe.display_provider_ids) {
        const auto* provider =
            registry.find_display_provider(provider_id);
        require(
            provider,
            "Controlled-unit recipe refers to absent provider '" +
                provider_id + "'");
        result.insert(
            result.end(),
            provider->settings.sections.begin(),
            provider->settings.sections.end());
    }
    return result;
}

std::vector<SettingDefaultDescriptor> combined_defaults(
    const ControlledUnitDescriptor& controlled_unit,
    const ControlledUnitRegistry& registry) {
    auto result = controlled_unit.settings.defaults;
    for (const auto& provider_id :
         controlled_unit.runtime_recipe.display_provider_ids) {
        const auto* provider =
            registry.find_display_provider(provider_id);
        require(
            provider,
            "Controlled-unit recipe refers to absent provider '" +
                provider_id + "'");
        result.insert(
            result.end(),
            provider->settings.defaults.begin(),
            provider->settings.defaults.end());
    }
    return result;
}

std::vector<std::string> combined_settings_classes(
    const ControlledUnitDescriptor& controlled_unit,
    const ControlledUnitRegistry& registry) {
    auto result = controlled_unit.settings.authority_classes;
    for (const auto& provider_id :
         controlled_unit.runtime_recipe.display_provider_ids) {
        const auto* provider =
            registry.find_display_provider(provider_id);
        require(
            provider,
            "Controlled-unit recipe refers to absent provider '" +
                provider_id + "'");
        result.insert(
            result.end(),
            provider->settings.authority_classes.begin(),
            provider->settings.authority_classes.end());
    }
    return result;
}

void check_default_json(
    const pamguard::project::SettingsDescriptor& settings,
    const std::string& descriptor_id) {
    const auto defaults = json::parse(settings.default_settings_json);
    require(
        defaults.is_object(),
        descriptor_id + " defaults must be a JSON object");
    for (const auto& entry : settings.defaults) {
        const json::json_pointer pointer(entry.pointer);
        require(
            defaults.contains(pointer),
            descriptor_id + " canonical defaults omit " +
                entry.pointer);
        if (entry.value_json) {
            require(
                defaults.at(pointer) == json::parse(*entry.value_json),
                descriptor_id + " canonical default differs at " +
                    entry.pointer);
        }
    }
}

void check_manifest_bundle(
    const ControlledUnitDescriptor& descriptor,
    const json& document) {
    const auto& bundle = find_by_id(
        document.at("bundles"),
        "bundleId",
        descriptor.id,
        "bundles");
    const auto context = "bundle '" + descriptor.id + "'";
    require(
        required_string(bundle, "kind", context) == "controlled-unit",
        context + " kind changed");
    const auto& authority = bundle.at("javaAuthority");
    require(
        required_string(authority, "registeredName", context) ==
            descriptor.java_authority.registered_name &&
        required_string(authority, "menuGroup", context) ==
            descriptor.java_authority.menu_group &&
        required_string(authority, "className", context) ==
            descriptor.java_authority.class_name &&
        required_string(authority, "relationship", context) ==
            descriptor.java_authority.relationship,
        context + " Java authority tuple differs from the registry");
    require(
        required_string(bundle, "parityLabel", context) ==
            descriptor.parity_status,
        context + " parity label differs from the registry");
    const auto& recipe = bundle.at("runtimeRecipe");
    require(
        recipe.value("version", 0u) ==
            descriptor.runtime_recipe.version,
        context + " runtime recipe version differs");
    require(
        required_string_array(
            recipe,
            "runtimeTypeIds",
            context + ".runtimeRecipe") ==
            pamguard::project::recipe_runtime_type_ids(descriptor),
        context + " runtime recipe IDs/order differ");
}

void check_manifest_instance_and_dependencies(
    const ControlledUnitDescriptor& descriptor,
    const ControlledUnitRegistry& registry,
    const json& document) {
    const auto& contract = find_by_id(
        document.at("controlledUnitContracts"),
        "bundleId",
        descriptor.id,
        "controlledUnitContracts");
    const auto context = "controlled-unit contract '" + descriptor.id + "'";
    const auto& rules = contract.at("instanceRules");
    require(
        rules.at("minimumInstances").get<std::size_t>() ==
            descriptor.instance_rules.minimum_instances &&
        manifest_maximum(rules, context) ==
            descriptor.instance_rules.maximum_instances &&
        required_string_array(
            rules,
            "allowedModes",
            context + ".instanceRules") ==
            mode_names(descriptor.instance_rules.allowed_modes),
        context + " multiplicity/run modes differ from Java authority");
    require(
        rules.contains("modeOverrides") &&
            rules.at("modeOverrides").is_array() &&
            rules.at("modeOverrides").size() ==
                descriptor.instance_rules.mode_overrides.size(),
        context + " mode-override count differs from Java authority");
    for (std::size_t index = 0;
         index < descriptor.instance_rules.mode_overrides.size();
         ++index) {
        const auto& expected =
            rules.at("modeOverrides").at(index);
        const auto& actual =
            descriptor.instance_rules.mode_overrides[index];
        require(
            required_string(
                expected,
                "mode",
                context + ".instanceRules.modeOverride") ==
                    pamguard::project::to_string(actual.mode) &&
                expected.at("minimumInstances")
                        .get<std::size_t>() ==
                    actual.minimum_instances &&
                manifest_maximum(
                    expected,
                    context +
                        ".instanceRules.modeOverride") ==
                    actual.maximum_instances,
            context +
                " mode override differs from Java authority");
    }

    std::vector<const pamguard::project::PublicDataRoleDescriptor*>
        dependencies;
    for (const auto& role : descriptor.public_roles) {
        if (role.direction == DataRoleDirection::Input &&
            role.default_provider_controlled_unit_type_id) {
            dependencies.push_back(&role);
        }
    }
    const auto& manifest_dependencies = contract.at("dependencies");
    require(
        manifest_dependencies.is_array() &&
            manifest_dependencies.size() == dependencies.size(),
        context + " dependency count differs");
    for (std::size_t index = 0;
         index < dependencies.size();
         ++index) {
        const auto& expected = manifest_dependencies.at(index);
        const auto& role = *dependencies[index];
        const auto* provider = registry.find_controlled_unit(
            *role.default_provider_controlled_unit_type_id);
        require(provider, context + " default provider is absent");
        require(
            required_string(
                expected,
                "requiredDataClass",
                context + ".dependency") == role.java_data_class &&
            required_string(
                expected,
                "defaultProviderClass",
                context + ".dependency") ==
                provider->java_authority.class_name,
            context + " dependency differs from Java authority");
    }

    const auto& configuration = contract.at("configurationAuthority");
    require(
        required_string(
            configuration,
            "cppParityClaim",
            context + ".configurationAuthority") ==
            descriptor.settings.parity_status,
        context + " settings parity claim differs");
    require(
        required_string_array(
            configuration,
            "settingsClasses",
            context + ".configurationAuthority") ==
            combined_settings_classes(descriptor, registry),
        context + " settings authority classes differ");
}

void check_manifest_configuration(
    const ControlledUnitDescriptor& descriptor,
    const ControlledUnitRegistry& registry,
    const json& document) {
    const auto& contract = find_by_id(
        document.at("coreConfigurationContracts"),
        "bundleId",
        descriptor.id,
        "coreConfigurationContracts");
    const auto context = "core configuration '" + descriptor.id + "'";
    require(
        required_string(contract, "cppParityClaim", context) ==
            descriptor.settings.parity_status,
        context + " parity claim differs");

    const auto sections = combined_sections(descriptor, registry);
    const auto& manifest_sections = contract.at("sectionOrder");
    require(
        manifest_sections.is_array() &&
            manifest_sections.size() == sections.size(),
        context + " settings-section count differs");
    for (std::size_t index = 0; index < sections.size(); ++index) {
        const auto& expected = manifest_sections.at(index);
        require(
            required_string(expected, "surface", context) ==
                sections[index].surface &&
            required_string_array(expected, "labels", context) ==
                sections[index].labels,
            context + " settings-section order/labels differ");
    }

    const auto defaults = combined_defaults(descriptor, registry);
    const auto& manifest_defaults = contract.at("priorityDefaults");
    require(
        manifest_defaults.is_array(),
        context + " priorityDefaults must be an array");
    std::vector<const json*> portable_manifest_defaults;
    for (const auto& expected : manifest_defaults) {
        const auto path = required_string(
            expected,
            "path",
            context + ".priorityDefault");
        const bool host_or_binding_owned =
            descriptor.id == "pamguard.sound-output" &&
            (path == "dataSource" ||
             path == "deviceNumber" ||
             path == "deviceType" ||
             path == "logPlaybackSpeed");
        if (!host_or_binding_owned) {
            portable_manifest_defaults.push_back(&expected);
        }
    }
    require(
        (descriptor.id == "pamguard.click-detector"
             ? portable_manifest_defaults.size() >= defaults.size()
             : portable_manifest_defaults.size() == defaults.size()),
        context + " priority-default count differs");
    for (const auto* expected_pointer :
         portable_manifest_defaults) {
        const auto& expected = *expected_pointer;
        const auto path =
            required_string(expected, "path", context + ".priorityDefault");
        const auto actual_entry = std::find_if(
            defaults.begin(),
            defaults.end(),
            [&](const auto& entry) {
                return entry.authority_path == path;
            });
        if (actual_entry == defaults.end() &&
            descriptor.id == "pamguard.click-detector") {
            continue;
        }
        require(
            actual_entry != defaults.end(),
            context + " omits manifest default '" + path + "'");
        const auto& actual = *actual_entry;
        require(
            required_string(
                expected,
                "authority",
                context + ".priorityDefault") == actual.authority,
            context + " authority differs for default '" + path + "'");
        if (expected.contains("value")) {
            require(
                actual.value_json &&
                    json::parse(*actual.value_json) ==
                        expected.at("value") &&
                    actual.value_source.empty(),
                context + " literal differs for default '" + path + "'");
        }
        else {
            require(
                !actual.value_json &&
                    required_string(
                        expected,
                        "valueSource",
                        context + ".priorityDefault") ==
                        actual.value_source,
                context + " value source differs for default '" + path +
                    "'");
        }
        require(
            expected.value("condition", std::string{}) ==
                actual.condition,
            context + " condition differs for default '" + path + "'");
    }
    if (descriptor.id == "pamguard.click-detector") {
        for (const auto& actual : defaults) {
            require(
                std::any_of(
                    portable_manifest_defaults.begin(),
                    portable_manifest_defaults.end(),
                    [&](const auto* expected) {
                        return expected->value(
                                   "path",
                                   std::string{}) ==
                            actual.authority_path;
                    }),
                context + " has an undocumented registry default '" +
                    actual.authority_path + "'");
        }
    }
}

void check_manifest_runtime_types(
    const ControlledUnitRegistry& registry,
    const std::vector<LowLevelTypeContract>& low_level_types,
    const json& document) {
    std::unordered_map<std::string, std::string> low_level_names;
    pamguard::core::ModuleRegistry module_registry;
    pamguard::core::register_builtin_module_types(module_registry);
    for (const auto& type : module_registry.list()) {
        low_level_names.emplace(type.id, type.name);
    }

    struct ExpectedRuntime {
        std::string id;
        std::string owner;
        std::string disposition;
        std::string parity;
    };
    const std::vector<ExpectedRuntime> expected{
        {
            "pamguard.acquisition",
            "pamguard.acquisition",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.amplifier",
            "pamguard.amplifier",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.patch-panel",
            "pamguard.patch-panel",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.filter",
            "pamguard.filter",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.decimator",
            "pamguard.decimator",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.fft",
            "pamguard.fft",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.spectrogram-noise",
            "pamguard.fft",
            "hidden-adapter",
            "internal-foundation",
        },
        {
            "pamguard.fft-noise-monitor",
            "pamguard.fft-noise-monitor",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.noise-band-monitor",
            "pamguard.noise-band-monitor",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.ltsa",
            "pamguard.ltsa",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.whistles-moans",
            "pamguard.whistles-moans",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.ishmael-energy-sum",
            "pamguard.ishmael-energy-sum",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.ishmael-sgram-corr",
            "pamguard.ishmael-sgram-corr",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.ishmael-match-filter",
            "pamguard.ishmael-match-filter",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.click-detector",
            "pamguard.click-detector",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.click-classifier",
            "pamguard.click-detector",
            "hidden-adapter",
            "internal-foundation",
        },
        {
            "pamguard.click-features",
            "pamguard.click-detector",
            "hidden-adapter",
            "internal-foundation",
        },
        {
            "pamguard.click-localiser",
            "pamguard.click-detector",
            "hidden-adapter",
            "internal-foundation",
        },
        {
            "pamguard.click-train",
            "pamguard.click-detector",
            "hidden-adapter",
            "internal-foundation",
        },
        {
            "pamguard.click-display",
            "pamguard.click-detector",
            "display-provider",
            "display-foundation",
        },
        {
            "pamguard.sound-output",
            "pamguard.sound-output",
            "controlled-unit",
            "partial",
        },
        {
            "pamguard.spectrogram-display",
            "pamguard.user-display",
            "display-provider",
            "display-foundation",
        },
    };
    for (const auto& runtime : expected) {
        const auto& manifest = find_by_id(
            document.at("runtimeTypes"),
            "runtimeTypeId",
            runtime.id,
            "runtimeTypes");
        const auto context = "runtime type '" + runtime.id + "'";
        require(
            required_string(manifest, "runtimeName", context) ==
                low_level_names.at(runtime.id) &&
            required_string(manifest, "operatorDisposition", context) ==
                runtime.disposition &&
            required_string(manifest, "parityLabel", context) ==
                runtime.parity,
            context + " catalogue metadata differs");
        const auto owners = required_string_array(
            manifest,
            "recipeOwners",
            context);
        require(
            std::find(
                owners.begin(),
                owners.end(),
                runtime.owner) != owners.end(),
            context + " omits its first-slice recipe owner");
    }

    const auto& display_runtime = find_by_id(
        document.at("runtimeTypes"),
        "runtimeTypeId",
        "pamguard.spectrogram-display",
        "runtimeTypes");
    const auto* provider = registry.find_display_provider(
        "pamguard.spectrogram-display");
    require(provider, "Spectrogram provider was not registered");
    require(
        required_string(
            display_runtime,
            "javaImplementationClass",
            "Spectrogram runtime type") ==
            provider->java_provider_class,
        "Spectrogram Java provider class differs from the manifest");
    require(
        std::any_of(
            low_level_types.begin(),
            low_level_types.end(),
            [](const auto& type) {
                return type.id == "pamguard.spectrogram-display";
            }),
        "Low-level catalogue omitted Spectrogram compatibility type");
}

void check_exact_first_slice(
    const ControlledUnitRegistry& registry) {
    require(
        registry.controlled_units().size() == 25 &&
            registry.display_providers().size() == 3 &&
            registry.global_settings().size() == 1,
        "Registry slice must contain 25 units, 3 providers, and 1 global component");

    const auto* acquisition =
        registry.find_controlled_unit("pamguard.acquisition");
    const auto* amplifier =
        registry.find_controlled_unit("pamguard.amplifier");
    const auto* patch_panel =
        registry.find_controlled_unit("pamguard.patch-panel");
    const auto* filter =
        registry.find_controlled_unit("pamguard.filter");
    const auto* decimator =
        registry.find_controlled_unit("pamguard.decimator");
    const auto* fft =
        registry.find_controlled_unit("pamguard.fft");
    const auto* fft_noise_monitor =
        registry.find_controlled_unit("pamguard.fft-noise-monitor");
    const auto* noise_band_monitor =
        registry.find_controlled_unit("pamguard.noise-band-monitor");
    const auto* ltsa =
        registry.find_controlled_unit("pamguard.ltsa");
    const auto* whistle_moan =
        registry.find_controlled_unit("pamguard.whistles-moans");
    const auto* sound_output =
        registry.find_controlled_unit("pamguard.sound-output");
    const auto* click_detector =
        registry.find_controlled_unit("pamguard.click-detector");
    const auto* mht_click_train =
        registry.find_controlled_unit("pamguard.mht-click-train");
    const auto* matched_template =
        registry.find_controlled_unit(
            "pamguard.matched-template-classifier");
    const auto* sound_recorder =
        registry.find_controlled_unit("pamguard.sound-recorder");
    const auto* clip_generator =
        registry.find_controlled_unit("pamguard.clip-generator");
    const auto* level_meter =
        registry.find_controlled_unit("pamguard.level-meter");
    const auto* user_input =
        registry.find_controlled_unit("pamguard.user-input");
    const auto* aural_listening =
        registry.find_controlled_unit("pamguard.aural-listening");
    const auto* alarm =
        registry.find_controlled_unit("pamguard.alarm-event-counter");
    const auto* effort =
        registry.find_controlled_unit("pamguard.effort-monitor");
    const auto* user_display =
        registry.find_controlled_unit("pamguard.user-display");
    const auto* spectrogram =
        registry.find_display_provider("pamguard.spectrogram-display");
    const auto* click_display =
        registry.find_display_provider("pamguard.click-display");
    const auto* level_meter_display =
        registry.find_display_provider(
            "pamguard.level-meter-display");
    const auto* array_manager =
        registry.find_global_settings("pamguard.array-manager");
    require(
        acquisition && amplifier && patch_panel && filter &&
            decimator && fft && fft_noise_monitor &&
            noise_band_monitor && ltsa && whistle_moan &&
            click_detector && mht_click_train && matched_template &&
            sound_recorder && clip_generator && sound_output && level_meter &&
            user_input && aural_listening && alarm && effort &&
            user_display &&
            spectrogram && click_display &&
            level_meter_display && array_manager,
        "Controlled-unit/global-settings slice is incomplete");
    require(
        array_manager->required &&
            array_manager->settings.version == 1 &&
            array_manager->adapter_id ==
                "pamguard.array-manager-settings.v1" &&
            array_manager->java_authority.class_name ==
                "Array.ArrayManager" &&
            array_manager->availability ==
                AvailabilityStatus::Available &&
            array_manager->parity_status == "partial",
        "Array Manager global descriptor/version/adapter changed");
    check_default_json(
        array_manager->settings,
        array_manager->id);

    for (const auto* descriptor :
         {acquisition, amplifier, patch_panel, filter, decimator,
          fft, whistle_moan, click_detector, mht_click_train,
          matched_template, user_display}) {
        require(
            descriptor->descriptor_version == 1 &&
                descriptor->settings.version == 1 &&
                descriptor->runtime_recipe.version == 1 &&
                descriptor->availability ==
                    AvailabilityStatus::Available &&
                descriptor->parity_status == "partial" &&
                descriptor->settings.whole_tree_change_policy ==
                    SettingsChangePolicy::StopRequired,
            descriptor->id +
                " descriptor/settings/recipe version or status changed");
        check_default_json(descriptor->settings, descriptor->id);
    }
    require(
        sound_output->descriptor_version == 1 &&
            sound_output->settings.version == 1 &&
            sound_output->runtime_recipe.version == 1 &&
            sound_output->availability ==
                AvailabilityStatus::Available &&
            sound_output->parity_status == "partial" &&
            sound_output->settings.parity_status ==
                "not-claimed" &&
            sound_output->settings.whole_tree_change_policy ==
                SettingsChangePolicy::LiveSafe,
        "Sound Output descriptor/settings/recipe version or status changed");
    const auto normal_sound_limits =
        pamguard::project::effective_instance_limits(
            sound_output->instance_rules,
            RunMode::Normal);
    const auto mixed_sound_limits =
        pamguard::project::effective_instance_limits(
            sound_output->instance_rules,
            RunMode::Mixed);
    const auto viewer_sound_limits =
        pamguard::project::effective_instance_limits(
            sound_output->instance_rules,
            RunMode::Viewer);
    require(
        sound_output->instance_rules.minimum_instances == 0 &&
            !sound_output->instance_rules.maximum_instances &&
            sound_output->instance_rules.mode_overrides.size() == 1 &&
            sound_output->instance_rules.mode_overrides.front().mode ==
                RunMode::Viewer &&
            sound_output->instance_rules.mode_overrides.front()
                    .minimum_instances == 1 &&
            sound_output->instance_rules.mode_overrides.front()
                    .maximum_instances ==
                std::optional<std::size_t>{1} &&
            normal_sound_limits ==
                pamguard::project::InstanceLimitDescriptor{
                    0,
                    std::nullopt,
                } &&
            mixed_sound_limits == normal_sound_limits &&
            viewer_sound_limits ==
                pamguard::project::InstanceLimitDescriptor{
                    1,
                    std::optional<std::size_t>{1},
                },
        "Sound Output Normal/Mixed/Viewer instance rules changed");
    check_default_json(
        sound_output->settings,
        sound_output->id);
    check_default_json(
        sound_recorder->settings,
        sound_recorder->id);
    check_default_json(
        clip_generator->settings,
        clip_generator->id);
    for (const auto* descriptor :
         {user_input, aural_listening, alarm, effort}) {
        require(
            descriptor->descriptor_version == 1 &&
                descriptor->settings.version == 1 &&
                descriptor->runtime_recipe.version == 1 &&
                descriptor->availability ==
                    AvailabilityStatus::Unavailable &&
                descriptor->parity_status == "experimental" &&
                descriptor->runtime_recipe.children.size() == 1 &&
                descriptor->runtime_recipe.children.front().
                    availability ==
                    AvailabilityStatus::Unavailable,
            descriptor->id +
                " must remain an unavailable experimental "
                "controlled-unit foundation");
        check_default_json(descriptor->settings, descriptor->id);
    }
    require(
        spectrogram->descriptor_version == 1 &&
            spectrogram->settings.version == 2 &&
            spectrogram->availability ==
                AvailabilityStatus::Available &&
            spectrogram->parity_status == "display-foundation" &&
            spectrogram->settings.whole_tree_change_policy ==
                SettingsChangePolicy::StopRequired,
        "Spectrogram descriptor/settings version or status changed");
    check_default_json(spectrogram->settings, spectrogram->id);
    require(
        click_display->descriptor_version == 1 &&
            click_display->settings.version == 1 &&
            click_display->availability ==
                AvailabilityStatus::Available &&
            click_display->parity_status == "browser-validated" &&
            click_display->owner_controlled_unit_type_id ==
                "pamguard.click-detector" &&
            click_display->minimum_instances == 1 &&
            click_display->maximum_instances ==
                std::optional<std::size_t>{1} &&
            !click_display->can_create_without_source,
        "Click Detector static-display ownership contract changed");
    check_default_json(
        click_display->settings,
        click_display->id);

    require(
        acquisition->runtime_recipe.children.size() == 1 &&
            acquisition->public_roles.size() == 1 &&
            acquisition->public_roles.front().id == "rawAudio" &&
            acquisition->runtime_recipe.public_role_mappings.size() == 1 &&
            acquisition->runtime_recipe.public_role_mappings.front()
                    .public_role_id == "rawAudio" &&
            acquisition->runtime_recipe.public_role_mappings.front()
                    .runtime_endpoint.port_id == "audio" &&
            acquisition->runtime_recipe.children.front().role_id ==
                "acquisition" &&
            acquisition->runtime_recipe.children.front()
                    .settings.source_pointer.empty() &&
            acquisition->runtime_recipe.children.front()
                    .settings.adapter_id ==
                "pamguard.acquisition-settings.v1",
        "Acquisition must explicitly project whole-unit settings through its v1 adapter");

    const auto check_signal_routing_recipe = [](
        const ControlledUnitDescriptor& descriptor,
        const std::string& output_role,
        const std::string& adapter_id) {
        require(
            descriptor.public_roles.size() == 2 &&
                descriptor.public_roles[0].id == "rawAudio" &&
                descriptor.public_roles[0].direction ==
                    DataRoleDirection::Input &&
                descriptor.public_roles[0].cardinality ==
                    RoleCardinality::ExactlyOne &&
                descriptor.public_roles[0]
                        .default_provider_controlled_unit_type_id ==
                    std::optional<std::string>{"pamguard.acquisition"} &&
                descriptor.public_roles[1].id == output_role &&
                descriptor.public_roles[1].direction ==
                    DataRoleDirection::Output &&
                descriptor.runtime_recipe.children.size() == 1 &&
                descriptor.runtime_recipe.children[0]
                        .runtime_type_id == descriptor.id &&
                descriptor.runtime_recipe.children[0]
                        .settings.source_pointer.empty() &&
                descriptor.runtime_recipe.children[0]
                        .settings.adapter_id == adapter_id &&
                descriptor.runtime_recipe.public_role_mappings.size() == 2 &&
                descriptor.runtime_recipe.public_role_mappings[0]
                        .public_role_id == "rawAudio" &&
                descriptor.runtime_recipe.public_role_mappings[0]
                        .runtime_endpoint.port_id == "input" &&
                descriptor.runtime_recipe.public_role_mappings[1]
                        .public_role_id == output_role &&
                descriptor.runtime_recipe.public_role_mappings[1]
                        .runtime_endpoint.port_id == "output",
            descriptor.id +
                " public source/output binding or identity recipe changed");
    };
    check_signal_routing_recipe(
        *amplifier,
        "amplifiedAudio",
        "identity.v1");
    check_signal_routing_recipe(
        *patch_panel,
        "patchedAudio",
        "identity.v1");
    check_signal_routing_recipe(
        *filter,
        "filteredAudio",
        "pamguard.standalone-filter-settings.v1");
    check_signal_routing_recipe(
        *decimator,
        "decimatedAudio",
        "pamguard.decimator-settings.v1");

    require(
        fft->public_roles.size() == 3 &&
            fft->public_roles[0].id == "rawAudio" &&
            fft->public_roles.front().direction ==
                DataRoleDirection::Input &&
            fft->public_roles.front().cardinality ==
                RoleCardinality::ExactlyOne &&
            fft->public_roles.front()
                    .default_provider_controlled_unit_type_id ==
                std::optional<std::string>{"pamguard.acquisition"} &&
            fft->public_roles[1].id == "fft" &&
            fft->public_roles[1].direction ==
                DataRoleDirection::Output &&
            fft->public_roles[2].id == "noiseReducedFft" &&
            fft->public_roles[2].direction ==
                DataRoleDirection::Output &&
            fft->runtime_recipe.public_role_mappings.size() == 3 &&
            fft->runtime_recipe.public_role_mappings[0].public_role_id ==
                "rawAudio" &&
            fft->runtime_recipe.public_role_mappings[0]
                    .runtime_endpoint.port_id == "input" &&
            fft->runtime_recipe.public_role_mappings[1].public_role_id ==
                "fft" &&
            fft->runtime_recipe.public_role_mappings[1]
                    .runtime_endpoint.child_role_id == "fft-process" &&
            fft->runtime_recipe.public_role_mappings[1]
                    .runtime_endpoint.port_id == "fft" &&
            fft->runtime_recipe.public_role_mappings[2].public_role_id ==
                "noiseReducedFft" &&
            fft->runtime_recipe.public_role_mappings[2]
                    .runtime_endpoint.child_role_id == "spectral-noise" &&
            fft->runtime_recipe.public_role_mappings[2]
                    .runtime_endpoint.port_id == "output" &&
            fft->runtime_recipe.children.size() == 2 &&
            fft->runtime_recipe.children[0].role_id == "fft-process" &&
            fft->runtime_recipe.children[0].settings.source_pointer ==
                "/fft" &&
            fft->runtime_recipe.children[1].role_id ==
                "spectral-noise" &&
            fft->runtime_recipe.children[1].settings.source_pointer ==
                "/spectralNoise" &&
            fft->runtime_recipe.internal_edges.size() == 1 &&
            fft->runtime_recipe.internal_edges.front().source.child_role_id ==
                "fft-process" &&
            fft->runtime_recipe.internal_edges.front().source.port_id ==
                "fft" &&
            fft->runtime_recipe.internal_edges.front().target.child_role_id ==
                "spectral-noise" &&
            fft->runtime_recipe.internal_edges.front().target.port_id ==
                "input",
        "FFT child settings projection or internal edge changed");

    const auto click_mapping = std::find_if(
        click_detector->runtime_recipe.public_role_mappings.begin(),
        click_detector->runtime_recipe.public_role_mappings.end(),
        [](const auto& mapping) {
            return mapping.public_role_id == "clicks";
        });
    const auto feature_edge = std::find_if(
        click_detector->runtime_recipe.internal_edges.begin(),
        click_detector->runtime_recipe.internal_edges.end(),
        [](const auto& edge) {
            return edge.id == "localiser-to-features";
        });
    const auto train_edge = std::find_if(
        click_detector->runtime_recipe.internal_edges.begin(),
        click_detector->runtime_recipe.internal_edges.end(),
        [](const auto& edge) {
            return edge.id == "localiser-to-train";
        });
    const auto click_schema =
        json::parse(click_detector->settings.settings_schema_json);
    const auto& tracked_schema =
        click_schema.at("properties")
            .at("localisation")
            .at("properties")
            .at("trackedTrain");
    const auto localiser_settings = json::parse(
        pamguard::project::click_detector_runtime_settings_json(
            click_detector->settings.default_settings_json,
            click_detector->settings.version,
            "localiser",
            pamguard::core::ArrayConfiguration{}));
    require(
        click_mapping !=
            click_detector->runtime_recipe.public_role_mappings.end() &&
            click_mapping->runtime_endpoint.child_role_id ==
                "localiser" &&
            click_mapping->runtime_endpoint.port_id == "accepted" &&
            feature_edge !=
                click_detector->runtime_recipe.internal_edges.end() &&
            feature_edge->source.child_role_id == "localiser" &&
            feature_edge->source.port_id == "accepted" &&
            feature_edge->target.child_role_id == "features" &&
            train_edge !=
                click_detector->runtime_recipe.internal_edges.end() &&
            train_edge->source.child_role_id == "localiser" &&
            train_edge->source.port_id == "accepted" &&
            train_edge->target.child_role_id == "train",
        "Click public/features/simple-train consumers must use the "
        "post-localiser accepted click");
    require(
        tracked_schema.value(
            "x-pamguard-runtime-availability",
            std::string{}) == "partial" &&
            tracked_schema.value(
                "x-pamguard-membership-runtime",
                std::string{}) == "available" &&
            tracked_schema.value(
                "x-pamguard-localisation-runtime",
                std::string{}) ==
                "unavailable-missing-navigation-origins" &&
            !tracked_schema.value(
                "x-pamguard-unavailable-reason",
                std::string{}).empty() &&
            !tracked_schema.value("readOnly", false) &&
            !localiser_settings.contains("trackedTrain"),
        "Manual TrackedClickLocaliser membership/settings boundary "
        "differs from the automatic click-localiser runtime");

    const auto& classification_schema =
        click_schema.at("properties")
            .at("classification")
            .at("properties");
    const auto& basic_schema =
        click_schema.at("$defs").at("basicClassifierType");
    const auto& sweep_schema =
        click_schema.at("$defs").at("sweepClassifierType");
    const auto& basic_default = basic_schema.at("default");
    const auto& sweep_default = sweep_schema.at("default");
    const auto check_exact_subtype_schema = [](
        const json& schema,
        const json& defaults,
        const std::string& context) {
        require(
            schema.at("additionalProperties") == false &&
                schema.at("properties").size() == defaults.size() &&
                schema.at("required").size() == defaults.size(),
            context + " schema is not exact/default-complete");
        for (const auto& [name, value] : defaults.items()) {
            require(
                schema.at("properties").contains(name) &&
                    schema.at("properties").at(name).at("default") ==
                        value &&
                    std::find(
                        schema.at("required").begin(),
                        schema.at("required").end(),
                        name) != schema.at("required").end(),
                context + " schema omits/defaults field '" + name + "'");
        }
    };
    check_exact_subtype_schema(
        basic_schema,
        basic_default,
        "Basic classifier");
    check_exact_subtype_schema(
        sweep_schema,
        sweep_default,
        "Sweep classifier");
    require(
        classification_schema.at("basicTypes")
                .at("items")
                .at("$ref") ==
            "#/$defs/basicClassifierType" &&
            classification_schema.at("sweepTypes")
                .at("items")
                .at("$ref") ==
            "#/$defs/sweepClassifierType" &&
            classification_schema.at("basicTypes")
                .at("x-pamguard-unique-by") == "speciesCode" &&
            classification_schema.at("sweepTypes")
                .at("x-pamguard-unique-by") == "speciesCode" &&
            basic_schema.at("properties")
                .at("enabled")
                .at("x-pamguard-runtime-effect") ==
            "stored-java-quirk-no-classification-effect" &&
            basic_schema.at("properties")
                .at("speciesCode")
                .at("x-pamguard-portable-normalization") ==
            "unique-1-to-255" &&
            sweep_schema.at("properties")
                .at("name")
                .at("x-pamguard-portable-normalization") ==
            "non-null-string",
        "Classifier subtype references, Java quirk, or portable "
        "normalization metadata changed");
    require(
        basic_default.at("whichSelections") == 5 &&
            basic_default.at("enabled") == true &&
            sweep_default.at("channelChoice") == "requireAll" &&
            sweep_default.at("lengthSmoothing") == 5 &&
            sweep_default.at("lengthDb") == 6.0 &&
            sweep_default.at("amplitudeRangeDb") ==
                json::array({0.0, 200.0}) &&
            sweep_default.at("fftFilter").at("band") == "highPass" &&
            sweep_default.at("bearingLimitsRadians") ==
                json::array({
                    -3.14159265358979323846,
                    3.14159265358979323846,
                }),
        "Java-derived classifier defaults or portable allocations changed");

    auto classifier_settings =
        json::parse(click_detector->settings.default_settings_json);
    auto basic_type = basic_default;
    basic_type["name"] = "Basic parity type";
    basic_type["enabled"] = false;
    basic_type["band1FreqHz"] = {2000.0, -1000.0};
    basic_type["widthEnergyFraction"] = 125.0;
    auto sweep_type = sweep_default;
    sweep_type["name"] = "Sweep parity type";
    sweep_type["speciesCode"] = 2;
    classifier_settings["classification"]["runOnline"] = true;
    classifier_settings["classification"]["checkAllClassifiers"] = true;
    classifier_settings["classification"]["basicTypes"] =
        json::array({basic_type});
    classifier_settings["classification"]["sweepTypes"] =
        json::array({sweep_type});
    pamguard::project::validate_click_detector_settings_json(
        classifier_settings.dump(),
        click_detector->settings.version);

    classifier_settings["classification"]["mode"] = "basic";
    const auto basic_projection = json::parse(
        pamguard::project::click_detector_runtime_settings_json(
            classifier_settings.dump(),
            click_detector->settings.version,
            "classifier",
            pamguard::core::ArrayConfiguration{}));
    require(
        basic_projection.at("types") == json::array({basic_type}) &&
            basic_projection.at("checkAllClassifiers") == true &&
            basic_projection.at("types").at(0).at("enabled") == false,
        "Basic subtype projection did not preserve exact settings or the "
        "stored-but-ignored Java enable flag");
    classifier_settings["classification"]["mode"] = "sweep";
    const auto sweep_projection = json::parse(
        pamguard::project::click_detector_runtime_settings_json(
            classifier_settings.dump(),
            click_detector->settings.version,
            "classifier",
            pamguard::core::ArrayConfiguration{}));
    require(
        sweep_projection.at("types") == json::array({sweep_type}) &&
            sweep_projection.at("checkAllClassifiers") == true,
        "Sweep subtype projection/check-all policy changed");

    const auto expect_classifier_rejection = [&](
        const json& candidate,
        const std::string& context) {
        bool rejected = false;
        try {
            pamguard::project::validate_click_detector_settings_json(
                candidate.dump(),
                click_detector->settings.version);
        }
        catch (const pamguard::project::ClickDetectorSettingsError&) {
            rejected = true;
        }
        require(rejected, "Click settings accepted " + context);
    };
    auto invalid_classifier = classifier_settings;
    invalid_classifier["classification"]["basicTypes"][0].erase("name");
    expect_classifier_rejection(
        invalid_classifier,
        "a Basic subtype with a missing canonical field");
    invalid_classifier = classifier_settings;
    invalid_classifier["classification"]["basicTypes"][0]["unknown"] = 1;
    expect_classifier_rejection(
        invalid_classifier,
        "a Basic subtype with an unknown field");
    invalid_classifier = classifier_settings;
    invalid_classifier["classification"]["basicTypes"].push_back(basic_type);
    expect_classifier_rejection(
        invalid_classifier,
        "duplicate portable Basic species codes");
    invalid_classifier = classifier_settings;
    invalid_classifier["classification"]["basicTypes"][0]["speciesCode"] =
        256;
    expect_classifier_rejection(
        invalid_classifier,
        "a Basic species code outside the portable 1..255 range");
    invalid_classifier = classifier_settings;
    invalid_classifier["classification"]["sweepTypes"][0]["channelChoice"] =
        "invented";
    expect_classifier_rejection(
        invalid_classifier,
        "an unknown Sweep channel policy");
    invalid_classifier = classifier_settings;
    invalid_classifier["classification"]["sweepTypes"][0]
        ["peakSmoothing"] = "stale-but-not-an-integer";
    expect_classifier_rejection(
        invalid_classifier,
        "an inactive Sweep field with the wrong canonical JSON type");
    invalid_classifier = classifier_settings;
    invalid_classifier["classification"]["sweepTypes"][0]
        ["enableEnergyBands"] = true;
    expect_classifier_rejection(
        invalid_classifier,
        "equal active Sweep energy-band limits");
    invalid_classifier = classifier_settings;
    invalid_classifier["classification"]["sweepTypes"][0]["enablePeak"] =
        true;
    invalid_classifier["classification"]["sweepTypes"][0]
        ["peakSearchRangeHz"] = {100.0, 200.0};
    invalid_classifier["classification"]["sweepTypes"][0]["peakRangeHz"] =
        {120.0, 180.0};
    invalid_classifier["classification"]["sweepTypes"][0]
        ["peakSmoothing"] = 2;
    expect_classifier_rejection(
        invalid_classifier,
        "even smoothing for an active Sweep spectral test");
    invalid_classifier = classifier_settings;
    invalid_classifier["classification"]["sweepTypes"][0]["enableWidth"] =
        true;
    invalid_classifier["classification"]["sweepTypes"][0]
        ["peakSearchRangeHz"] = {100.0, 200.0};
    invalid_classifier["classification"]["sweepTypes"][0]
        ["peakWidthRangeHz"] = {10.0, 20.0};
    invalid_classifier["classification"]["sweepTypes"][0]
        ["peakWidthThresholdDb"] = 0.0;
    expect_classifier_rejection(
        invalid_classifier,
        "a zero threshold for an active Sweep width test");

    auto java_runtime_quirks = classifier_settings;
    auto& quirky_sweep =
        java_runtime_quirks["classification"]["sweepTypes"][0];
    quirky_sweep["peakSmoothing"] = 2;
    quirky_sweep["peakWidthThresholdDb"] = 0.0;
    quirky_sweep["correlationFactor"] = 0.0;
    quirky_sweep["enableFftFilter"] = true;
    quirky_sweep["fftFilter"]["highPassFreqHz"] = 4000.0;
    quirky_sweep["fftFilter"]["lowPassFreqHz"] = -100.0;
    quirky_sweep["fftFilter"]["band"] = "bandPass";
    quirky_sweep["enableSweep"] = true;
    quirky_sweep["enableZeroCrossings"] = false;
    quirky_sweep["zeroCrossingSweepKhzPerMs"] = {-2.0, 3.0};
    quirky_sweep["bearingLimitsRadians"] = {4.0, -4.0};
    pamguard::project::validate_click_detector_settings_json(
        java_runtime_quirks.dump(),
        click_detector->settings.version);

    require(
        sound_output->public_roles.size() == 1 &&
            sound_output->public_roles.front().id == "audio" &&
            sound_output->public_roles.front().direction ==
                DataRoleDirection::Input &&
            sound_output->public_roles.front().cardinality ==
                RoleCardinality::ExactlyOne &&
            !sound_output->public_roles.front()
                .default_provider_controlled_unit_type_id &&
            sound_output->runtime_recipe.children.size() == 1 &&
            sound_output->runtime_recipe.children.front().role_id ==
                "playback-process" &&
            sound_output->runtime_recipe.children.front()
                .runtime_type_id == "pamguard.sound-output" &&
            sound_output->runtime_recipe.children.front()
                .settings.adapter_id == "identity.v1" &&
            sound_output->runtime_recipe.public_role_mappings.size() == 1 &&
            sound_output->runtime_recipe.public_role_mappings.front()
                .public_role_id == "audio" &&
            sound_output->runtime_recipe.public_role_mappings.front()
                .runtime_endpoint.port_id == "audio",
        "Sound Output public binding/runtime recipe changed");

    require(
        user_display->runtime_recipe.children.empty() &&
            user_display->runtime_recipe.internal_edges.empty() &&
            user_display->runtime_recipe.display_provider_ids ==
                std::vector<std::string>{
                    "pamguard.spectrogram-display"} &&
            spectrogram->provider_name == "Spectrogram Display" &&
            spectrogram->java_provider_class ==
                "Spectrogram.SpectrogramDiplayProvider" &&
            spectrogram->java_component_class ==
                "Spectrogram.SpectrogramDisplayComponent" &&
            spectrogram->owner_controlled_unit_type_id ==
                "pamguard.user-display" &&
            !spectrogram->maximum_instances &&
            spectrogram->can_create_without_source &&
            spectrogram->public_roles.size() == 1 &&
            spectrogram->public_roles.front().cardinality ==
                RoleCardinality::ZeroOrOne,
        "User Display/Spectrogram ownership or no-runtime-child contract changed");
}

void check_validator_rejects_bad_contracts(
    const ControlledUnitRegistry& registry,
    const std::vector<LowLevelTypeContract>& low_level_types) {
    ControlledUnitRegistry duplicate_registry;
    duplicate_registry.register_controlled_unit(
        *registry.find_controlled_unit("pamguard.acquisition"));
    bool duplicate_rejected = false;
    try {
        duplicate_registry.register_controlled_unit(
            *registry.find_controlled_unit("pamguard.acquisition"));
    }
    catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    require(
        duplicate_rejected,
        "Registry accepted a duplicate controlled-unit ID");

    ControlledUnitRegistry missing_settings_authority;
    auto missing_settings_class =
        *registry.find_controlled_unit("pamguard.acquisition");
    missing_settings_class.settings.authority_classes.clear();
    missing_settings_authority.register_controlled_unit(
        std::move(missing_settings_class));
    require(
        has_issue(
            missing_settings_authority.validate(),
            "invalid-settings-authority",
            "pamguard.acquisition"),
        "Registry allowed a settings-bearing unit to omit its "
        "Java settings authority");

    const auto unavailable_user_input =
        *registry.find_controlled_unit("pamguard.user-input");
    ControlledUnitRegistry unavailable_missing_runtime;
    unavailable_missing_runtime.register_controlled_unit(
        unavailable_user_input);
    require(
        unavailable_missing_runtime.validate_against({}).valid(),
        "Registry rejected an explicitly unavailable descriptor and "
        "unavailable child solely because its future runtime type is absent");

    auto available_child = unavailable_user_input;
    available_child.runtime_recipe.children.front().availability =
        AvailabilityStatus::Available;
    ControlledUnitRegistry available_child_missing_runtime;
    available_child_missing_runtime.register_controlled_unit(
        std::move(available_child));
    require(
        has_issue(
            available_child_missing_runtime.validate_against({}),
            "missing-low-level-type",
            "pamguard.user-input"),
        "Registry allowed an available runtime child to omit its "
        "low-level type");

    auto available_descriptor = unavailable_user_input;
    available_descriptor.availability =
        AvailabilityStatus::Available;
    ControlledUnitRegistry available_descriptor_missing_runtime;
    available_descriptor_missing_runtime.register_controlled_unit(
        std::move(available_descriptor));
    require(
        has_issue(
            available_descriptor_missing_runtime.validate_against({}),
            "missing-low-level-type",
            "pamguard.user-input"),
        "Registry allowed an available descriptor to omit its "
        "low-level runtime type");

    ControlledUnitRegistry invalid_global_registry;
    auto invalid_global =
        *registry.find_global_settings("pamguard.array-manager");
    invalid_global.adapter_id = "Invalid Adapter";
    invalid_global_registry.register_global_settings(
        std::move(invalid_global));
    require(
        has_issue(
            invalid_global_registry.validate(),
            "invalid-global-settings",
            "pamguard.array-manager"),
        "Registry validation accepted an invalid global adapter ID");

    ControlledUnitRegistry invalid_role_registry;
    auto invalid =
        *registry.find_controlled_unit("pamguard.acquisition");
    invalid.public_roles.front().id = "Bad Role";
    invalid_role_registry.register_controlled_unit(std::move(invalid));
    const auto invalid_role_validation =
        invalid_role_registry.validate();
    require(
        has_issue(
            invalid_role_validation,
            "invalid-public-role-id",
            "pamguard.acquisition"),
        "Registry validation accepted invalid public-role syntax");

    ControlledUnitRegistry duplicate_override_registry;
    auto duplicate_override =
        *registry.find_controlled_unit("pamguard.sound-output");
    duplicate_override.instance_rules.mode_overrides.push_back(
        duplicate_override.instance_rules.mode_overrides.front());
    duplicate_override_registry.register_controlled_unit(
        std::move(duplicate_override));
    require(
        has_issue(
            duplicate_override_registry.validate(),
            "invalid-instance-mode-overrides",
            "pamguard.sound-output"),
        "Registry validation accepted duplicate run-mode overrides");

    ControlledUnitRegistry disallowed_override_registry;
    auto disallowed_override =
        *registry.find_controlled_unit("pamguard.sound-output");
    disallowed_override.instance_rules.allowed_modes = {
        RunMode::Normal,
        RunMode::Mixed,
    };
    disallowed_override_registry.register_controlled_unit(
        std::move(disallowed_override));
    require(
        has_issue(
            disallowed_override_registry.validate(),
            "invalid-instance-mode-overrides",
            "pamguard.sound-output"),
        "Registry validation accepted an override for a disallowed mode");

    ControlledUnitRegistry invalid_override_limits_registry;
    auto invalid_override_limits =
        *registry.find_controlled_unit("pamguard.sound-output");
    invalid_override_limits.instance_rules.mode_overrides.front()
        .minimum_instances = 2;
    invalid_override_limits_registry.register_controlled_unit(
        std::move(invalid_override_limits));
    require(
        has_issue(
            invalid_override_limits_registry.validate(),
            "invalid-instance-mode-overrides",
            "pamguard.sound-output"),
        "Registry validation accepted invalid run-mode override limits");

    auto incompatible_catalogue = low_level_types;
    auto type = std::find_if(
        incompatible_catalogue.begin(),
        incompatible_catalogue.end(),
        [](const auto& entry) {
            return entry.id == "pamguard.spectrogram-noise";
        });
    require(
        type != incompatible_catalogue.end(),
        "Low-level catalogue omitted spectral-noise type");
    auto port = std::find_if(
        type->ports.begin(),
        type->ports.end(),
        [](const auto& entry) { return entry.id == "output"; });
    require(
        port != type->ports.end(),
        "Low-level spectral-noise type omitted output port");
    port->data_type = "pamguard.incompatible";
    const auto mismatch =
        registry.validate_against(incompatible_catalogue);
    require(
        has_issue(
            mismatch,
            "low-level-data-type-mismatch",
            "pamguard.fft"),
        "Registry validation accepted an incompatible child output mapping");
}

void check_unavailable_units_cannot_be_instantiated(
    const ControlledUnitRegistry& registry) {
    pamguard::core::ModuleRegistry runtime_registry;
    pamguard::core::register_builtin_module_types(runtime_registry);
    const auto temporary_root =
        std::filesystem::temp_directory_path() /
        (
            "pamguard-unavailable-utilities-" +
            pamguard::project::generate_uuid_v4());
    try {
        {
            pamguard::project::ProjectStore store(temporary_root);
            pamguard::project::ProjectAuthority authority(
                registry,
                runtime_registry,
                store);
            for (const auto* type_id :
                 {
                     "pamguard.user-input",
                     "pamguard.aural-listening",
                     "pamguard.alarm-event-counter",
                     "pamguard.effort-monitor",
                 }) {
                pamguard::project::ProjectMutationBatch batch;
                batch.operations.push_back(
                    pamguard::project::AddControlledUnitOperation{
                        "unavailable-utility",
                        type_id,
                    });
                bool rejected_as_unavailable = false;
                try {
                    static_cast<void>(
                        authority.mutate(
                            authority.snapshot().etag,
                            batch));
                }
                catch (
                    const pamguard::project::ProjectAuthorityError&
                        error) {
                    rejected_as_unavailable =
                        error.code() ==
                        "controlled_unit_type_unavailable";
                }
                require(
                    rejected_as_unavailable &&
                        authority.snapshot().project
                            .controlled_units.empty(),
                    std::string(type_id) +
                        " was instantiable despite unavailable "
                        "catalogue status");
            }
        }
    }
    catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(temporary_root, ignored);
        throw;
    }
    std::error_code ignored;
    std::filesystem::remove_all(temporary_root, ignored);
}

json read_manifest(const std::filesystem::path& path) {
    std::ifstream input(path);
    require(
        input.is_open(),
        "Could not open controlled-unit parity manifest: " +
            path.string());
    json document;
    input >> document;
    require(document.is_object(), "Parity manifest root must be an object");
    require(
        document.value("schemaVersion", 0) == 2 &&
            document.value("manifestId", std::string{}) ==
                "pamguard-controlled-unit-parity",
        "Unexpected controlled-unit parity manifest schema/identity");
    return document;
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(
            argc == 2,
            "Usage: controlled_unit_registry_check <controlled-unit-parity-manifest.json>");
        const auto document = read_manifest(argv[1]);

        ControlledUnitRegistry registry;
        pamguard::project::register_builtin_controlled_units(registry);
        check_exact_first_slice(registry);
        require_clean_validation(
            registry.validate(),
            "Controlled-unit registry structural validation failed");

        const auto low_level_types = low_level_catalogue();
        require_clean_validation(
            registry.validate_against(low_level_types),
            "Controlled-unit registry low-level compatibility failed");

        for (const auto& descriptor : registry.controlled_units()) {
            check_manifest_bundle(descriptor, document);
            check_manifest_instance_and_dependencies(
                descriptor,
                registry,
                document);
            const bool has_core_configuration_contract =
                std::any_of(
                    document.at("coreConfigurationContracts").begin(),
                    document.at("coreConfigurationContracts").end(),
                    [&](const auto& contract) {
                        return contract.value(
                                   "bundleId",
                                   std::string{}) ==
                            descriptor.id;
                    });
            if (has_core_configuration_contract) {
                check_manifest_configuration(
                    descriptor,
                    registry,
                    document);
            }
        }
        check_manifest_runtime_types(
            registry,
            low_level_types,
            document);
        check_validator_rejects_bad_contracts(
            registry,
            low_level_types);
        check_unavailable_units_cannot_be_instantiated(
            registry);

        std::cout
            << "Controlled-unit registry validated "
            << registry.controlled_units().size()
            << " Java units, "
            << registry.display_providers().size()
            << " display providers, "
            << registry.global_settings().size()
            << " global Array Manager descriptor, "
               "and exact low-level recipe compatibility\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
