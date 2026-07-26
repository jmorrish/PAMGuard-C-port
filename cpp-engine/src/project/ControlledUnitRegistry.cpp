#include "pamguard/project/ControlledUnitRegistry.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace pamguard::project {

namespace {

bool is_lower_ascii(char character) noexcept {
    return character >= 'a' && character <= 'z';
}

bool is_digit_ascii(char character) noexcept {
    return character >= '0' && character <= '9';
}

bool valid_stable_id(std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    bool segment_start = true;
    bool previous_hyphen = false;
    for (const char character : value) {
        if (segment_start) {
            if (!is_lower_ascii(character)) {
                return false;
            }
            segment_start = false;
            previous_hyphen = false;
            continue;
        }
        if (character == '.') {
            if (previous_hyphen) {
                return false;
            }
            segment_start = true;
            continue;
        }
        if (character == '-') {
            if (previous_hyphen) {
                return false;
            }
            previous_hyphen = true;
            continue;
        }
        if (!is_lower_ascii(character) && !is_digit_ascii(character)) {
            return false;
        }
        previous_hyphen = false;
    }
    return !segment_start && !previous_hyphen;
}

bool valid_public_role_id(std::string_view value) noexcept {
    if (value.empty() || !is_lower_ascii(value.front())) {
        return false;
    }
    return std::all_of(
        value.begin() + 1,
        value.end(),
        [](const char character) {
            return is_lower_ascii(character) ||
                (character >= 'A' && character <= 'Z') ||
                is_digit_ascii(character);
        });
}

bool valid_json_pointer(std::string_view pointer) noexcept {
    if (pointer.empty()) {
        return true;
    }
    if (pointer.front() != '/') {
        return false;
    }
    for (std::size_t index = 0; index < pointer.size(); ++index) {
        if (pointer[index] != '~') {
            continue;
        }
        if (++index >= pointer.size() ||
            (pointer[index] != '0' && pointer[index] != '1')) {
            return false;
        }
    }
    return true;
}

bool valid_run_mode(RunMode mode) noexcept {
    switch (mode) {
    case RunMode::Normal:
    case RunMode::Mixed:
    case RunMode::Viewer:
        return true;
    }
    return false;
}

template <typename Range, typename Projection>
bool contains_duplicate(
    const Range& values,
    Projection projection) {
    std::unordered_set<std::string> seen;
    for (const auto& value : values) {
        if (!seen.emplace(projection(value)).second) {
            return true;
        }
    }
    return false;
}

bool contains_all_capabilities(
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

const PublicDataRoleDescriptor* find_public_role(
    const std::vector<PublicDataRoleDescriptor>& roles,
    const std::string& id) {
    const auto found = std::find_if(
        roles.begin(),
        roles.end(),
        [&](const auto& role) { return role.id == id; });
    return found == roles.end() ? nullptr : &*found;
}

const RuntimeChildDescriptor* find_child(
    const RuntimeExpansionRecipeDescriptor& recipe,
    const std::string& role_id) {
    const auto found = std::find_if(
        recipe.children.begin(),
        recipe.children.end(),
        [&](const auto& child) { return child.role_id == role_id; });
    return found == recipe.children.end() ? nullptr : &*found;
}

const LowLevelPortContract* find_port(
    const LowLevelTypeContract& type,
    const std::string& port_id) {
    const auto found = std::find_if(
        type.ports.begin(),
        type.ports.end(),
        [&](const auto& port) { return port.id == port_id; });
    return found == type.ports.end() ? nullptr : &*found;
}

void add_issue(
    ControlledUnitRegistryValidation& result,
    std::string code,
    const std::string& descriptor_id,
    std::string message) {
    result.issues.push_back({
        std::move(code),
        descriptor_id,
        std::move(message),
    });
}

void validate_settings(
    const SettingsDescriptor& settings,
    const std::string& descriptor_id,
    ControlledUnitRegistryValidation& result) {
    if (settings.version == 0) {
        add_issue(
            result,
            "invalid-settings-version",
            descriptor_id,
            "Settings version must be positive");
    }
    if (settings.authority_classes.empty() ||
        contains_duplicate(
            settings.authority_classes,
            [](const auto& value) { return value; })) {
        add_issue(
            result,
            "invalid-settings-authority",
            descriptor_id,
            "Settings authority classes must be non-empty and unique");
    }
    if (std::any_of(
            settings.authority_classes.begin(),
            settings.authority_classes.end(),
            [](const auto& value) { return value.empty(); }) ||
        std::any_of(
            settings.authority_sources.begin(),
            settings.authority_sources.end(),
            [](const auto& value) { return value.empty(); }) ||
        contains_duplicate(
            settings.authority_sources,
            [](const auto& value) { return value; })) {
        add_issue(
            result,
            "invalid-settings-authority",
            descriptor_id,
            "Settings authority entries must be non-empty and unique");
    }
    if (settings.default_settings_json.empty()) {
        add_issue(
            result,
            "missing-settings-defaults",
            descriptor_id,
            "Canonical default settings JSON must not be empty");
    }
    if (settings.parity_status.empty()) {
        add_issue(
            result,
            "missing-settings-parity",
            descriptor_id,
            "Settings parity status must not be empty");
    }

    std::unordered_set<std::string> surfaces;
    for (const auto& section : settings.sections) {
        if (!valid_stable_id(section.surface) ||
            !surfaces.emplace(section.surface).second ||
            section.labels.empty() ||
            std::any_of(
                section.labels.begin(),
                section.labels.end(),
                [](const auto& label) { return label.empty(); })) {
            add_issue(
                result,
                "invalid-settings-section",
                descriptor_id,
                "Settings surfaces must have unique stable IDs and non-empty labels");
        }
    }

    std::unordered_set<std::string> pointers;
    std::unordered_set<std::string> authority_paths;
    for (const auto& entry : settings.defaults) {
        if (entry.pointer.empty() ||
            !valid_json_pointer(entry.pointer) ||
            !pointers.emplace(entry.pointer).second ||
            entry.authority_path.empty() ||
            !authority_paths.emplace(entry.authority_path).second ||
            entry.authority.empty() ||
            (entry.value_json.has_value() == !entry.value_source.empty())) {
            add_issue(
                result,
                "invalid-setting-default",
                descriptor_id,
                "Setting defaults need unique pointers/authority paths, one value source, and an authority");
        }
    }
}

void validate_roles(
    const std::vector<PublicDataRoleDescriptor>& roles,
    const std::string& descriptor_id,
    ControlledUnitRegistryValidation& result) {
    std::unordered_set<std::string> role_ids;
    for (const auto& role : roles) {
        if (!valid_public_role_id(role.id) ||
            !role_ids.emplace(role.id).second) {
            add_issue(
                result,
                "invalid-public-role-id",
                descriptor_id,
                "Public role IDs must be unique lower-camel stable IDs");
        }
        if (role.name.empty() || !valid_stable_id(role.data_type)) {
            add_issue(
                result,
                "invalid-public-role",
                descriptor_id,
                "Public roles require a name and stable data type");
        }
        if (contains_duplicate(
                role.capabilities,
                [](const auto& value) { return value; }) ||
            std::any_of(
                role.capabilities.begin(),
                role.capabilities.end(),
                [](const auto& value) {
                    return !valid_stable_id(value);
                })) {
            add_issue(
                result,
                "invalid-role-capability",
                descriptor_id,
                "Role capabilities must be unique stable IDs");
        }
        if (role.direction == DataRoleDirection::Input) {
            if (role.java_data_class.empty()) {
                add_issue(
                    result,
                    "missing-java-data-class",
                    descriptor_id,
                    "Input role '" + role.id +
                        "' must name its Java data class");
            }
            if (role.default_provider_controlled_unit_type_id &&
                !valid_stable_id(
                    *role.default_provider_controlled_unit_type_id)) {
                add_issue(
                    result,
                    "invalid-default-provider-id",
                    descriptor_id,
                    "Input role '" + role.id +
                        "' has an invalid default-provider ID");
            }
        }
        else if (!role.java_data_class.empty() ||
                 role.default_provider_controlled_unit_type_id) {
            add_issue(
                result,
                "invalid-output-dependency",
                descriptor_id,
                "Output role '" + role.id +
                    "' cannot declare an input dependency");
        }
    }
}

void validate_instance_rules(
    const InstanceRulesDescriptor& rules,
    const std::string& descriptor_id,
    ControlledUnitRegistryValidation& result) {
    if (rules.maximum_instances &&
        rules.minimum_instances > *rules.maximum_instances) {
        add_issue(
            result,
            "invalid-instance-limits",
            descriptor_id,
            "Minimum instances exceeds maximum instances");
    }
    std::unordered_set<int> modes;
    bool invalid_mode = false;
    for (const auto mode : rules.allowed_modes) {
        invalid_mode = invalid_mode || !valid_run_mode(mode);
        modes.emplace(static_cast<int>(mode));
    }
    if (rules.allowed_modes.empty() || invalid_mode ||
        modes.size() != rules.allowed_modes.size()) {
        add_issue(
            result,
            "invalid-run-modes",
            descriptor_id,
            "Allowed run modes must be non-empty and unique");
    }

    std::unordered_set<int> overridden_modes;
    bool invalid_override = false;
    for (const auto& override_rules : rules.mode_overrides) {
        const auto mode = static_cast<int>(override_rules.mode);
        invalid_override =
            invalid_override ||
            !valid_run_mode(override_rules.mode) ||
            !modes.contains(mode) ||
            !overridden_modes.emplace(mode).second ||
            (override_rules.maximum_instances &&
             override_rules.minimum_instances >
                 *override_rules.maximum_instances);
    }
    if (invalid_override) {
        add_issue(
            result,
            "invalid-instance-mode-overrides",
            descriptor_id,
            "Run-mode instance overrides must be unique, target allowed modes, and have valid limits");
    }
}

void validate_recipe_structure(
    const ControlledUnitDescriptor& descriptor,
    ControlledUnitRegistryValidation& result) {
    const auto& recipe = descriptor.runtime_recipe;
    if (recipe.version == 0) {
        add_issue(
            result,
            "invalid-recipe-version",
            descriptor.id,
            "Runtime recipe version must be positive");
    }
    if (!valid_stable_id(recipe.id)) {
        add_issue(
            result,
            "invalid-recipe-id",
            descriptor.id,
            "Runtime recipe identity must be a non-empty stable ID");
    }

    std::unordered_set<std::string> child_roles;
    for (const auto& child : recipe.children) {
        if (!valid_stable_id(child.role_id) ||
            !child_roles.emplace(child.role_id).second ||
            !valid_stable_id(child.runtime_type_id) ||
            !valid_json_pointer(child.settings.source_pointer) ||
            !valid_stable_id(child.settings.adapter_id) ||
            child.parity_status.empty()) {
            add_issue(
                result,
                "invalid-runtime-child",
                descriptor.id,
                "Runtime children require unique roles, stable IDs, explicit settings projection, and parity");
        }
    }

    std::unordered_set<std::string> mapped_roles;
    for (const auto& mapping : recipe.public_role_mappings) {
        const auto* role =
            find_public_role(descriptor.public_roles, mapping.public_role_id);
        const auto* child =
            find_child(recipe, mapping.runtime_endpoint.child_role_id);
        if (!role ||
            !mapped_roles.emplace(mapping.public_role_id).second ||
            !child ||
            !valid_stable_id(mapping.runtime_endpoint.port_id)) {
            add_issue(
                result,
                "invalid-public-role-mapping",
                descriptor.id,
                "Every public-role mapping must uniquely reference an existing role, child, and stable port");
        }
    }
    for (const auto& role : descriptor.public_roles) {
        if (!mapped_roles.contains(role.id)) {
            add_issue(
                result,
                "missing-public-role-mapping",
                descriptor.id,
                "Public role '" + role.id +
                    "' is not mapped by the runtime recipe");
        }
    }

    std::unordered_set<std::string> edge_ids;
    for (const auto& edge : recipe.internal_edges) {
        if (!valid_stable_id(edge.id) ||
            !edge_ids.emplace(edge.id).second ||
            !find_child(recipe, edge.source.child_role_id) ||
            !find_child(recipe, edge.target.child_role_id) ||
            !valid_stable_id(edge.source.port_id) ||
            !valid_stable_id(edge.target.port_id)) {
            add_issue(
                result,
                "invalid-internal-edge",
                descriptor.id,
                "Internal edges require unique IDs and existing child/port endpoints");
        }
    }

    if (contains_duplicate(
            recipe.display_provider_ids,
            [](const auto& value) { return value; }) ||
        std::any_of(
            recipe.display_provider_ids.begin(),
            recipe.display_provider_ids.end(),
            [](const auto& value) {
                return !valid_stable_id(value);
            })) {
        add_issue(
            result,
            "invalid-display-provider-recipe",
            descriptor.id,
            "Recipe display-provider IDs must be unique stable IDs");
    }
}

void validate_provider_structure(
    const DisplayProviderDescriptor& provider,
    ControlledUnitRegistryValidation& result) {
    if (!valid_stable_id(provider.id) ||
        provider.descriptor_version == 0 ||
        provider.provider_name.empty() ||
        provider.java_provider_class.empty() ||
        provider.java_component_class.empty() ||
        !valid_stable_id(provider.owner_controlled_unit_type_id) ||
        provider.parity_status.empty()) {
        add_issue(
            result,
            "invalid-display-provider",
            provider.id,
            "Display provider identity, version, Java classes, owner, and parity are required");
    }
    if (provider.maximum_instances &&
        provider.minimum_instances > *provider.maximum_instances) {
        add_issue(
            result,
            "invalid-instance-limits",
            provider.id,
            "Display provider minimum exceeds maximum");
    }
    validate_roles(provider.public_roles, provider.id, result);
    validate_settings(provider.settings, provider.id, result);

    if (!valid_stable_id(
            provider.low_level_compatibility.low_level_type_id)) {
        add_issue(
            result,
            "invalid-display-compatibility",
            provider.id,
            "Display compatibility type must be a stable ID");
    }
    std::unordered_set<std::string> mapped_roles;
    for (const auto& mapping :
         provider.low_level_compatibility.public_role_mappings) {
        if (!find_public_role(
                provider.public_roles,
                mapping.public_role_id) ||
            !mapped_roles.emplace(mapping.public_role_id).second ||
            !valid_stable_id(mapping.low_level_port_id)) {
            add_issue(
                result,
                "invalid-display-role-mapping",
                provider.id,
                "Display role mappings must uniquely map existing roles to stable low-level ports");
        }
    }
    for (const auto& role : provider.public_roles) {
        if (!mapped_roles.contains(role.id)) {
            add_issue(
                result,
                "missing-display-role-mapping",
                provider.id,
                "Display public role '" + role.id +
                    "' has no compatibility mapping");
        }
    }
}

const PublicDataRoleDescriptor* compatible_default_output(
    const ControlledUnitDescriptor& provider,
    const PublicDataRoleDescriptor& input) {
    const auto found = std::find_if(
        provider.public_roles.begin(),
        provider.public_roles.end(),
        [&](const auto& output) {
            return output.direction == DataRoleDirection::Output &&
                output.data_type == input.data_type &&
                contains_all_capabilities(
                    output.capabilities,
                    input.capabilities);
        });
    return found == provider.public_roles.end() ? nullptr : &*found;
}

void validate_default_provider(
    const ControlledUnitRegistry& registry,
    const PublicDataRoleDescriptor& role,
    const std::string& descriptor_id,
    ControlledUnitRegistryValidation& result) {
    if (!role.default_provider_controlled_unit_type_id) {
        return;
    }
    const auto* provider = registry.find_controlled_unit(
        *role.default_provider_controlled_unit_type_id);
    if (!provider) {
        add_issue(
            result,
            "missing-default-provider",
            descriptor_id,
            "Role '" + role.id + "' refers to unknown default provider '" +
                *role.default_provider_controlled_unit_type_id + "'");
        return;
    }
    if (!compatible_default_output(*provider, role)) {
        add_issue(
            result,
            "incompatible-default-provider",
            descriptor_id,
            "Default provider '" + provider->id +
                "' has no compatible public output for role '" + role.id +
                "'");
    }
}

using LowLevelTypeIndex =
    std::unordered_map<std::string, const LowLevelTypeContract*>;

void validate_low_level_endpoint(
    const LowLevelTypeIndex& low_level_types,
    const std::string& descriptor_id,
    const RuntimeChildDescriptor& child,
    const std::string& port_id,
    const PublicDataRoleDescriptor& public_role,
    ControlledUnitRegistryValidation& result) {
    const auto type = low_level_types.find(child.runtime_type_id);
    if (type == low_level_types.end()) {
        return;
    }
    const auto* port = find_port(*type->second, port_id);
    if (!port) {
        add_issue(
            result,
            "missing-low-level-port",
            descriptor_id,
            "Runtime child '" + child.role_id + "' type '" +
                child.runtime_type_id + "' has no port '" + port_id + "'");
        return;
    }
    if (port->direction != public_role.direction) {
        add_issue(
            result,
            "low-level-direction-mismatch",
            descriptor_id,
            "Public role '" + public_role.id +
                "' direction differs from its low-level port");
    }
    if (port->data_type != public_role.data_type) {
        add_issue(
            result,
            "low-level-data-type-mismatch",
            descriptor_id,
            "Public role '" + public_role.id +
                "' data type differs from its low-level port");
    }
    if (!contains_all_capabilities(
            port->capabilities,
            public_role.capabilities)) {
        add_issue(
            result,
            "low-level-capability-mismatch",
            descriptor_id,
            "Public role '" + public_role.id +
                "' requires capabilities absent from its low-level port");
    }
}

void validate_internal_edge_against_low_level(
    const LowLevelTypeIndex& low_level_types,
    const ControlledUnitDescriptor& descriptor,
    const InternalRuntimeEdgeDescriptor& edge,
    ControlledUnitRegistryValidation& result) {
    const auto* source_child =
        find_child(descriptor.runtime_recipe, edge.source.child_role_id);
    const auto* target_child =
        find_child(descriptor.runtime_recipe, edge.target.child_role_id);
    if (!source_child || !target_child) {
        return;
    }
    const auto source_type =
        low_level_types.find(source_child->runtime_type_id);
    const auto target_type =
        low_level_types.find(target_child->runtime_type_id);
    if (source_type == low_level_types.end() ||
        target_type == low_level_types.end()) {
        return;
    }
    const auto* source_port =
        find_port(*source_type->second, edge.source.port_id);
    const auto* target_port =
        find_port(*target_type->second, edge.target.port_id);
    if (!source_port || !target_port) {
        add_issue(
            result,
            "missing-low-level-port",
            descriptor.id,
            "Internal edge '" + edge.id +
                "' refers to a missing low-level port");
        return;
    }
    if (source_port->direction != DataRoleDirection::Output ||
        target_port->direction != DataRoleDirection::Input) {
        add_issue(
            result,
            "low-level-direction-mismatch",
            descriptor.id,
            "Internal edge '" + edge.id +
                "' must connect output to input");
    }
    if (source_port->data_type != target_port->data_type) {
        add_issue(
            result,
            "low-level-data-type-mismatch",
            descriptor.id,
            "Internal edge '" + edge.id +
                "' connects incompatible data types");
    }
}

ControlledUnitRegistryValidation validate_registry(
    const ControlledUnitRegistry& registry,
    std::span<const LowLevelTypeContract> low_level_catalogue,
    bool check_low_level) {
    ControlledUnitRegistryValidation result;
    std::unordered_set<std::string> recipe_ids;

    for (const auto& descriptor : registry.controlled_units()) {
        if (!valid_stable_id(descriptor.id) ||
            descriptor.descriptor_version == 0 ||
            descriptor.java_authority.registered_name.empty() ||
            descriptor.java_authority.menu_group.empty() ||
            descriptor.java_authority.class_name.empty() ||
            descriptor.java_authority.relationship.empty() ||
            descriptor.parity_status.empty()) {
            add_issue(
                result,
                "invalid-controlled-unit",
                descriptor.id,
                "Controlled-unit identity, versions, Java authority, and parity are required");
        }
        validate_instance_rules(
            descriptor.instance_rules,
            descriptor.id,
            result);
        validate_roles(
            descriptor.public_roles,
            descriptor.id,
            result);
        validate_settings(
            descriptor.settings,
            descriptor.id,
            result);
        validate_recipe_structure(descriptor, result);
        if (valid_stable_id(descriptor.runtime_recipe.id) &&
            !recipe_ids.emplace(descriptor.runtime_recipe.id).second) {
            add_issue(
                result,
                "duplicate-recipe-id",
                descriptor.id,
                "Runtime recipe identity '" +
                    descriptor.runtime_recipe.id +
                    "' is already registered");
        }
        for (const auto& role : descriptor.public_roles) {
            validate_default_provider(
                registry,
                role,
                descriptor.id,
                result);
        }
    }

    for (const auto& provider : registry.display_providers()) {
        validate_provider_structure(provider, result);
        const auto* owner = registry.find_controlled_unit(
            provider.owner_controlled_unit_type_id);
        if (!owner) {
            add_issue(
                result,
                "missing-display-owner",
                provider.id,
                "Display provider owner is not registered");
        }
        else if (std::find(
                     owner->runtime_recipe.display_provider_ids.begin(),
                     owner->runtime_recipe.display_provider_ids.end(),
                     provider.id) ==
                 owner->runtime_recipe.display_provider_ids.end()) {
            add_issue(
                result,
                "missing-display-owner-recipe",
                provider.id,
                "Owning controlled-unit recipe does not contribute this provider");
        }
        for (const auto& role : provider.public_roles) {
            validate_default_provider(
                registry,
                role,
                provider.id,
                result);
        }
    }

    for (const auto& descriptor : registry.global_settings()) {
        if (!valid_stable_id(descriptor.id) ||
            descriptor.name.empty() ||
            descriptor.java_authority.registered_name.empty() ||
            descriptor.java_authority.class_name.empty() ||
            descriptor.java_authority.relationship.empty() ||
            !valid_stable_id(descriptor.adapter_id) ||
            descriptor.parity_status.empty()) {
            add_issue(
                result,
                "invalid-global-settings",
                descriptor.id,
                "Global-settings identity, name, Java authority, adapter, and parity are required");
        }
        validate_settings(
            descriptor.settings,
            descriptor.id,
            result);
    }

    for (const auto& descriptor : registry.controlled_units()) {
        for (const auto& provider_id :
             descriptor.runtime_recipe.display_provider_ids) {
            const auto* provider =
                registry.find_display_provider(provider_id);
            if (!provider) {
                add_issue(
                    result,
                    "missing-recipe-display-provider",
                    descriptor.id,
                    "Recipe refers to unknown display provider '" +
                        provider_id + "'");
            }
            else if (provider->owner_controlled_unit_type_id !=
                     descriptor.id) {
                add_issue(
                    result,
                    "display-owner-mismatch",
                    descriptor.id,
                    "Recipe provider '" + provider_id +
                        "' names another owner");
            }
        }
    }

    if (!check_low_level) {
        return result;
    }

    LowLevelTypeIndex low_level_types;
    for (const auto& type : low_level_catalogue) {
        if (!valid_stable_id(type.id) ||
            !low_level_types.emplace(type.id, &type).second) {
            add_issue(
                result,
                "invalid-low-level-type",
                type.id,
                "Low-level type catalogue IDs must be unique stable IDs");
        }
        if (contains_duplicate(
                type.ports,
                [](const auto& port) { return port.id; })) {
            add_issue(
                result,
                "invalid-low-level-port",
                type.id,
                "Low-level port IDs must be unique per type");
        }
    }

    for (const auto& descriptor : registry.controlled_units()) {
        for (const auto& child : descriptor.runtime_recipe.children) {
            const bool explicitly_unavailable =
                descriptor.availability ==
                    AvailabilityStatus::Unavailable &&
                child.availability ==
                    AvailabilityStatus::Unavailable;
            if (!low_level_types.contains(child.runtime_type_id) &&
                !explicitly_unavailable) {
                add_issue(
                    result,
                    "missing-low-level-type",
                    descriptor.id,
                    "Runtime child '" + child.role_id +
                        "' refers to unknown low-level type '" +
                        child.runtime_type_id + "'");
            }
        }
        for (const auto& mapping :
             descriptor.runtime_recipe.public_role_mappings) {
            const auto* role =
                find_public_role(
                    descriptor.public_roles,
                    mapping.public_role_id);
            const auto* child =
                find_child(
                    descriptor.runtime_recipe,
                    mapping.runtime_endpoint.child_role_id);
            const bool explicitly_unavailable =
                child &&
                descriptor.availability ==
                    AvailabilityStatus::Unavailable &&
                child->availability ==
                    AvailabilityStatus::Unavailable;
            if (role && child && !explicitly_unavailable) {
                validate_low_level_endpoint(
                    low_level_types,
                    descriptor.id,
                    *child,
                    mapping.runtime_endpoint.port_id,
                    *role,
                    result);
            }
        }
        for (const auto& edge :
             descriptor.runtime_recipe.internal_edges) {
            const auto* source_child =
                find_child(
                    descriptor.runtime_recipe,
                    edge.source.child_role_id);
            const auto* target_child =
                find_child(
                    descriptor.runtime_recipe,
                    edge.target.child_role_id);
            const bool explicitly_unavailable =
                descriptor.availability ==
                    AvailabilityStatus::Unavailable &&
                source_child &&
                target_child &&
                source_child->availability ==
                    AvailabilityStatus::Unavailable &&
                target_child->availability ==
                    AvailabilityStatus::Unavailable;
            if (explicitly_unavailable) {
                continue;
            }
            validate_internal_edge_against_low_level(
                low_level_types,
                descriptor,
                edge,
                result);
        }
    }

    for (const auto& provider : registry.display_providers()) {
        const auto type = low_level_types.find(
            provider.low_level_compatibility.low_level_type_id);
        if (type == low_level_types.end()) {
            add_issue(
                result,
                "missing-low-level-type",
                provider.id,
                "Display compatibility refers to unknown low-level type '" +
                    provider.low_level_compatibility.low_level_type_id + "'");
            continue;
        }
        for (const auto& mapping :
             provider.low_level_compatibility.public_role_mappings) {
            const auto* role =
                find_public_role(
                    provider.public_roles,
                    mapping.public_role_id);
            if (!role) {
                continue;
            }
            const auto* port =
                find_port(*type->second, mapping.low_level_port_id);
            if (!port) {
                add_issue(
                    result,
                    "missing-low-level-port",
                    provider.id,
                    "Display compatibility type has no port '" +
                        mapping.low_level_port_id + "'");
                continue;
            }
            if (port->direction != role->direction) {
                add_issue(
                    result,
                    "low-level-direction-mismatch",
                    provider.id,
                    "Display role '" + role->id +
                        "' direction differs from its low-level port");
            }
            if (port->data_type != role->data_type) {
                add_issue(
                    result,
                    "low-level-data-type-mismatch",
                    provider.id,
                    "Display role '" + role->id +
                        "' data type differs from its low-level port");
            }
            if (!contains_all_capabilities(
                    port->capabilities,
                    role->capabilities)) {
                add_issue(
                    result,
                    "low-level-capability-mismatch",
                    provider.id,
                    "Display role '" + role->id +
                        "' requires capabilities absent from its low-level port");
            }
        }
    }

    return result;
}

} // namespace

std::string_view to_string(RunMode mode) noexcept {
    switch (mode) {
    case RunMode::Normal:
        return "normal";
    case RunMode::Mixed:
        return "mixed";
    case RunMode::Viewer:
        return "viewer";
    }
    return "unknown";
}

InstanceLimitDescriptor effective_instance_limits(
    const InstanceRulesDescriptor& rules,
    RunMode mode) noexcept {
    const auto found = std::find_if(
        rules.mode_overrides.begin(),
        rules.mode_overrides.end(),
        [mode](const auto& override_rules) {
            return override_rules.mode == mode;
        });
    if (found != rules.mode_overrides.end()) {
        return {
            found->minimum_instances,
            found->maximum_instances,
        };
    }
    return {
        rules.minimum_instances,
        rules.maximum_instances,
    };
}

std::string_view to_string(DataRoleDirection direction) noexcept {
    switch (direction) {
    case DataRoleDirection::Input:
        return "input";
    case DataRoleDirection::Output:
        return "output";
    }
    return "unknown";
}

std::string_view to_string(RoleCardinality cardinality) noexcept {
    switch (cardinality) {
    case RoleCardinality::ZeroOrOne:
        return "0..1";
    case RoleCardinality::ExactlyOne:
        return "1";
    case RoleCardinality::ZeroOrMany:
        return "0..N";
    case RoleCardinality::OneOrMany:
        return "1..N";
    }
    return "unknown";
}

std::string_view to_string(SettingsChangePolicy policy) noexcept {
    switch (policy) {
    case SettingsChangePolicy::LiveSafe:
        return "live-safe";
    case SettingsChangePolicy::ProcessRestart:
        return "process-restart";
    case SettingsChangePolicy::StopRequired:
        return "stop-required";
    }
    return "unknown";
}

std::string_view to_string(AvailabilityStatus status) noexcept {
    switch (status) {
    case AvailabilityStatus::Available:
        return "available";
    case AvailabilityStatus::Unavailable:
        return "unavailable";
    }
    return "unknown";
}

void ControlledUnitRegistry::register_controlled_unit(
    ControlledUnitDescriptor descriptor) {
    if (controlled_unit_index_.contains(descriptor.id) ||
        display_provider_index_.contains(descriptor.id) ||
        global_settings_index_.contains(descriptor.id)) {
        throw std::invalid_argument(
            "Duplicate controlled-unit/display-provider/global-settings ID: " +
            descriptor.id);
    }
    const auto index = controlled_units_.size();
    controlled_unit_index_.emplace(descriptor.id, index);
    controlled_units_.push_back(std::move(descriptor));
}

void ControlledUnitRegistry::register_display_provider(
    DisplayProviderDescriptor descriptor) {
    if (display_provider_index_.contains(descriptor.id) ||
        controlled_unit_index_.contains(descriptor.id) ||
        global_settings_index_.contains(descriptor.id)) {
        throw std::invalid_argument(
            "Duplicate display-provider/controlled-unit/global-settings ID: " +
            descriptor.id);
    }
    const auto index = display_providers_.size();
    display_provider_index_.emplace(descriptor.id, index);
    display_providers_.push_back(std::move(descriptor));
}

void ControlledUnitRegistry::register_global_settings(
    GlobalSettingsDescriptor descriptor) {
    if (global_settings_index_.contains(descriptor.id) ||
        controlled_unit_index_.contains(descriptor.id) ||
        display_provider_index_.contains(descriptor.id)) {
        throw std::invalid_argument(
            "Duplicate global-settings/controlled-unit/display-provider ID: " +
            descriptor.id);
    }
    const auto index = global_settings_.size();
    global_settings_index_.emplace(descriptor.id, index);
    global_settings_.push_back(std::move(descriptor));
}

const ControlledUnitDescriptor*
ControlledUnitRegistry::find_controlled_unit(
    const ControlledUnitTypeId& id) const noexcept {
    const auto found = controlled_unit_index_.find(id);
    return found == controlled_unit_index_.end()
        ? nullptr
        : &controlled_units_[found->second];
}

const DisplayProviderDescriptor*
ControlledUnitRegistry::find_display_provider(
    const DisplayProviderTypeId& id) const noexcept {
    const auto found = display_provider_index_.find(id);
    return found == display_provider_index_.end()
        ? nullptr
        : &display_providers_[found->second];
}

const GlobalSettingsDescriptor*
ControlledUnitRegistry::find_global_settings(
    const GlobalSettingsTypeId& id) const noexcept {
    const auto found = global_settings_index_.find(id);
    return found == global_settings_index_.end()
        ? nullptr
        : &global_settings_[found->second];
}

const std::vector<ControlledUnitDescriptor>&
ControlledUnitRegistry::controlled_units() const noexcept {
    return controlled_units_;
}

const std::vector<DisplayProviderDescriptor>&
ControlledUnitRegistry::display_providers() const noexcept {
    return display_providers_;
}

const std::vector<GlobalSettingsDescriptor>&
ControlledUnitRegistry::global_settings() const noexcept {
    return global_settings_;
}

ControlledUnitRegistryValidation
ControlledUnitRegistry::validate() const {
    return validate_registry(*this, {}, false);
}

ControlledUnitRegistryValidation
ControlledUnitRegistry::validate_against(
    std::span<const LowLevelTypeContract> low_level_types) const {
    return validate_registry(*this, low_level_types, true);
}

std::vector<std::string> recipe_runtime_type_ids(
    const ControlledUnitDescriptor& descriptor) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    for (const auto& child : descriptor.runtime_recipe.children) {
        if (seen.emplace(child.runtime_type_id).second) {
            result.push_back(child.runtime_type_id);
        }
    }
    for (const auto& provider_id :
         descriptor.runtime_recipe.display_provider_ids) {
        if (seen.emplace(provider_id).second) {
            result.push_back(provider_id);
        }
    }
    return result;
}

} // namespace pamguard::project
