#include "pamguard/project/ControlledUnitJson.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <json.hpp>

#include "CanonicalJson.h"

namespace pamguard::project {

namespace {

using Json = nlohmann::json;

[[noreturn]] void fail(
    std::string_view context,
    const std::string& message) {
    throw ControlledUnitJsonError(
        std::string(context) + ": " + message);
}

std::string child_path(
    std::string_view path,
    std::string_view child) {
    if (path.empty()) {
        return "/" + std::string(child);
    }
    return std::string(path) + "/" + std::string(child);
}

std::string index_path(
    std::string_view path,
    std::size_t index) {
    return std::string(path) + "/" + std::to_string(index);
}

Json parse_embedded_object(
    std::string_view encoded,
    std::string_view context) {
    try {
        auto value = detail::parse_strict_json(
            encoded,
            context,
            detail::kMaximumEmbeddedSettingsBytes,
            detail::kMaximumEmbeddedSettingsDepth);
        if (!value.is_object()) {
            fail(context, "must encode a JSON object");
        }
        return value;
    }
    catch (const ControlledUnitJsonError&) {
        throw;
    }
    catch (const std::exception& error) {
        fail(context, error.what());
    }
}

bool schema_allows_type(
    const Json& schema,
    std::string_view type) {
    const auto found = schema.find("type");
    if (found == schema.end()) {
        return true;
    }
    if (found->is_string()) {
        return found->get_ref<const std::string&>() == type;
    }
    if (!found->is_array()) {
        return false;
    }
    return std::any_of(
        found->begin(),
        found->end(),
        [&](const Json& candidate) {
            return candidate.is_string() &&
                candidate.get_ref<const std::string&>() == type;
        });
}

void validate_schema_shape(
    const Json& schema,
    std::string_view path,
    const Json& root_schema) {
    if (!schema.is_object()) {
        fail(path, "schema node must be an object");
    }

    const auto reference = schema.find("$ref");
    if (reference != schema.end()) {
        if (!reference->is_string()) {
            fail(child_path(path, "$ref"), "must be a string");
        }
        const auto& encoded =
            reference->get_ref<const std::string&>();
        if (!encoded.starts_with("#/")) {
            fail(
                child_path(path, "$ref"),
                "only document-local schema references are supported");
        }
        const Json::json_pointer pointer(encoded.substr(1));
        if (!root_schema.contains(pointer) ||
            !root_schema.at(pointer).is_object()) {
            fail(
                child_path(path, "$ref"),
                "schema reference does not resolve to an object");
        }
    }

    if (schema_allows_type(schema, "object")) {
        const auto declared_type = schema.find("type");
        const bool explicitly_object =
            declared_type != schema.end() &&
            ((declared_type->is_string() &&
              declared_type->get_ref<const std::string&>() == "object") ||
             (declared_type->is_array() &&
              std::any_of(
                  declared_type->begin(),
                  declared_type->end(),
                  [](const Json& type) {
                      return type.is_string() &&
                          type.get_ref<const std::string&>() == "object";
                  })));
        if (explicitly_object) {
            const auto additional =
                schema.find("additionalProperties");
            if (additional == schema.end() ||
                !additional->is_boolean() ||
                additional->get<bool>()) {
                fail(
                    path,
                    "every object schema must set additionalProperties to false");
            }
        }
    }

    const auto properties = schema.find("properties");
    if (properties != schema.end()) {
        if (!properties->is_object()) {
            fail(child_path(path, "properties"), "must be an object");
        }
        for (auto property = properties->begin();
             property != properties->end();
             ++property) {
            validate_schema_shape(
                property.value(),
                child_path(
                    child_path(path, "properties"),
                    property.key()),
                root_schema);
        }
    }

    const auto items = schema.find("items");
    if (items != schema.end()) {
        validate_schema_shape(
            *items,
            child_path(path, "items"),
            root_schema);
    }

    const auto definitions = schema.find("$defs");
    if (definitions != schema.end()) {
        if (!definitions->is_object()) {
            fail(child_path(path, "$defs"), "must be an object");
        }
        for (auto definition = definitions->begin();
             definition != definitions->end();
             ++definition) {
            validate_schema_shape(
                definition.value(),
                child_path(
                    child_path(path, "$defs"),
                    definition.key()),
                root_schema);
        }
    }
}

std::string json_type_name(const Json& value) {
    if (value.is_null()) {
        return "null";
    }
    if (value.is_boolean()) {
        return "boolean";
    }
    if (value.is_number_integer() ||
        value.is_number_unsigned()) {
        return "integer";
    }
    if (value.is_number_float()) {
        return "number";
    }
    if (value.is_string()) {
        return "string";
    }
    if (value.is_array()) {
        return "array";
    }
    if (value.is_object()) {
        return "object";
    }
    return "unknown";
}

bool value_matches_schema_type(
    const Json& value,
    std::string_view type) {
    if (type == "null") {
        return value.is_null();
    }
    if (type == "boolean") {
        return value.is_boolean();
    }
    if (type == "integer") {
        return value.is_number_integer() ||
            value.is_number_unsigned();
    }
    if (type == "number") {
        return value.is_number();
    }
    if (type == "string") {
        return value.is_string();
    }
    if (type == "array") {
        return value.is_array();
    }
    if (type == "object") {
        return value.is_object();
    }
    return false;
}

bool value_matches_declared_type(
    const Json& value,
    const Json& declared_type) {
    if (declared_type.is_string()) {
        return value_matches_schema_type(
            value,
            declared_type.get_ref<const std::string&>());
    }
    if (!declared_type.is_array()) {
        return false;
    }
    return std::any_of(
        declared_type.begin(),
        declared_type.end(),
        [&](const Json& type) {
            return type.is_string() &&
                value_matches_schema_type(
                    value,
                    type.get_ref<const std::string&>());
        });
}

double schema_number(
    const Json& schema,
    std::string_view keyword,
    std::string_view path) {
    const auto& value = schema.at(std::string(keyword));
    if (!value.is_number()) {
        fail(child_path(path, keyword), "must be a number");
    }
    return value.get<double>();
}

void validate_value_against_schema(
    const Json& value,
    const Json& schema,
    std::string_view path,
    const Json& root_schema) {
    const auto reference = schema.find("$ref");
    if (reference != schema.end()) {
        if (!reference->is_string()) {
            fail(child_path(path, "$ref"), "must be a string");
        }
        const auto& encoded =
            reference->get_ref<const std::string&>();
        if (!encoded.starts_with("#/")) {
            fail(
                child_path(path, "$ref"),
                "only document-local schema references are supported");
        }
        const Json::json_pointer pointer(encoded.substr(1));
        if (!root_schema.contains(pointer) ||
            !root_schema.at(pointer).is_object()) {
            fail(
                child_path(path, "$ref"),
                "schema reference does not resolve to an object");
        }
        validate_value_against_schema(
            value,
            root_schema.at(pointer),
            path,
            root_schema);
        return;
    }

    const auto declared_type = schema.find("type");
    if (declared_type != schema.end() &&
        !value_matches_declared_type(value, *declared_type)) {
        fail(
            path,
            "default value type '" + json_type_name(value) +
                "' is not allowed by its schema");
    }

    const auto enumeration = schema.find("enum");
    if (enumeration != schema.end()) {
        if (!enumeration->is_array() ||
            std::find(
                enumeration->begin(),
                enumeration->end(),
                value) == enumeration->end()) {
            fail(path, "default value is not in the schema enum");
        }
    }

    if (value.is_object()) {
        const auto properties = schema.find("properties");
        if (properties == schema.end() ||
            !properties->is_object()) {
            if (!value.empty()) {
                fail(path, "schema has no declared object properties");
            }
        }
        else {
            for (auto property = value.begin();
                 property != value.end();
                 ++property) {
                const auto property_schema =
                    properties->find(property.key());
                if (property_schema == properties->end()) {
                    fail(
                        child_path(path, property.key()),
                        "default contains an undeclared property");
                }
                validate_value_against_schema(
                    property.value(),
                    *property_schema,
                    child_path(path, property.key()),
                    root_schema);
            }
        }

        const auto required = schema.find("required");
        if (required != schema.end()) {
            if (!required->is_array()) {
                fail(child_path(path, "required"), "must be an array");
            }
            for (const auto& name : *required) {
                if (!name.is_string()) {
                    fail(
                        child_path(path, "required"),
                        "entries must be strings");
                }
                const auto& key =
                    name.get_ref<const std::string&>();
                if (!value.contains(key)) {
                    fail(
                        child_path(path, key),
                        "required default property is missing");
                }
            }
        }
    }

    if (value.is_array()) {
        if (schema.contains("minItems") &&
            value.size() <
                schema.at("minItems").get<std::size_t>()) {
            fail(path, "default array has too few items");
        }
        if (schema.contains("maxItems") &&
            value.size() >
                schema.at("maxItems").get<std::size_t>()) {
            fail(path, "default array has too many items");
        }
        const auto items = schema.find("items");
        if (items != schema.end()) {
            for (std::size_t index = 0;
                 index < value.size();
                 ++index) {
                validate_value_against_schema(
                    value[index],
                    *items,
                    index_path(path, index),
                    root_schema);
            }
        }
    }

    if (value.is_string() && schema.contains("minLength")) {
        const auto minimum =
            schema.at("minLength").get<std::size_t>();
        try {
            if (detail::utf8_scalar_count(
                    value.get_ref<const std::string&>(),
                    path) < minimum) {
                fail(path, "default string is shorter than minLength");
            }
        }
        catch (const ControlledUnitJsonError&) {
            throw;
        }
        catch (const std::exception& error) {
            fail(path, error.what());
        }
    }

    if (value.is_number()) {
        const auto number = value.get<double>();
        if (schema.contains("minimum") &&
            number < schema_number(schema, "minimum", path)) {
            fail(path, "default number is below minimum");
        }
        if (schema.contains("maximum") &&
            number > schema_number(schema, "maximum", path)) {
            fail(path, "default number is above maximum");
        }
        if (schema.contains("exclusiveMinimum") &&
            number <=
                schema_number(schema, "exclusiveMinimum", path)) {
            fail(path, "default number is not above exclusiveMinimum");
        }
        if (schema.contains("exclusiveMaximum") &&
            number >=
                schema_number(schema, "exclusiveMaximum", path)) {
            fail(path, "default number is not below exclusiveMaximum");
        }
        if (schema.contains("multipleOf")) {
            const auto divisor =
                schema_number(schema, "multipleOf", path);
            if (divisor <= 0 ||
                std::abs(std::remainder(number, divisor)) >
                    std::numeric_limits<double>::epsilon() *
                        std::max(1.0, std::abs(number)) * 8.0) {
                fail(path, "default number is not a schema multiple");
            }
        }
    }
}

Json optional_size_json(
    const std::optional<std::size_t>& value) {
    return value ? Json(*value) : Json(nullptr);
}

Json string_array_json(
    const std::vector<std::string>& values) {
    Json result = Json::array();
    for (const auto& value : values) {
        result.push_back(value);
    }
    return result;
}

Json modes_json(const std::vector<RunMode>& modes) {
    Json result = Json::array();
    for (const auto mode : modes) {
        result.push_back(to_string(mode));
    }
    return result;
}

Json mode_overrides_json(
    const std::vector<RunModeInstanceRulesOverrideDescriptor>&
        overrides) {
    Json result = Json::array();
    for (const auto& override_rules : overrides) {
        result.push_back({
            {"mode", to_string(override_rules.mode)},
            {"minimum", override_rules.minimum_instances},
            {
                "maximum",
                optional_size_json(
                    override_rules.maximum_instances),
            },
        });
    }
    return result;
}

bool contains_capabilities(
    const std::vector<std::string>& available,
    const std::vector<std::string>& required) {
    return std::all_of(
        required.begin(),
        required.end(),
        [&](const auto& capability) {
            return std::find(
                       available.begin(),
                       available.end(),
                       capability) != available.end();
        });
}

Json default_provider_json(
    const ControlledUnitRegistry& registry,
    const PublicDataRoleDescriptor& role) {
    if (!role.default_provider_controlled_unit_type_id) {
        return nullptr;
    }
    const auto* provider = registry.find_controlled_unit(
        *role.default_provider_controlled_unit_type_id);
    if (!provider) {
        fail(
            child_path("/roles", role.id),
            "default controlled-unit provider is missing");
    }
    const auto output = std::find_if(
        provider->public_roles.begin(),
        provider->public_roles.end(),
        [&](const auto& candidate) {
            return
                candidate.direction == DataRoleDirection::Output &&
                candidate.data_type == role.data_type &&
                contains_capabilities(
                    candidate.capabilities,
                    role.capabilities);
        });
    if (output == provider->public_roles.end()) {
        fail(
            child_path("/roles", role.id),
            "default controlled-unit provider has no compatible output");
    }
    return {
        {
            "controlledUnitTypeId",
            *role.default_provider_controlled_unit_type_id,
        },
        {"outputRole", output->id},
    };
}

Json role_json(
    const ControlledUnitRegistry& registry,
    const PublicDataRoleDescriptor& role) {
    return {
        {"id", role.id},
        {"name", role.name},
        {"direction", to_string(role.direction)},
        {"dataType", role.data_type},
        {"cardinality", to_string(role.cardinality)},
        {"capabilities", string_array_json(role.capabilities)},
        {
            "javaDataClass",
            role.java_data_class.empty()
                ? Json(nullptr)
                : Json(role.java_data_class),
        },
        {"defaultProvider", default_provider_json(registry, role)},
    };
}

Json roles_json(
    const ControlledUnitRegistry& registry,
    const std::vector<PublicDataRoleDescriptor>& roles,
    DataRoleDirection direction) {
    Json result = Json::array();
    for (const auto& role : roles) {
        if (role.direction == direction) {
            result.push_back(role_json(registry, role));
        }
    }
    return result;
}

Json settings_json(
    const SettingsDescriptor& descriptor,
    std::string_view owner_path) {
    const auto settings_path =
        child_path(owner_path, "settings");
    auto schema = parse_embedded_object(
        descriptor.settings_schema_json,
        child_path(settings_path, "schema"));
    const auto declared_type = schema.find("type");
    if (declared_type == schema.end() ||
        !declared_type->is_string() ||
        declared_type->get_ref<const std::string&>() != "object") {
        fail(
            child_path(settings_path, "schema/type"),
            "root settings schema must declare type object");
    }
    validate_schema_shape(
        schema,
        child_path(settings_path, "schema"),
        schema);

    auto defaults = parse_embedded_object(
        descriptor.default_settings_json,
        child_path(settings_path, "defaults"));
    validate_value_against_schema(
        defaults,
        schema,
        child_path(settings_path, "defaults"),
        schema);

    Json sections = Json::array();
    for (const auto& section : descriptor.sections) {
        sections.push_back({
            {"surface", section.surface},
            {"labels", string_array_json(section.labels)},
        });
    }

    Json default_evidence = Json::array();
    for (const auto& evidence : descriptor.defaults) {
        Json entry = {
            {"pointer", evidence.pointer},
            {"authorityPath", evidence.authority_path},
            {
                "condition",
                evidence.condition.empty()
                    ? Json(nullptr)
                    : Json(evidence.condition),
            },
            {"authority", evidence.authority},
        };
        if (evidence.value_json) {
            try {
                entry["value"] = detail::parse_strict_json(
                    *evidence.value_json,
                    child_path(
                        child_path(
                            settings_path,
                            "defaultEvidence"),
                        evidence.pointer),
                    detail::kMaximumEmbeddedSettingsBytes,
                    detail::kMaximumEmbeddedSettingsDepth);
            }
            catch (const std::exception& error) {
                fail(
                    child_path(
                        child_path(
                            settings_path,
                            "defaultEvidence"),
                        evidence.pointer),
                    error.what());
            }
        }
        else {
            entry["valueSource"] = evidence.value_source;
        }
        default_evidence.push_back(std::move(entry));
    }

    return {
        {"version", descriptor.version},
        {"schema", std::move(schema)},
        {"defaults", std::move(defaults)},
        {"sections", std::move(sections)},
        {"defaultEvidence", std::move(default_evidence)},
        {
            "changeRules",
            Json::array({
                {
                    {"pointer", ""},
                    {
                        "policy",
                        to_string(
                            descriptor.whole_tree_change_policy),
                    },
                },
            }),
        },
        {
            "javaAuthority",
            {
                {
                    "classes",
                    string_array_json(
                        descriptor.authority_classes),
                },
                {
                    "sourceReferences",
                    string_array_json(
                        descriptor.authority_sources),
                },
            },
        },
        {
            "status",
            {
                {"parity", descriptor.parity_status},
            },
        },
    };
}

Json endpoint_json(const RuntimeEndpointDescriptor& endpoint) {
    return {
        {"childRole", endpoint.child_role_id},
        {"port", endpoint.port_id},
    };
}

const PublicDataRoleDescriptor* find_role(
    const ControlledUnitDescriptor& descriptor,
    std::string_view role_id) {
    const auto found = std::find_if(
        descriptor.public_roles.begin(),
        descriptor.public_roles.end(),
        [&](const auto& role) { return role.id == role_id; });
    return found == descriptor.public_roles.end()
        ? nullptr
        : &*found;
}

Json recipe_json(const ControlledUnitDescriptor& descriptor) {
    if (descriptor.runtime_recipe.id.empty()) {
        fail(
            child_path(
                child_path(
                    "/controlledUnitTypes",
                    descriptor.id),
                "recipe/id"),
            "stable expansion-recipe identity is required");
    }

    Json children = Json::array();
    for (const auto& child : descriptor.runtime_recipe.children) {
        children.push_back({
            {"role", child.role_id},
            {"runtimeTypeId", child.runtime_type_id},
            {"hidden", child.hidden},
            {
                "settingsMapping",
                {
                    {"sourcePointer", child.settings.source_pointer},
                    {"adapterId", child.settings.adapter_id},
                },
            },
            {
                "status",
                {
                    {"availability", to_string(child.availability)},
                    {"parity", child.parity_status},
                },
            },
        });
    }

    Json public_mappings = Json::array();
    for (const auto& mapping :
         descriptor.runtime_recipe.public_role_mappings) {
        const auto* role =
            find_role(descriptor, mapping.public_role_id);
        if (!role) {
            fail(
                child_path(
                    child_path(
                        "/controlledUnitTypes",
                        descriptor.id),
                    "recipe/publicMappings"),
                "mapping refers to an unknown public role");
        }
        public_mappings.push_back({
            {"publicRole", mapping.public_role_id},
            {"direction", to_string(role->direction)},
            {
                "runtimeEndpoint",
                endpoint_json(mapping.runtime_endpoint),
            },
        });
    }

    Json internal_edges = Json::array();
    for (const auto& edge :
         descriptor.runtime_recipe.internal_edges) {
        internal_edges.push_back({
            {"id", edge.id},
            {"source", endpoint_json(edge.source)},
            {"target", endpoint_json(edge.target)},
        });
    }

    return {
        {"id", descriptor.runtime_recipe.id},
        {"version", descriptor.runtime_recipe.version},
        {"children", std::move(children)},
        {"publicMappings", std::move(public_mappings)},
        {"internalEdges", std::move(internal_edges)},
        {
            "contributedDisplayProviderTypeIds",
            string_array_json(
                descriptor.runtime_recipe.display_provider_ids),
        },
    };
}

Json controlled_unit_json(
    const ControlledUnitRegistry& registry,
    const ControlledUnitDescriptor& descriptor) {
    const auto descriptor_path =
        child_path("/controlledUnitTypes", descriptor.id);
    return {
        {"typeId", descriptor.id},
        {"descriptorVersion", descriptor.descriptor_version},
        {
            "palette",
            {
                {
                    "registeredName",
                    descriptor.java_authority.registered_name,
                },
                {"aliases", Json::array()},
                {"menuGroup", descriptor.java_authority.menu_group},
                {"tooltip", descriptor.java_authority.tooltip},
            },
        },
        {
            "javaAuthority",
            {
                {
                    "registeredName",
                    descriptor.java_authority.registered_name,
                },
                {"menuGroup", descriptor.java_authority.menu_group},
                {"className", descriptor.java_authority.class_name},
                {
                    "relationship",
                    descriptor.java_authority.relationship,
                },
            },
        },
        {
            "status",
            {
                {"availability", to_string(descriptor.availability)},
                {"parity", descriptor.parity_status},
            },
        },
        {
            "instanceRules",
            {
                {
                    "minimum",
                    descriptor.instance_rules.minimum_instances,
                },
                {
                    "maximum",
                    optional_size_json(
                        descriptor.instance_rules.maximum_instances),
                },
                {
                    "allowedModes",
                    modes_json(descriptor.instance_rules.allowed_modes),
                },
                {
                    "modeOverrides",
                    mode_overrides_json(
                        descriptor.instance_rules.mode_overrides),
                },
            },
        },
        {
            "inputs",
            roles_json(
                registry,
                descriptor.public_roles,
                DataRoleDirection::Input),
        },
        {
            "outputs",
            roles_json(
                registry,
                descriptor.public_roles,
                DataRoleDirection::Output),
        },
        {
            "settings",
            settings_json(descriptor.settings, descriptor_path),
        },
        {"recipe", recipe_json(descriptor)},
        {
            "help",
            {
                {
                    "point",
                    descriptor.java_authority.help_point.empty()
                        ? Json(nullptr)
                        : Json(descriptor.java_authority.help_point),
                },
                {
                    "sourceReferences",
                    string_array_json(
                        descriptor.java_authority.source_references),
                },
            },
        },
    };
}

Json display_provider_json(
    const ControlledUnitRegistry& registry,
    const DisplayProviderDescriptor& descriptor) {
    const auto descriptor_path =
        child_path("/displayProviderTypes", descriptor.id);

    Json mappings = Json::array();
    for (const auto& mapping :
         descriptor.low_level_compatibility.public_role_mappings) {
        mappings.push_back({
            {"publicRole", mapping.public_role_id},
            {"lowLevelPort", mapping.low_level_port_id},
        });
    }

    return {
        {"providerTypeId", descriptor.id},
        {"descriptorVersion", descriptor.descriptor_version},
        {"name", descriptor.provider_name},
        {
            "ownerControlledUnitTypeId",
            descriptor.owner_controlled_unit_type_id,
        },
        {
            "javaAuthority",
            {
                {"providerClass", descriptor.java_provider_class},
                {"componentClass", descriptor.java_component_class},
            },
        },
        {
            "status",
            {
                {"availability", to_string(descriptor.availability)},
                {"parity", descriptor.parity_status},
            },
        },
        {
            "instanceRules",
            {
                {"minimum", descriptor.minimum_instances},
                {
                    "maximum",
                    optional_size_json(descriptor.maximum_instances),
                },
                {
                    "canCreateWithoutSource",
                    descriptor.can_create_without_source,
                },
            },
        },
        {
            "inputs",
            roles_json(
                registry,
                descriptor.public_roles,
                DataRoleDirection::Input),
        },
        {
            "outputs",
            roles_json(
                registry,
                descriptor.public_roles,
                DataRoleDirection::Output),
        },
        {
            "settings",
            settings_json(descriptor.settings, descriptor_path),
        },
        {
            "lowLevelCompatibility",
            {
                {
                    "runtimeTypeId",
                    descriptor.low_level_compatibility.low_level_type_id,
                },
                {"publicMappings", std::move(mappings)},
            },
        },
        {
            "help",
            {
                {"point", nullptr},
                {
                    "sourceReferences",
                    string_array_json(descriptor.source_references),
                },
            },
        },
    };
}

Json global_settings_json(
    const GlobalSettingsDescriptor& descriptor) {
    const auto descriptor_path =
        child_path("/globalSettingsTypes", descriptor.id);
    return {
        {"typeId", descriptor.id},
        {"name", descriptor.name},
        {"adapterId", descriptor.adapter_id},
        {"required", descriptor.required},
        {
            "javaAuthority",
            {
                {
                    "registeredName",
                    descriptor.java_authority.registered_name,
                },
                {
                    "className",
                    descriptor.java_authority.class_name,
                },
                {
                    "relationship",
                    descriptor.java_authority.relationship,
                },
            },
        },
        {
            "status",
            {
                {"availability", to_string(descriptor.availability)},
                {"parity", descriptor.parity_status},
            },
        },
        {
            "settings",
            settings_json(descriptor.settings, descriptor_path),
        },
        {
            "help",
            {
                {
                    "point",
                    descriptor.java_authority.help_point.empty()
                        ? Json(nullptr)
                        : Json(descriptor.java_authority.help_point),
                },
                {
                    "sourceReferences",
                    string_array_json(
                        descriptor.java_authority.source_references),
                },
            },
        },
    };
}

Json catalogue_json(const ControlledUnitRegistry& registry) {
    const auto validation = registry.validate();
    if (!validation.valid()) {
        const auto& issue = validation.issues.front();
        fail(
            "/",
            "invalid controlled-unit registry [" + issue.code +
                "] " + issue.descriptor_id + ": " + issue.message);
    }

    Json controlled_units = Json::array();
    for (const auto& descriptor : registry.controlled_units()) {
        controlled_units.push_back(
            controlled_unit_json(registry, descriptor));
    }

    Json display_providers = Json::array();
    for (const auto& descriptor : registry.display_providers()) {
        display_providers.push_back(
            display_provider_json(registry, descriptor));
    }

    Json global_settings = Json::array();
    for (const auto& descriptor : registry.global_settings()) {
        global_settings.push_back(
            global_settings_json(descriptor));
    }

    return {
        {
            "schemaVersion",
            kControlledUnitCatalogueSchemaVersion,
        },
        {
            "descriptorSet",
            {
                {"id", kControlledUnitDescriptorSetId},
                {"version", kControlledUnitDescriptorSetVersion},
                {"authorityCommit", kControlledUnitAuthorityCommit},
            },
        },
        {"controlledUnitTypes", std::move(controlled_units)},
        {"displayProviderTypes", std::move(display_providers)},
        {"globalSettingsTypes", std::move(global_settings)},
    };
}

} // namespace

std::string controlled_unit_catalogue_to_json(
    const ControlledUnitRegistry& registry,
    bool pretty) {
    try {
        auto document = catalogue_json(registry);
        detail::normalize_json_numbers(
            document,
            "Controlled-unit catalogue",
            detail::kMaximumJsonDepth);
        return document.dump(
            pretty ? 2 : -1,
            ' ',
            false,
            Json::error_handler_t::strict);
    }
    catch (const ControlledUnitJsonError&) {
        throw;
    }
    catch (const std::exception& error) {
        fail("/", error.what());
    }
}

} // namespace pamguard::project
