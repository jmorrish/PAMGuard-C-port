#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pamguard::project {

using ControlledUnitTypeId = std::string;
using DisplayProviderTypeId = std::string;
using GlobalSettingsTypeId = std::string;
using PublicRoleId = std::string;
using RuntimeChildRoleId = std::string;

enum class RunMode {
    Normal,
    Mixed,
    Viewer,
};

enum class DataRoleDirection {
    Input,
    Output,
};

enum class RoleCardinality {
    ZeroOrOne,
    ExactlyOne,
    ZeroOrMany,
    OneOrMany,
};

enum class SettingsChangePolicy {
    LiveSafe,
    ProcessRestart,
    StopRequired,
};

enum class AvailabilityStatus {
    Available,
    Unavailable,
};

[[nodiscard]] std::string_view to_string(RunMode mode) noexcept;
[[nodiscard]] std::string_view to_string(
    DataRoleDirection direction) noexcept;
[[nodiscard]] std::string_view to_string(
    RoleCardinality cardinality) noexcept;
[[nodiscard]] std::string_view to_string(
    SettingsChangePolicy policy) noexcept;
[[nodiscard]] std::string_view to_string(
    AvailabilityStatus status) noexcept;

struct JavaAuthorityDescriptor {
    std::string registered_name;
    std::string menu_group;
    std::string class_name;
    std::string relationship;
    std::string tooltip;
    std::string help_point;
    std::vector<std::string> source_references;
};

struct InstanceLimitDescriptor {
    std::size_t minimum_instances = 0;
    /** An absent maximum is Java's maxNumber == 0 (unlimited). */
    std::optional<std::size_t> maximum_instances;

    bool operator==(const InstanceLimitDescriptor&) const = default;
};

struct RunModeInstanceRulesOverrideDescriptor {
    RunMode mode = RunMode::Normal;
    std::size_t minimum_instances = 0;
    /** An absent maximum is Java's maxNumber == 0 (unlimited). */
    std::optional<std::size_t> maximum_instances;

    bool operator==(
        const RunModeInstanceRulesOverrideDescriptor&) const = default;
};

struct InstanceRulesDescriptor {
    std::size_t minimum_instances = 0;
    /** An absent maximum is Java's maxNumber == 0 (unlimited). */
    std::optional<std::size_t> maximum_instances;
    std::vector<RunMode> allowed_modes;
    /**
     * Java PamModuleInfo may vary min/max by run mode. Overrides are unique
     * by mode and may only target an allowed mode.
     */
    std::vector<RunModeInstanceRulesOverrideDescriptor> mode_overrides;
};

/** Resolve one run mode to its override, or to the descriptor's base limits. */
[[nodiscard]] InstanceLimitDescriptor effective_instance_limits(
    const InstanceRulesDescriptor& rules,
    RunMode mode) noexcept;

struct PublicDataRoleDescriptor {
    PublicRoleId id;
    std::string name;
    DataRoleDirection direction = DataRoleDirection::Input;
    std::string data_type;
    RoleCardinality cardinality = RoleCardinality::ZeroOrOne;
    std::vector<std::string> capabilities;
    /** Java PamDependency data class, when this is an input dependency. */
    std::string java_data_class;
    std::optional<ControlledUnitTypeId>
        default_provider_controlled_unit_type_id;
};

struct SettingsSectionDescriptor {
    std::string surface;
    std::vector<std::string> labels;
};

struct SettingDefaultDescriptor {
    /** RFC 6901 pointer into the canonical controlled-unit/provider settings. */
    std::string pointer;
    /** Java member path used by the parity manifest. */
    std::string authority_path;
    /** JSON-encoded literal. Absent when Java calculates the value at runtime. */
    std::optional<std::string> value_json;
    std::string value_source;
    std::string condition;
    std::string authority;
};

struct SettingsDescriptor {
    std::uint32_t version = 1;
    std::vector<std::string> authority_classes;
    std::vector<std::string> authority_sources;
    /** Complete initial canonical settings for this first registry slice. */
    std::string default_settings_json = "{}";
    std::vector<SettingsSectionDescriptor> sections;
    std::vector<SettingDefaultDescriptor> defaults;
    /**
     * Phase 1 begins conservatively: this policy applies to the whole settings
     * tree until field-level live/restart policies have parity evidence.
     */
    SettingsChangePolicy whole_tree_change_policy =
        SettingsChangePolicy::StopRequired;
    std::string parity_status = "not-claimed";
    /**
     * Draft 2020-12-style JSON Schema for the complete canonical settings
     * object. First-slice built-ins close every object with
     * additionalProperties:false.
     */
    std::string settings_schema_json =
        R"({"type":"object","additionalProperties":false})";
};

struct RuntimeSettingsProjectionDescriptor {
    /**
     * RFC 6901 pointer into canonical controlled-unit settings. The empty
     * string denotes the document root.
     */
    std::string source_pointer;
    /**
     * Stable pure-adapter identifier. "identity.v1" copies the selected
     * subtree; other identifiers name explicit field/value adapters.
     */
    std::string adapter_id;
};

struct RuntimeChildDescriptor {
    RuntimeChildRoleId role_id;
    std::string runtime_type_id;
    RuntimeSettingsProjectionDescriptor settings;
    bool hidden = true;
    AvailabilityStatus availability = AvailabilityStatus::Available;
    std::string parity_status;
};

struct RuntimeEndpointDescriptor {
    RuntimeChildRoleId child_role_id;
    std::string port_id;
};

struct PublicRoleMappingDescriptor {
    PublicRoleId public_role_id;
    RuntimeEndpointDescriptor runtime_endpoint;
};

struct InternalRuntimeEdgeDescriptor {
    std::string id;
    RuntimeEndpointDescriptor source;
    RuntimeEndpointDescriptor target;
};

struct RuntimeExpansionRecipeDescriptor {
    std::uint32_t version = 1;
    std::vector<RuntimeChildDescriptor> children;
    std::vector<PublicRoleMappingDescriptor> public_role_mappings;
    std::vector<InternalRuntimeEdgeDescriptor> internal_edges;
    /**
     * Presentation providers owned by this controlled unit. They are part of
     * the manifest recipe but are never expanded as runtime children.
     */
    std::vector<DisplayProviderTypeId> display_provider_ids;
    /** Stable persisted expansion-recipe identity. */
    std::string id;
};

struct ControlledUnitDescriptor {
    ControlledUnitTypeId id;
    std::uint32_t descriptor_version = 1;
    JavaAuthorityDescriptor java_authority;
    InstanceRulesDescriptor instance_rules;
    std::vector<PublicDataRoleDescriptor> public_roles;
    SettingsDescriptor settings;
    RuntimeExpansionRecipeDescriptor runtime_recipe;
    AvailabilityStatus availability = AvailabilityStatus::Available;
    std::string parity_status;
};

struct DisplayRoleMappingDescriptor {
    PublicRoleId public_role_id;
    std::string low_level_port_id;
};

struct DisplayCompatibilityDescriptor {
    /**
     * Existing low-level display type used only to validate input role/port
     * compatibility. It is not a runtime child expansion.
     */
    std::string low_level_type_id;
    std::vector<DisplayRoleMappingDescriptor> public_role_mappings;
};

struct DisplayProviderDescriptor {
    DisplayProviderTypeId id;
    std::uint32_t descriptor_version = 1;
    std::string provider_name;
    std::string java_provider_class;
    std::string java_component_class;
    ControlledUnitTypeId owner_controlled_unit_type_id;
    std::size_t minimum_instances = 0;
    /** Absent corresponds to Java getMaxDisplays() == 0 (unlimited). */
    std::optional<std::size_t> maximum_instances;
    bool can_create_without_source = false;
    std::vector<PublicDataRoleDescriptor> public_roles;
    SettingsDescriptor settings;
    DisplayCompatibilityDescriptor low_level_compatibility;
    AvailabilityStatus availability = AvailabilityStatus::Available;
    std::string parity_status;
    std::vector<std::string> source_references;
};

/**
 * One singleton-style project setting which is not owned by a controlled-unit
 * instance. Java's ArrayManager is the first such component.
 */
struct GlobalSettingsDescriptor {
    GlobalSettingsTypeId id;
    std::string name;
    JavaAuthorityDescriptor java_authority;
    SettingsDescriptor settings;
    /** Stable pure adapter which projects canonical settings to typed state. */
    std::string adapter_id;
    /** Required components are inserted into every new project. */
    bool required = false;
    AvailabilityStatus availability = AvailabilityStatus::Available;
    std::string parity_status;
};

/**
 * Decoupled snapshot of the low-level module catalogue. Project code can
 * validate recipes without making the controlled-unit registry depend on the
 * mutable graph or project document.
 */
struct LowLevelPortContract {
    std::string id;
    DataRoleDirection direction = DataRoleDirection::Input;
    std::string data_type;
    std::vector<std::string> capabilities;
};

struct LowLevelTypeContract {
    std::string id;
    std::vector<LowLevelPortContract> ports;
};

struct ControlledUnitRegistryIssue {
    std::string code;
    std::string descriptor_id;
    std::string message;
};

struct ControlledUnitRegistryValidation {
    std::vector<ControlledUnitRegistryIssue> issues;

    [[nodiscard]] bool valid() const noexcept {
        return issues.empty();
    }
};

class ControlledUnitRegistry {
public:
    void register_controlled_unit(ControlledUnitDescriptor descriptor);
    void register_display_provider(DisplayProviderDescriptor descriptor);
    void register_global_settings(GlobalSettingsDescriptor descriptor);

    [[nodiscard]] const ControlledUnitDescriptor* find_controlled_unit(
        const ControlledUnitTypeId& id) const noexcept;
    [[nodiscard]] const DisplayProviderDescriptor* find_display_provider(
        const DisplayProviderTypeId& id) const noexcept;
    [[nodiscard]] const GlobalSettingsDescriptor* find_global_settings(
        const GlobalSettingsTypeId& id) const noexcept;

    [[nodiscard]] const std::vector<ControlledUnitDescriptor>&
    controlled_units() const noexcept;
    [[nodiscard]] const std::vector<DisplayProviderDescriptor>&
    display_providers() const noexcept;
    [[nodiscard]] const std::vector<GlobalSettingsDescriptor>&
    global_settings() const noexcept;

    /** Validate registry structure without requiring a runtime catalogue. */
    [[nodiscard]] ControlledUnitRegistryValidation validate() const;
    /**
     * Also validate child and provider port mappings against actual low-level
     * runtime type descriptors.
     */
    [[nodiscard]] ControlledUnitRegistryValidation validate_against(
        std::span<const LowLevelTypeContract> low_level_types) const;

private:
    std::vector<ControlledUnitDescriptor> controlled_units_;
    std::vector<DisplayProviderDescriptor> display_providers_;
    std::vector<GlobalSettingsDescriptor> global_settings_;
    std::unordered_map<ControlledUnitTypeId, std::size_t>
        controlled_unit_index_;
    std::unordered_map<DisplayProviderTypeId, std::size_t>
        display_provider_index_;
    std::unordered_map<GlobalSettingsTypeId, std::size_t>
        global_settings_index_;
};

/**
 * Manifest runtimeTypeIds for a controlled-unit recipe, preserving child then
 * provider declaration order while removing duplicates.
 */
[[nodiscard]] std::vector<std::string> recipe_runtime_type_ids(
    const ControlledUnitDescriptor& descriptor);

} // namespace pamguard::project
