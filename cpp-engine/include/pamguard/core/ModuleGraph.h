#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pamguard::core {

using ModuleTypeId = std::string;
using ModuleInstanceId = std::string;
using PortId = std::string;

enum class PortDirection {
    Input,
    Output,
};

struct PortDescriptor {
    PortId id;
    std::string name;
    PortDirection direction = PortDirection::Input;
    std::string data_type;
    bool required = false;
    bool accepts_multiple = false;
    std::vector<std::string> capabilities;
};

struct ModuleTypeDescriptor {
    ModuleTypeId id;
    std::string name;
    std::string category;
    std::string description;
    std::size_t minimum_instances = 0;
    std::optional<std::size_t> maximum_instances;
    std::vector<PortDescriptor> ports;
    std::string settings_schema_json = "{}";
    std::string default_settings_json = "{}";
    /** Supported execution contexts, normally live and offline/viewer. */
    std::vector<std::string> run_modes{"live", "offline"};
    /** Display-provider type IDs contributed by this module type. */
    std::vector<std::string> provided_display_types;
    /** Honest implementation and Java-parity catalogue labels. */
    std::string implementation_status = "implemented";
    std::string parity_status = "not-claimed";
};

class ModuleRegistry {
public:
    void register_type(ModuleTypeDescriptor descriptor);
    [[nodiscard]] const ModuleTypeDescriptor* find(const ModuleTypeId& id) const;
    [[nodiscard]] std::vector<ModuleTypeDescriptor> list() const;

private:
    std::unordered_map<ModuleTypeId, ModuleTypeDescriptor> types_;
};

struct ModuleInstance {
    ModuleInstanceId id;
    ModuleTypeId type_id;
    std::string name;
    bool enabled = true;
    std::string settings_json = "{}";
};

struct ModuleEndpoint {
    ModuleInstanceId module_id;
    PortId port_id;
};

struct ModuleConnection {
    std::string id;
    ModuleEndpoint source;
    ModuleEndpoint target;
};

struct ModuleGraphDocument {
    std::uint32_t schema_version = 1;
    std::uint64_t revision = 0;
    std::vector<ModuleInstance> modules;
    std::vector<ModuleConnection> connections;
    /** Versioned graph-wide acquisition, clock, and output policies. */
    std::string acquisition_json = "{}";
    std::string clock_json = "{}";
    std::string persistence_json = "{}";
};

struct GraphIssue {
    std::string code;
    std::string message;
    std::string module_id;
    std::string connection_id;
};

struct GraphValidation {
    std::vector<GraphIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return issues.empty(); }
};

struct GraphApplyResult {
    bool applied = false;
    std::uint64_t revision = 0;
    std::vector<GraphIssue> issues;
};

struct CompatibleSource {
    ModuleEndpoint endpoint;
    std::string module_name;
    std::string port_name;
    std::string data_type;
    std::vector<std::string> capabilities;
};

class ModuleGraph {
public:
    explicit ModuleGraph(const ModuleRegistry& registry);

    [[nodiscard]] ModuleGraphDocument snapshot() const;
    [[nodiscard]] GraphValidation validate(const ModuleGraphDocument& document) const;
    [[nodiscard]] GraphApplyResult apply(
        ModuleGraphDocument document,
        std::uint64_t expected_revision);
    [[nodiscard]] GraphValidation restore(ModuleGraphDocument document);
    [[nodiscard]] std::vector<CompatibleSource> compatible_sources(
        const ModuleGraphDocument& document,
        const ModuleEndpoint& target) const;

private:
    [[nodiscard]] GraphValidation validate_unlocked(
        const ModuleGraphDocument& document) const;

    const ModuleRegistry& registry_;
    mutable std::shared_mutex mutex_;
    ModuleGraphDocument document_;
};

} // namespace pamguard::core
