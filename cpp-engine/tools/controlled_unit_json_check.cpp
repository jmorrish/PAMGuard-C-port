#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <json.hpp>

#include "pamguard/core/MatchedTemplateSettings.h"
#include "pamguard/core/MhtClickTrainSettings.h"
#include "pamguard/core/WhistleMoanSettings.h"
#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ControlledUnitJson.h"

namespace {

using Json = nlohmann::json;
using pamguard::project::ControlledUnitJsonError;
using pamguard::project::ControlledUnitRegistry;
using pamguard::project::ControlledUnitRegistryValidation;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::set<std::string> keys(const Json& object) {
    require(object.is_object(), "Expected a JSON object");
    std::set<std::string> result;
    for (auto iterator = object.begin();
         iterator != object.end();
         ++iterator) {
        result.insert(iterator.key());
    }
    return result;
}

bool has_issue(
    const ControlledUnitRegistryValidation& validation,
    std::string_view code) {
    return std::any_of(
        validation.issues.begin(),
        validation.issues.end(),
        [&](const auto& issue) { return issue.code == code; });
}

void require_keys(
    const Json& object,
    std::initializer_list<std::string_view> expected,
    const std::string& context) {
    std::set<std::string> wanted;
    for (const auto key : expected) {
        wanted.emplace(key);
    }
    require(
        keys(object) == wanted,
        context + " has an unexpected JSON shape");
}

const Json& find_by_string(
    const Json& array,
    std::string_view field,
    std::string_view value,
    const std::string& context) {
    require(array.is_array(), context + " must be an array");
    const auto found = std::find_if(
        array.begin(),
        array.end(),
        [&](const Json& entry) {
            return entry.is_object() &&
                entry.value(
                    std::string(field),
                    std::string{}) == value;
        });
    require(
        found != array.end(),
        context + " is missing '" + std::string(value) + "'");
    return *found;
}

std::vector<std::string> string_values(
    const Json& array,
    std::string_view field) {
    require(array.is_array(), "Expected a JSON array");
    std::vector<std::string> result;
    for (const auto& entry : array) {
        require(
            entry.is_object() &&
                entry.contains(std::string(field)) &&
                entry.at(std::string(field)).is_string(),
            "Expected an object with a string identity field");
        result.push_back(
            entry.at(std::string(field)).get<std::string>());
    }
    return result;
}

bool schema_type_contains(
    const Json& schema,
    std::string_view expected) {
    const auto& type = schema.at("type");
    if (type.is_string()) {
        return type.get<std::string>() == expected;
    }
    return type.is_array() &&
        std::any_of(
            type.begin(),
            type.end(),
            [&](const Json& candidate) {
                return candidate.is_string() &&
                    candidate.get<std::string>() == expected;
            });
}

void check_closed_object_schemas(
    const Json& schema,
    const std::string& path) {
    require(schema.is_object(), path + " schema node must be an object");
    if (schema.contains("type") &&
        schema_type_contains(schema, "object")) {
        require(
            schema.contains("additionalProperties") &&
                schema.at("additionalProperties").is_boolean() &&
                !schema.at("additionalProperties").get<bool>(),
            path +
                " object schema is not closed with additionalProperties:false");
    }
    if (schema.contains("properties")) {
        require(
            schema.at("properties").is_object(),
            path + ".properties must be an object");
        for (auto property = schema.at("properties").begin();
             property != schema.at("properties").end();
             ++property) {
            check_closed_object_schemas(
                property.value(),
                path + ".properties." + property.key());
        }
    }
    if (schema.contains("items")) {
        check_closed_object_schemas(
            schema.at("items"),
            path + ".items");
    }
    if (schema.contains("$defs")) {
        require(
            schema.at("$defs").is_object(),
            path + ".$defs must be an object");
        for (auto definition = schema.at("$defs").begin();
             definition != schema.at("$defs").end();
             ++definition) {
            check_closed_object_schemas(
                definition.value(),
                path + ".$defs." + definition.key());
        }
    }
}

void check_default_structure(
    const Json& schema,
    const Json& value,
    const std::string& path,
    const Json& root_schema) {
    if (schema.contains("$ref")) {
        require(
            schema.at("$ref").is_string(),
            path + " schema reference must be a string");
        const auto reference =
            schema.at("$ref").get<std::string>();
        require(
            reference.starts_with("#/"),
            path + " schema reference must be document-local");
        const Json::json_pointer pointer(reference.substr(1));
        require(
            root_schema.contains(pointer),
            path + " schema reference does not resolve");
        check_default_structure(
            root_schema.at(pointer),
            value,
            path,
            root_schema);
        return;
    }
    if (value.is_object()) {
        require(
            schema_type_contains(schema, "object"),
            path + " default object has a non-object schema");
        const auto properties = schema.find("properties");
        if (value.empty() && properties == schema.end()) {
            return;
        }
        require(
            properties != schema.end() && properties->is_object(),
            path + " object schema is missing properties");

        std::set<std::string> default_keys;
        for (auto entry = value.begin();
             entry != value.end();
             ++entry) {
            default_keys.insert(entry.key());
            require(
                properties->contains(entry.key()),
                path + " default contains undeclared key " +
                    entry.key());
            check_default_structure(
                properties->at(entry.key()),
                entry.value(),
                path + "." + entry.key(),
                root_schema);
        }

        require(
            schema.contains("required") &&
                schema.at("required").is_array(),
            path + " complete object schema is missing required");
        std::set<std::string> required;
        for (const auto& key : schema.at("required")) {
            require(
                key.is_string(),
                path + ".required contains a non-string");
            required.insert(key.get<std::string>());
        }
        require(
            required == default_keys,
            path +
                " required fields do not exactly match canonical defaults");
        return;
    }

    if (value.is_array()) {
        require(
            schema_type_contains(schema, "array") &&
                schema.contains("items"),
            path + " default array has an incomplete schema");
        for (std::size_t index = 0;
             index < value.size();
             ++index) {
            check_default_structure(
                schema.at("items"),
                value[index],
                path + "[" + std::to_string(index) + "]",
                root_schema);
        }
        return;
    }

    const std::string expected =
        value.is_null()
        ? "null"
        : value.is_boolean()
        ? "boolean"
        : (value.is_number_integer() ||
           value.is_number_unsigned())
        ? "integer"
        : value.is_number()
        ? "number"
        : value.is_string()
        ? "string"
        : "";
    require(
        !expected.empty() &&
            (schema_type_contains(schema, expected) ||
             (expected == "integer" &&
              schema_type_contains(schema, "number"))),
        path + " default primitive type differs from schema");
}

void check_settings_contract(
    const Json& settings,
    const std::string& context,
    std::string_view expected_change_policy = "stop-required",
    int expected_version = 1) {
    require_keys(
        settings,
        {
            "version",
            "schema",
            "defaults",
            "sections",
            "defaultEvidence",
            "changeRules",
            "javaAuthority",
            "status",
        },
        context);
    require(
        settings.at("version") == expected_version,
        context + " settings version changed");
    require(
        settings.at("schema").at("$schema") ==
            "https://json-schema.org/draft/2020-12/schema",
        context + " schema dialect changed");
    check_closed_object_schemas(
        settings.at("schema"),
        context + ".schema");
    check_default_structure(
        settings.at("schema"),
        settings.at("defaults"),
        context + ".defaults",
        settings.at("schema"));
    require(
        settings.at("changeRules") ==
            Json::array({
                {
                    {"pointer", ""},
                    {"policy", std::string(expected_change_policy)},
                },
            }),
        context + " whole-tree change rule changed");
    require(
        settings.at("javaAuthority").at("classes").is_array() &&
            !settings.at("javaAuthority").at("classes").empty() &&
            settings.at("javaAuthority")
                .at("sourceReferences")
                .is_array() &&
            !settings.at("javaAuthority")
                 .at("sourceReferences")
                 .empty(),
        context + " omitted settings authority evidence");
    require(
        settings.at("status").at("parity") == "not-claimed",
        context + " made an unsupported settings parity claim");
}

void check_role(
    const Json& role,
    std::string_view id,
    std::string_view direction,
    std::string_view data_type,
    std::string_view cardinality) {
    require_keys(
        role,
        {
            "id",
            "name",
            "direction",
            "dataType",
            "cardinality",
            "capabilities",
            "javaDataClass",
            "defaultProvider",
        },
        "role " + std::string(id));
    require(
        role.at("id") == std::string(id) &&
            role.at("direction") == std::string(direction) &&
            role.at("dataType") == std::string(data_type) &&
            role.at("cardinality") == std::string(cardinality),
        "Role contract changed for " + std::string(id));
}

std::set<std::string> semantic_constraint_ids(
    const Json& settings_schema) {
    std::set<std::string> result;
    const auto found =
        settings_schema.find("x-pamguardConstraints");
    if (found == settings_schema.end()) {
        return result;
    }
    require(
        found->is_array(),
        "x-pamguardConstraints must be an array");
    for (const auto& constraint : *found) {
        require(
            constraint.is_object() &&
                constraint.contains("id") &&
                constraint.at("id").is_string() &&
                constraint.contains("kind") &&
                constraint.at("kind").is_string(),
            "Semantic settings constraint is malformed");
        require(
            result.insert(
                constraint.at("id").get<std::string>()).second,
            "Duplicate semantic settings constraint");
    }
    return result;
}

void check_catalogue(const Json& root) {
    require_keys(
        root,
        {
            "schemaVersion",
            "descriptorSet",
            "controlledUnitTypes",
            "displayProviderTypes",
            "globalSettingsTypes",
        },
        "catalogue");
    require(
        root.at("schemaVersion") == 1,
        "Catalogue schemaVersion changed");
    require(
        root.at("descriptorSet") ==
            Json({
                {"id", "pamguard-2.02.18e"},
                {"version", 1},
                {
                    "authorityCommit",
                    "dca55c81ef6f1498a8a3b926c69e7182afb915ee",
                },
            }),
        "Descriptor-set authority identity changed");

    const auto& units = root.at("controlledUnitTypes");
    const auto& providers = root.at("displayProviderTypes");
    const auto& globals = root.at("globalSettingsTypes");
    require(
        string_values(units, "typeId") ==
            std::vector<std::string>{
                "pamguard.acquisition",
                "pamguard.amplifier",
                "pamguard.patch-panel",
                "pamguard.filter",
                "pamguard.decimator",
                "pamguard.fft",
                "pamguard.fft-noise-monitor",
                "pamguard.noise-band-monitor",
                "pamguard.ltsa",
                "pamguard.whistles-moans",
                "pamguard.ishmael-energy-sum",
                "pamguard.ishmael-sgram-corr",
                "pamguard.ishmael-match-filter",
                "pamguard.click-detector",
                "pamguard.mht-click-train",
                "pamguard.matched-template-classifier",
                "pamguard.sound-recorder",
                "pamguard.clip-generator",
                "pamguard.sound-output",
                "pamguard.level-meter",
                "pamguard.user-input",
                "pamguard.aural-listening",
                "pamguard.alarm-event-counter",
                "pamguard.effort-monitor",
                "pamguard.user-display",
            },
        "Controlled-unit first-slice identities/order changed");
    require(
        string_values(providers, "providerTypeId") ==
            std::vector<std::string>{
                "pamguard.spectrogram-display",
                "pamguard.click-display",
                "pamguard.level-meter-display",
            },
        "Display-provider first-slice identities/order changed");
    require(
        string_values(globals, "typeId") ==
            std::vector<std::string>{
                "pamguard.array-manager",
            },
        "Global-settings first-slice identity changed");

    const auto& acquisition = find_by_string(
        units,
        "typeId",
        "pamguard.acquisition",
        "controlledUnitTypes");
    const auto& amplifier = find_by_string(
        units,
        "typeId",
        "pamguard.amplifier",
        "controlledUnitTypes");
    const auto& patch_panel = find_by_string(
        units,
        "typeId",
        "pamguard.patch-panel",
        "controlledUnitTypes");
    const auto& filter = find_by_string(
        units,
        "typeId",
        "pamguard.filter",
        "controlledUnitTypes");
    const auto& decimator = find_by_string(
        units,
        "typeId",
        "pamguard.decimator",
        "controlledUnitTypes");
    const auto& fft = find_by_string(
        units,
        "typeId",
        "pamguard.fft",
        "controlledUnitTypes");
    const auto& fft_noise_monitor = find_by_string(
        units,
        "typeId",
        "pamguard.fft-noise-monitor",
        "controlledUnitTypes");
    const auto& noise_band_monitor = find_by_string(
        units,
        "typeId",
        "pamguard.noise-band-monitor",
        "controlledUnitTypes");
    const auto& ltsa = find_by_string(
        units,
        "typeId",
        "pamguard.ltsa",
        "controlledUnitTypes");
    const auto& whistle_moan = find_by_string(
        units,
        "typeId",
        "pamguard.whistles-moans",
        "controlledUnitTypes");
    const auto& sound_output = find_by_string(
        units,
        "typeId",
        "pamguard.sound-output",
        "controlledUnitTypes");
    const auto& click_detector = find_by_string(
        units,
        "typeId",
        "pamguard.click-detector",
        "controlledUnitTypes");
    const auto& mht_click_train = find_by_string(
        units,
        "typeId",
        "pamguard.mht-click-train",
        "controlledUnitTypes");
    const auto& matched_template = find_by_string(
        units,
        "typeId",
        "pamguard.matched-template-classifier",
        "controlledUnitTypes");
    const auto& user_input = find_by_string(
        units,
        "typeId",
        "pamguard.user-input",
        "controlledUnitTypes");
    const auto& aural_listening = find_by_string(
        units,
        "typeId",
        "pamguard.aural-listening",
        "controlledUnitTypes");
    const auto& alarm = find_by_string(
        units,
        "typeId",
        "pamguard.alarm-event-counter",
        "controlledUnitTypes");
    const auto& effort = find_by_string(
        units,
        "typeId",
        "pamguard.effort-monitor",
        "controlledUnitTypes");
    const auto& user_display = find_by_string(
        units,
        "typeId",
        "pamguard.user-display",
        "controlledUnitTypes");
    const auto& spectrogram = find_by_string(
        providers,
        "providerTypeId",
        "pamguard.spectrogram-display",
        "displayProviderTypes");
    const auto& click_display = find_by_string(
        providers,
        "providerTypeId",
        "pamguard.click-display",
        "displayProviderTypes");
    const auto& array_manager = find_by_string(
        globals,
        "typeId",
        "pamguard.array-manager",
        "globalSettingsTypes");

    require(
        array_manager.at("name") == "Array Manager" &&
            array_manager.at("adapterId") ==
                "pamguard.array-manager-settings.v1" &&
            array_manager.at("required") == true &&
            array_manager.at("javaAuthority") ==
                Json({
                    {"registeredName", "Array Manager"},
                    {"className", "Array.ArrayManager"},
                    {"relationship", "global-core"},
                }) &&
            array_manager.at("status") ==
                Json({
                    {"availability", "available"},
                    {"parity", "partial"},
                }),
        "Array Manager descriptor identity/status changed");
    require(
        array_manager.at("settings").at("defaults")
                .at("arrayName") == "Basic Linear Array" &&
            array_manager.at("settings").at("defaults")
                .at("speedOfSoundMps") == 1500 &&
            array_manager.at("settings").at("defaults")
                .at("streamers").size() == 1 &&
            array_manager.at("settings").at("defaults")
                .at("hydrophones").size() == 2 &&
            semantic_constraint_ids(
                array_manager.at("settings").at("schema")) ==
                std::set<std::string>{
                    "hydrophone-bandwidth-ordered",
                    "hydrophone-channels-match-list-order",
                    "hydrophone-streamers-exist",
                    "streamer-ids-match-list-order",
                },
        "Array Manager defaults/adapter constraints changed");

    require(
        acquisition.at("palette") ==
            Json({
                {"registeredName", "Sound Acquisition"},
                {"aliases", Json::array()},
                {"menuGroup", "Sound Processing"},
                {
                    "tooltip",
                    "Controls input of sound data from sound cards, NI cards, etc.",
                },
            }) &&
            acquisition.at("javaAuthority").at("className") ==
                "Acquisition.AcquisitionControl" &&
            acquisition.at("javaAuthority").at("relationship") ==
                "direct",
        "Acquisition palette/Java authority changed");
    require(
        acquisition.at("status") ==
            Json({
                {"availability", "available"},
                {"parity", "partial"},
            }),
        "Acquisition status changed");
    require(
        acquisition.at("instanceRules") ==
            Json({
                {"minimum", 0},
                {"maximum", nullptr},
                {
                    "allowedModes",
                    Json::array({"normal", "mixed", "viewer"}),
                },
                {"modeOverrides", Json::array()},
            }),
        "Acquisition instance rules changed");
    require(
        acquisition.at("inputs").empty() &&
            acquisition.at("outputs").size() == 1,
        "Acquisition role count changed");
    require(
        semantic_constraint_ids(
            acquisition.at("settings").at("schema")) ==
            std::set<std::string>{
                "calibration-empty-or-covers-channels",
                "hardware-channels-cover-active-channels",
                "hydrophones-cover-active-channels",
                "preamplifier-bandwidth-ordered",
            },
        "Acquisition adapter-level schema constraints changed");
    check_role(
        acquisition.at("outputs").at(0),
        "rawAudio",
        "output",
        "pamguard.raw-audio",
        "1");

    const auto check_signal_routing_type = [](
        const Json& descriptor,
        const std::string& registered_name,
        const std::string& class_name,
        const std::string& output_role,
        const std::string& recipe_id,
        const std::string& child_role,
        const std::string& adapter_id) {
        require(
            descriptor.at("palette").at("registeredName") ==
                    registered_name &&
                descriptor.at("palette").at("menuGroup") ==
                    "Sound Processing" &&
                descriptor.at("javaAuthority").at("className") ==
                    class_name &&
                descriptor.at("status") ==
                    Json({
                        {"availability", "available"},
                        {"parity", "partial"},
                    }) &&
                descriptor.at("inputs").size() == 1 &&
                descriptor.at("outputs").size() == 1,
            registered_name + " catalogue identity/status changed");
        check_role(
            descriptor.at("inputs").at(0),
            "rawAudio",
            "input",
            "pamguard.raw-audio",
            "1");
        require(
            descriptor.at("inputs").at(0).at("defaultProvider") ==
                Json({
                    {
                        "controlledUnitTypeId",
                        "pamguard.acquisition",
                    },
                    {"outputRole", "rawAudio"},
                }),
            registered_name + " default source binding changed");
        check_role(
            descriptor.at("outputs").at(0),
            output_role,
            "output",
            "pamguard.raw-audio",
            "1");
        const auto& recipe = descriptor.at("recipe");
        require(
            recipe.at("id") == recipe_id &&
                recipe.at("children").size() == 1 &&
                recipe.at("children").at(0).at("role") == child_role &&
                recipe.at("children")
                        .at(0)
                        .at("runtimeTypeId") ==
                    descriptor.at("typeId") &&
                recipe.at("children")
                        .at(0)
                        .at("settingsMapping") ==
                    Json({
                        {"sourcePointer", ""},
                        {"adapterId", adapter_id},
                    }) &&
                string_values(
                    recipe.at("publicMappings"),
                    "publicRole") ==
                    std::vector<std::string>{
                        "rawAudio",
                        output_role,
                    },
            registered_name +
                " identity runtime recipe/public mappings changed");
    };
    check_signal_routing_type(
        amplifier,
        "Signal Amplifier",
        "amplifier.AmpControl",
        "amplifiedAudio",
        "pamguard.amplifier.runtime",
        "amplifier-process",
        "identity.v1");
    check_signal_routing_type(
        patch_panel,
        "Patch Panel",
        "patchPanel.PatchPanelControl",
        "patchedAudio",
        "pamguard.patch-panel.runtime",
        "patch-panel-process",
        "identity.v1");
    check_signal_routing_type(
        filter,
        "Filters (IIR and FIR)",
        "Filters.FilterControl",
        "filteredAudio",
        "pamguard.filter.runtime",
        "filter-process",
        "pamguard.standalone-filter-settings.v1");
    check_signal_routing_type(
        decimator,
        "Decimator",
        "decimator.DecimatorControl",
        "decimatedAudio",
        "pamguard.decimator.runtime",
        "decimator-process",
        "pamguard.decimator-settings.v1");
    require(
        amplifier.at("settings").at("defaults")
                .at("channelSettings").size() == 32 &&
            std::all_of(
                amplifier.at("settings")
                    .at("defaults")
                    .at("channelSettings")
                    .begin(),
                amplifier.at("settings")
                    .at("defaults")
                    .at("channelSettings")
                    .end(),
                [](const Json& row) {
                    return row ==
                        Json({
                            {"gainDb", 0.0},
                            {"invert", false},
                        });
                }),
        "Signal Amplifier defaults are not 32 Java-unity channels");
    const auto& patch_defaults =
        patch_panel.at("settings").at("defaults");
    require(
        patch_defaults.at("routingMatrix").size() == 32 &&
            patch_defaults.at("advancedGainMatrix").is_null(),
        "Patch Panel canonical/Advanced defaults changed");
    for (std::size_t input_channel = 0;
         input_channel < 32;
         ++input_channel) {
        require(
            patch_defaults.at("routingMatrix")
                    .at(input_channel).size() == 32,
            "Patch Panel route row length changed");
        for (std::size_t output_channel = 0;
             output_channel < 32;
             ++output_channel) {
            require(
                patch_defaults.at("routingMatrix")
                        .at(input_channel)
                        .at(output_channel) ==
                    (input_channel == output_channel),
                "Patch Panel default is not Java's identity matrix");
        }
    }

    const auto& filter_settings = filter.at("settings");
    const auto& filter_defaults =
        filter_settings.at("defaults");
    require(
        filter_defaults.at("channelBitmap") == 0 &&
            filter_defaults.at("type") == "butterworth" &&
            filter_defaults.at("band") == "bandPass" &&
            filter_defaults.at("order") == 4 &&
            filter_defaults.at("lowPassFreqHz") == 20000 &&
            filter_defaults.at("highPassFreqHz") == 2000 &&
            filter_defaults.at("passBandRippleDb") == 2 &&
            filter_defaults.at("stopBandRippleDb") == 2 &&
            filter_defaults.at("chebyGamma") == 3 &&
            filter_defaults.at("arbitraryFrequenciesHz").empty() &&
            filter_defaults.at("arbitraryGainsDb").empty() &&
            filter_settings.at("schema")
                    .at("additionalProperties") == false &&
            filter_settings.at("schema")
                    .at("x-pamguard-authority")
                    .at("settingsClass") ==
                "Filters.FilterParameters_2" &&
            filter_settings.at("schema")
                    .at("x-pamguard-portable-deviations")
                    .size() == 6,
        "Filter Java defaults or strict portable schema changed");

    const auto& decimator_settings =
        decimator.at("settings");
    const auto& decimator_defaults =
        decimator_settings.at("defaults");
    const auto& decimator_filter =
        decimator_defaults.at("filter");
    require(
        decimator_defaults.at("outputSampleRateHz") == 2000 &&
            decimator_defaults.at("channelBitmap") == 0 &&
            decimator_defaults.at("interpolation") == 0 &&
            decimator_filter.at("type") == "butterworth" &&
            decimator_filter.at("band") == "lowPass" &&
            decimator_filter.at("order") == 6 &&
            decimator_filter.at("lowPassFreqHz") == 1000 &&
            decimator_filter.at("highPassFreqHz") == 2000 &&
            decimator_filter.at("passBandRippleDb") == 2 &&
            decimator_filter.at("stopBandRippleDb") == 2 &&
            decimator_filter.at("chebyGamma") == 3 &&
            decimator_filter.at("arbitraryFrequenciesHz").empty() &&
            decimator_filter.at("arbitraryGainsDb").empty() &&
            decimator_settings.at("schema")
                    .at("additionalProperties") == false &&
            decimator_settings.at("schema")
                    .at("x-pamguard-authority")
                    .at("settingsClass") ==
                "decimator.DecimatorParams" &&
            decimator_settings.at("schema")
                    .at("x-pamguard-portable-deviations")
                    .size() == 8,
        "Decimator Java defaults or strict portable schema changed");

    require(
        fft.at("inputs").size() == 1 &&
            fft.at("outputs").size() == 2,
        "FFT public role count changed");
    require(
        semantic_constraint_ids(
            fft.at("settings").at("schema")) ==
            std::set<std::string>{
                "fft-hop-within-length",
                "fft-length-power-of-two",
            },
        "FFT adapter-level schema constraints changed");
    const auto& fft_input = fft.at("inputs").at(0);
    check_role(
        fft_input,
        "rawAudio",
        "input",
        "pamguard.raw-audio",
        "1");
    require(
        fft_input.at("javaDataClass") ==
                "PamDetection.RawDataUnit" &&
            fft_input.at("defaultProvider") ==
                Json({
                    {
                        "controlledUnitTypeId",
                        "pamguard.acquisition",
                    },
                    {"outputRole", "rawAudio"},
                }),
        "FFT default-provider contract changed");

    const auto check_noise_ltsa_type = [](
        const Json& descriptor,
        const std::string& registered_name,
        const std::string& class_name,
        const std::string& input_role,
        const std::string& input_data_type,
        const std::string& output_role,
        const std::string& output_data_type,
        const std::string& recipe_id,
        const std::string& child_role,
        const std::string& adapter_id) {
        require(
            descriptor.at("palette").at("registeredName") ==
                    registered_name &&
                descriptor.at("palette").at("menuGroup") ==
                    "Sound Processing" &&
                descriptor.at("javaAuthority").at("className") ==
                    class_name &&
                descriptor.at("status") ==
                    Json({
                        {"availability", "available"},
                        {"parity", "partial"},
                    }) &&
                descriptor.at("inputs").size() == 1 &&
                descriptor.at("outputs").size() == 1,
            registered_name + " catalogue identity/status changed");
        check_role(
            descriptor.at("inputs").at(0),
            input_role,
            "input",
            input_data_type,
            "1");
        check_role(
            descriptor.at("outputs").at(0),
            output_role,
            "output",
            output_data_type,
            "1");
        const auto& recipe = descriptor.at("recipe");
        require(
            recipe.at("id") == recipe_id &&
                recipe.at("children").size() == 1 &&
                recipe.at("children").at(0).at("role") ==
                    child_role &&
                recipe.at("children")
                        .at(0)
                        .at("settingsMapping")
                        .at("adapterId") == adapter_id,
            registered_name + " runtime recipe changed");
    };
    check_noise_ltsa_type(
        fft_noise_monitor,
        "Noise Monitor",
        "noiseMonitor.NoiseControl",
        "fft",
        "pamguard.fft",
        "noiseMeasurements",
        "pamguard.fft-noise",
        "pamguard.fft-noise-monitor.runtime",
        "noise-process",
        "pamguard.fft-noise-settings.v1");
    check_noise_ltsa_type(
        noise_band_monitor,
        "Noise Band Monitor",
        "noiseBandMonitor.NoiseBandControl",
        "rawAudio",
        "pamguard.raw-audio",
        "noiseBandMeasurements",
        "pamguard.noise-band",
        "pamguard.noise-band-monitor.runtime",
        "noise-band-process",
        "pamguard.noise-band-settings.v1");
    check_noise_ltsa_type(
        ltsa,
        "Long Term Spectral Average",
        "ltsa.LtsaControl",
        "fft",
        "pamguard.fft",
        "ltsa",
        "pamguard.ltsa",
        "pamguard.ltsa.runtime",
        "ltsa-process",
        "pamguard.ltsa-settings.v1");
    require(
        fft_noise_monitor.at("settings").at("defaults") ==
                Json({
                    {"channelBitmap", 1},
                    {"measurementIntervalSeconds", 60},
                    {"nMeasures", 100},
                    {"useAll", true},
                    {"bands", Json::array()},
                }) &&
            noise_band_monitor.at("settings").at("defaults") ==
                Json({
                    {"channelBitmap", 1},
                    {"bandType", "thirdOctave"},
                    {"filterType", "butterworth"},
                    {"iirOrder", 6},
                    {"firOrder", 7},
                    {"firGamma", 2.5},
                    {"outputIntervalSeconds", 10},
                    {"minimumFrequencyHz", 1.7925856629456591},
                    {"maximumFrequencyHz", 1133.6866687924667},
                    {"referenceFrequencyHz", 1000},
                }) &&
            ltsa.at("settings").at("defaults") ==
                Json({
                    {"channelBitmap", 0},
                    {"intervalSeconds", 60},
                    {"longerFactor", 10},
                }),
        "Noise/LTSA Java-authoritative defaults changed");
    require(
        !noise_band_monitor.at("settings")
             .at("defaults")
             .contains("logFreqScale") &&
            !noise_band_monitor.at("settings")
                 .at("defaults")
                 .contains("showGrid") &&
            ltsa.at("inputs").at(0).at("javaDataClass") ==
                "PamDetection.RawDataUnit",
        "Noise/LTSA science/display separation or LTSA dependency authority changed");

    require(
        whistle_moan.at("palette").at("registeredName") ==
                "Whistle and Moan Detector" &&
            whistle_moan.at("palette").at("menuGroup") ==
                "Detectors" &&
            whistle_moan.at("javaAuthority").at("className") ==
                "whistlesAndMoans.WhistleMoanControl" &&
            whistle_moan.at("status") ==
                Json({
                    {"availability", "available"},
                    {"parity", "partial"},
                }) &&
            whistle_moan.at("inputs").size() == 1 &&
            whistle_moan.at("outputs").size() == 2,
        "Whistle/Moan catalogue identity/status changed");
    check_role(
        whistle_moan.at("inputs").at(0),
        "fft",
        "input",
        "pamguard.fft",
        "1");
    check_role(
        whistle_moan.at("outputs").at(0),
        "noiseReducedFft",
        "output",
        "pamguard.fft",
        "1");
    check_role(
        whistle_moan.at("outputs").at(1),
        "contours",
        "output",
        "pamguard.whistle-contour",
        "1");
    const auto& whistle_input =
        whistle_moan.at("inputs").at(0);
    const auto& whistle_recipe =
        whistle_moan.at("recipe");
    require(
        whistle_input.at("javaDataClass") ==
                "fftManager.FFTDataUnit" &&
            whistle_input.at("defaultProvider") ==
                Json({
                    {
                        "controlledUnitTypeId",
                        "pamguard.fft",
                    },
                    {"outputRole", "fft"},
                }) &&
            whistle_recipe.at("id") ==
                "pamguard.whistles-moans.runtime" &&
            whistle_recipe.at("children").size() == 2 &&
            whistle_recipe.at("children").at(0).at("role") ==
                "noise-reduction" &&
            whistle_recipe.at("children")
                    .at(0)
                    .at("runtimeTypeId") ==
                "pamguard.spectrogram-noise" &&
            whistle_recipe.at("children")
                    .at(0)
                    .at("settingsMapping")
                    .at("adapterId") ==
                "pamguard.whistle-noise-settings.v1" &&
            whistle_recipe.at("children").at(1).at("role") ==
                "contour-connect" &&
            whistle_recipe.at("children")
                    .at(1)
                    .at("runtimeTypeId") ==
                "pamguard.whistles-moans" &&
            whistle_recipe.at("children")
                    .at(1)
                    .at("settingsMapping")
                    .at("adapterId") ==
                "pamguard.whistle-contour-settings.v1" &&
            whistle_recipe.at("internalEdges").size() == 1 &&
            whistle_recipe.at("internalEdges").at(0).at("id") ==
                "noise-to-contours",
        "Whistle/Moan dependency or two-process recipe changed");
    require(
        whistle_moan.at("settings").at("defaults") ==
                Json::parse(
                    pamguard::core::
                        whistle_moan_default_settings_json()) &&
            whistle_moan.at("settings")
                    .at("schema")
                    .at("x-pamguard-authority")
                    .at("commit") ==
                "dca55c81ef6f1498a8a3b926c69e7182afb915ee" &&
            whistle_moan.at("settings")
                    .at("schema")
                    .at("x-pamguard-portable-deviations")
                    .size() >= 8,
        "Whistle/Moan Java defaults or documented deviations changed");

    require(
        sound_output.at("palette").at("registeredName") ==
                "Sound Output" &&
            sound_output.at("palette").at("menuGroup") ==
                "Sound Processing" &&
            sound_output.at("javaAuthority").at("className") ==
                "soundPlayback.PlaybackControl" &&
            sound_output.at("status") ==
                Json({
                    {"availability", "available"},
                    {"parity", "partial"},
                }) &&
            sound_output.at("inputs").size() == 1 &&
            sound_output.at("outputs").empty(),
        "Sound Output catalogue identity/status changed");
    require(
        sound_output.at("instanceRules") ==
            Json({
                {"minimum", 0},
                {"maximum", nullptr},
                {
                    "allowedModes",
                    Json::array({"normal", "mixed", "viewer"}),
                },
                {
                    "modeOverrides",
                    Json::array({
                        {
                            {"mode", "viewer"},
                            {"minimum", 1},
                            {"maximum", 1},
                        },
                    }),
                },
            }),
        "Sound Output Normal/Mixed/Viewer instance rules changed");
    const auto& sound_input =
        sound_output.at("inputs").at(0);
    check_role(
        sound_input,
        "audio",
        "input",
        "pamguard.raw-audio",
        "1");
    require(
        sound_input.at("javaDataClass") ==
                "PamDetection.RawDataUnit" &&
            sound_input.at("defaultProvider").is_null(),
        "Sound Output raw-audio binding contract changed");
    const auto& sound_defaults =
        sound_output.at("settings").at("defaults");
    require(
        sound_defaults.at("channelBitmap") == 0 &&
            sound_defaults.at("defaultSampleRate") == true &&
            sound_defaults.at("playbackRateHz") == 48000 &&
            sound_defaults.at("playbackSpeed") == 1 &&
            sound_defaults.at("playbackGainDb") == 0 &&
            sound_defaults.at("hpFilter") == 0 &&
            !sound_defaults.contains("deviceId") &&
            !sound_defaults.contains("deviceNumber") &&
            !sound_defaults.contains("deviceType"),
        "Sound Output portable defaults or host separation changed");

    require(
        click_detector.at("palette").at("registeredName") ==
                "Click Detector" &&
            click_detector.at("palette").at("menuGroup") ==
                "Detectors" &&
            click_detector.at("javaAuthority").at("className") ==
                "clickDetector.ClickControl" &&
            click_detector.at("status") ==
                Json({
                    {"availability", "available"},
                    {"parity", "partial"},
                }) &&
            click_detector.at("inputs").size() == 1 &&
            click_detector.at("outputs").size() == 9,
        "Click Detector catalogue identity/status changed");
    check_role(
        click_detector.at("inputs").at(0),
        "rawAudio",
        "input",
        "pamguard.raw-audio",
        "1");
    const auto& click_defaults =
        click_detector.at("settings").at("defaults");
    require(
        click_defaults.at("detector").at("channelBitmap") == 3 &&
            click_defaults.at("detector").at("groupingType") ==
                "all" &&
            click_defaults.at("detector").at("thresholdDb") == 10 &&
            click_defaults.at("classification").at("runOnline") ==
                false &&
            click_defaults.at("classification").at("mode") ==
                "sweep" &&
            click_defaults.at("train").at("enabled") == false &&
            click_defaults.at("train").at("minClicks") == 6 &&
            !click_defaults.contains("array") &&
            !click_defaults.at("detector").contains("sourceId"),
        "Click Detector Java defaults or source/Array separation changed");
    const auto& click_recipe =
        click_detector.at("recipe");
    require(
        click_recipe.at("id") ==
                "pamguard.click-detector.runtime" &&
            string_values(click_recipe.at("children"), "role") ==
                std::vector<std::string>{
                    "detector",
                    "classifier",
                    "features",
                    "localiser",
                    "train",
                } &&
            click_recipe.at("internalEdges").size() == 4 &&
            click_recipe.at("contributedDisplayProviderTypeIds") ==
                Json::array({"pamguard.click-display"}),
        "Click Detector hidden runtime recipe changed");
    require(
        mht_click_train.at("palette").at("registeredName") ==
                "Click Train Detector" &&
            mht_click_train.at("palette").at("menuGroup") ==
                "Detectors" &&
            mht_click_train.at("javaAuthority").at("className") ==
                "clickTrainDetector.ClickTrainControl" &&
            mht_click_train.at("status") ==
                Json({
                    {"availability", "available"},
                    {"parity", "partial"},
                }) &&
            mht_click_train.at("inputs").size() == 4 &&
            mht_click_train.at("outputs").size() == 2 &&
            mht_click_train.at("settings").at("defaults") ==
                Json::parse(
                    pamguard::core::
                        mht_click_train_default_settings_json()),
        "MHT Click Train catalogue identity/defaults changed");
    check_role(
        mht_click_train.at("inputs").at(0),
        "clicks",
        "input",
        "pamguard.click",
        "1");
    const auto& mht_recipe =
        mht_click_train.at("recipe");
    require(
        mht_recipe.at("id") ==
                "pamguard.mht-click-train.runtime" &&
            mht_recipe.at("children").size() == 1 &&
            mht_recipe.at("children").at(0).at("role") ==
                "detector" &&
            mht_recipe.at("children")
                    .at(0)
                    .at("runtimeTypeId") ==
                "pamguard.mht-click-train" &&
            mht_recipe.at("children")
                    .at(0)
                    .at("settingsMapping") ==
                Json({
                    {"sourcePointer", ""},
                    {
                        "adapterId",
                        "pamguard.mht-click-train-settings.v1",
                    },
                }),
        "MHT Click Train runtime recipe changed");
    require(
        matched_template.at("palette").at("menuGroup") ==
                "Classifiers" &&
            matched_template.at("palette").at("registeredName") ==
                "Matched Template Click Classifer" &&
            matched_template.at("settings").at("defaults") ==
                Json::parse(
                    pamguard::core::
                        matched_template_default_settings_json()) &&
            matched_template.at("recipe").at("id") ==
                "pamguard.matched-template-classifier.runtime" &&
            matched_template.at("recipe")
                    .at("children")
                    .at(0)
                    .at("settingsMapping")
                    .at("adapterId") ==
                "pamguard.matched-template-settings.v1",
        "Matched Template catalogue identity/defaults changed");
    const auto check_unavailable_utility = [](
        const Json& descriptor,
        std::string_view registered_name,
        std::string_view runtime_type_id) {
        require(
            descriptor.at("palette").at("registeredName") ==
                    std::string(registered_name) &&
                descriptor.at("palette").at("menuGroup") ==
                    "Utilities" &&
                descriptor.at("status") ==
                    Json({
                        {"availability", "unavailable"},
                        {"parity", "experimental"},
                    }) &&
                descriptor.at("recipe").at("children").size() == 1 &&
                descriptor.at("recipe")
                        .at("children")
                        .at(0)
                        .at("runtimeTypeId") ==
                    std::string(runtime_type_id) &&
                descriptor.at("recipe")
                        .at("children")
                        .at(0)
                        .at("status")
                        .at("availability") ==
                    "unavailable",
            std::string(registered_name) +
                " unavailable experimental catalogue boundary changed");
    };
    check_unavailable_utility(
        user_input,
        "User input",
        "pamguard.user-input");
    check_unavailable_utility(
        aural_listening,
        "Aural Listening Form",
        "pamguard.aural-listening");
    check_unavailable_utility(
        alarm,
        "Alarm",
        "pamguard.alarm-event-counter");
    check_unavailable_utility(
        effort,
        "Scroll Effort",
        "pamguard.effort-monitor");
    require(
        user_input.at("inputs").empty() &&
            user_input.at("outputs").size() == 1 &&
            user_input.at("outputs").at(0).at("dataType") ==
                "pamguard.user-input-data" &&
            aural_listening.at("inputs").empty() &&
            aural_listening.at("outputs").size() == 2 &&
            aural_listening.at("outputs").at(0).at("dataType") ==
                "pamguard.listening-effort" &&
            aural_listening.at("outputs").at(1).at("dataType") ==
                "pamguard.thing-heard" &&
            alarm.at("inputs").size() == 1 &&
            alarm.at("inputs").at(0).at("dataType") ==
                "pamguard.data-unit" &&
            alarm.at("outputs").size() == 1 &&
            alarm.at("outputs").at(0).at("dataType") ==
                "pamguard.alarm-state" &&
            effort.at("inputs").empty() &&
            effort.at("outputs").size() == 1 &&
            effort.at("outputs").at(0).at("dataType") ==
                "pamguard.scroll-effort",
        "Unavailable Utilities catalogue exposed generic runtime data");
    const auto& sound_recipe =
        sound_output.at("recipe");
    require(
        sound_recipe.at("id") ==
                "pamguard.sound-output.runtime" &&
            sound_recipe.at("children").size() == 1 &&
            sound_recipe.at("children").at(0).at("role") ==
                "playback-process" &&
            sound_recipe.at("children")
                    .at(0)
                    .at("runtimeTypeId") ==
                "pamguard.sound-output" &&
            sound_recipe.at("children")
                    .at(0)
                    .at("settingsMapping") ==
                Json({
                    {"sourcePointer", ""},
                    {"adapterId", "identity.v1"},
                }),
        "Sound Output runtime recipe changed");
    check_role(
        fft.at("outputs").at(0),
        "fft",
        "output",
        "pamguard.fft",
        "1");
    check_role(
        fft.at("outputs").at(1),
        "noiseReducedFft",
        "output",
        "pamguard.fft",
        "1");

    const auto& acquisition_recipe = acquisition.at("recipe");
    require(
        acquisition_recipe.at("id") ==
                "pamguard.acquisition.runtime" &&
            acquisition_recipe.at("children").size() == 1 &&
            acquisition_recipe.at("children")
                    .at(0)
                    .at("settingsMapping") ==
                Json({
                    {"sourcePointer", ""},
                    {
                        "adapterId",
                        "pamguard.acquisition-settings.v1",
                    },
                }),
        "Acquisition recipe/settings mapping changed");
    require(
        acquisition_recipe.at("publicMappings") ==
            Json::array({
                {
                    {"publicRole", "rawAudio"},
                    {"direction", "output"},
                    {
                        "runtimeEndpoint",
                        {
                            {"childRole", "acquisition"},
                            {"port", "audio"},
                        },
                    },
                },
            }),
        "Acquisition public mapping changed");

    const auto& fft_recipe = fft.at("recipe");
    require(
        string_values(fft_recipe.at("children"), "role") ==
            std::vector<std::string>{
                "fft-process",
                "spectral-noise",
            },
        "FFT runtime child roles changed");
    require(
        fft_recipe.at("children")
                .at(0)
                .at("settingsMapping") ==
            Json({
                {"sourcePointer", "/fft"},
                {"adapterId", "pamguard.fft-settings.v1"},
            }) &&
            fft_recipe.at("children")
                    .at(1)
                    .at("settingsMapping") ==
                Json({
                    {"sourcePointer", "/spectralNoise"},
                    {"adapterId", "identity.v1"},
                }),
        "FFT child settings mappings changed");
    require(
        fft_recipe.at("internalEdges") ==
            Json::array({
                {
                    {"id", "fft-to-spectral-noise"},
                    {
                        "source",
                        {
                            {"childRole", "fft-process"},
                            {"port", "fft"},
                        },
                    },
                    {
                        "target",
                        {
                            {"childRole", "spectral-noise"},
                            {"port", "input"},
                        },
                    },
                },
            }),
        "FFT internal edge changed");
    require(
        string_values(
            fft_recipe.at("publicMappings"),
            "publicRole") ==
            std::vector<std::string>{
                "rawAudio",
                "fft",
                "noiseReducedFft",
            },
        "FFT public mappings changed");

    require(
        user_display.at("inputs").empty() &&
            user_display.at("outputs").empty() &&
            user_display.at("recipe").at("children").empty() &&
            user_display.at("recipe")
                    .at("contributedDisplayProviderTypeIds") ==
                Json::array({"pamguard.spectrogram-display"}),
        "User Display ownership recipe changed");
    require(
        user_display.at("settings").at("defaults") ==
                Json::object() &&
            user_display.at("settings")
                    .at("schema")
                    .at("maxProperties") == 0,
        "User Display must accept only an empty settings object");

    require(
        spectrogram.at("ownerControlledUnitTypeId") ==
                "pamguard.user-display" &&
            spectrogram.at("name") == "Spectrogram Display" &&
            spectrogram.at("javaAuthority").at("providerClass") ==
                "Spectrogram.SpectrogramDiplayProvider" &&
            spectrogram.at("instanceRules") ==
                Json({
                    {"minimum", 0},
                    {"maximum", nullptr},
                    {"canCreateWithoutSource", true},
                }),
        "Spectrogram provider identity/instance rules changed");
    require(
        spectrogram.at("inputs").size() == 1 &&
            spectrogram.at("outputs").empty(),
        "Spectrogram provider roles changed");
    require(
        semantic_constraint_ids(
            spectrogram.at("settings").at("schema")) ==
            std::set<std::string>{
                "amplitude-limits-ordered",
                "frequency-limits-ordered",
                "panel-channel-count",
            },
        "Spectrogram adapter-level schema constraints changed");
    require(
        spectrogram.at("settings").at("version") == 2 &&
            spectrogram.at("settings").at("defaults") ==
                Json({
                    {"nPanels", 1},
                    {"channelList", Json::array({0})},
                    {"frequencyLimits", Json::array({0, 0})},
                    {"amplitudeLimits", Json::array({50, 120})},
                    {"colourMap", "GREY"},
                    {"wrapDisplay", true},
                    {"timeScaleFixed", false},
                    {"displayLength", 20},
                    {"pixelsPerSlics", 1},
                    {"showScale", true},
                }) &&
            !spectrogram.at("settings")
                 .at("schema")
                 .at("properties")
                 .contains("sourceName"),
        "Spectrogram portable settings must not duplicate source authority");
    const auto& display_input = spectrogram.at("inputs").at(0);
    check_role(
        display_input,
        "fft",
        "input",
        "pamguard.fft",
        "0..1");
    require(
        display_input.at("defaultProvider") ==
            Json({
                {"controlledUnitTypeId", "pamguard.fft"},
                {"outputRole", "fft"},
            }),
        "Spectrogram default-provider contract changed");
    require(
        spectrogram.at("lowLevelCompatibility") ==
            Json({
                {"runtimeTypeId", "pamguard.spectrogram-display"},
                {
                    "publicMappings",
                    Json::array({
                        {
                            {"publicRole", "fft"},
                            {"lowLevelPort", "fft"},
                        },
                    }),
                },
            }),
        "Spectrogram low-level compatibility mapping changed");

    require(
        click_display.at("ownerControlledUnitTypeId") ==
                "pamguard.click-detector" &&
            click_display.at("name") == "Click Display" &&
            click_display.at("javaAuthority").at("componentClass") ==
                "clickDetector.ClickBTDisplay" &&
            click_display.at("instanceRules") ==
                Json({
                    {"minimum", 1},
                    {"maximum", 1},
                    {"canCreateWithoutSource", false},
                }) &&
            click_display.at("inputs").size() == 1 &&
            click_display.at("outputs").empty(),
        "Click display ownership/instance contract changed");
    check_role(
        click_display.at("inputs").at(0),
        "clicks",
        "input",
        "pamguard.click",
        "1");

    for (const auto* descriptor :
         {&acquisition, &amplifier, &fft, &click_detector,
          &user_display}) {
        check_settings_contract(
            descriptor->at("settings"),
            descriptor->at("typeId").get<std::string>());
        require(
            descriptor->at("help")
                    .at("sourceReferences")
                    .is_array() &&
                !descriptor->at("help")
                     .at("sourceReferences")
                     .empty() &&
                descriptor->at("help").at("point").is_string(),
            descriptor->at("typeId").get<std::string>() +
                " omitted help/source evidence");
    }
    check_settings_contract(
        patch_panel.at("settings"),
        "pamguard.patch-panel");
    require(
        patch_panel.at("help").at("point").is_null() &&
            patch_panel.at("help")
                    .at("sourceReferences")
                    .is_array() &&
            !patch_panel.at("help")
                     .at("sourceReferences")
                     .empty(),
        "Patch Panel Java source evidence/help absence changed");
    check_settings_contract(
        spectrogram.at("settings"),
        "pamguard.spectrogram-display",
        "stop-required",
        2);
    check_settings_contract(
        sound_output.at("settings"),
        "pamguard.sound-output",
        "live-safe");
    check_settings_contract(
        click_display.at("settings"),
        "pamguard.click-display",
        "live-safe");
    check_settings_contract(
        array_manager.at("settings"),
        "pamguard.array-manager");
    require(
        spectrogram.at("help")
                .at("sourceReferences")
                .is_array() &&
            !spectrogram.at("help")
                 .at("sourceReferences")
                 .empty(),
        "Spectrogram omitted source evidence");

    std::set<std::string> provider_ids;
    for (const auto& provider : providers) {
        provider_ids.insert(
            provider.at("providerTypeId").get<std::string>());
    }
    for (const auto& unit : units) {
        for (const auto& child :
             unit.at("recipe").at("children")) {
            require(
                !provider_ids.contains(
                    child.at("runtimeTypeId").get<std::string>()),
                "Presentation provider leaked into runtime children");
        }
    }
}

void check_rejection() {
    ControlledUnitRegistry source;
    pamguard::project::register_builtin_controlled_units(source);
    const auto* acquisition =
        source.find_controlled_unit("pamguard.acquisition");
    require(acquisition != nullptr, "Missing Acquisition fixture");

    ControlledUnitRegistry open_schema;
    auto descriptor = *acquisition;
    descriptor.settings.settings_schema_json =
        R"({"type":"object","properties":{},"additionalProperties":true})";
    open_schema.register_controlled_unit(std::move(descriptor));
    bool rejected = false;
    try {
        static_cast<void>(
            pamguard::project::controlled_unit_catalogue_to_json(
                open_schema));
    }
    catch (const ControlledUnitJsonError&) {
        rejected = true;
    }
    require(
        rejected,
        "Catalogue accepted an open object settings schema");

    ControlledUnitRegistry bad_defaults;
    descriptor = *acquisition;
    descriptor.settings.default_settings_json =
        R"({"unexpected":true})";
    bad_defaults.register_controlled_unit(std::move(descriptor));
    rejected = false;
    try {
        static_cast<void>(
            pamguard::project::controlled_unit_catalogue_to_json(
                bad_defaults));
    }
    catch (const ControlledUnitJsonError&) {
        rejected = true;
    }
    require(
        rejected,
        "Catalogue accepted defaults that do not conform to the schema");

    ControlledUnitRegistry missing_recipe_id;
    descriptor = *acquisition;
    descriptor.runtime_recipe.id.clear();
    missing_recipe_id.register_controlled_unit(
        std::move(descriptor));
    require(
        has_issue(
            missing_recipe_id.validate(),
            "invalid-recipe-id"),
        "Registry accepted an empty persisted recipe ID");

    ControlledUnitRegistry duplicate_recipe_id;
    descriptor = *acquisition;
    duplicate_recipe_id.register_controlled_unit(descriptor);
    descriptor.id = "pamguard.acquisition-copy";
    duplicate_recipe_id.register_controlled_unit(
        std::move(descriptor));
    require(
        has_issue(
            duplicate_recipe_id.validate(),
            "duplicate-recipe-id"),
        "Registry accepted duplicate persisted recipe IDs");
}

} // namespace

int main() {
    try {
        ControlledUnitRegistry registry;
        pamguard::project::register_builtin_controlled_units(registry);

        const auto compact =
            pamguard::project::controlled_unit_catalogue_to_json(
                registry);
        const auto repeated =
            pamguard::project::controlled_unit_catalogue_to_json(
                registry);
        const auto pretty =
            pamguard::project::controlled_unit_catalogue_to_json(
                registry,
                true);
        require(
            compact == repeated,
            "Compact catalogue serialization is not deterministic");

        const auto document = Json::parse(compact);
        require(
            Json::parse(pretty) == document,
            "Pretty catalogue serialization changed its values");
        check_catalogue(document);
        check_rejection();

        std::cout
            << "Controlled-unit catalogue JSON contract covers "
            << document.at("controlledUnitTypes").size()
            << " controlled units and "
            << document.at("displayProviderTypes").size()
            << " display provider plus "
            << document.at("globalSettingsTypes").size()
            << " global settings descriptor\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
