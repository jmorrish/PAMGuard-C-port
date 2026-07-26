#include "pamguard/core/ModuleGraph.h"

#include <algorithm>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace pamguard::core {

namespace {

const PortDescriptor* find_port(
    const ModuleTypeDescriptor& type,
    const PortId& port_id,
    PortDirection direction) {
    const auto found = std::find_if(
        type.ports.begin(),
        type.ports.end(),
        [&](const PortDescriptor& port) {
            return port.id == port_id && port.direction == direction;
        });
    return found == type.ports.end() ? nullptr : &*found;
}

bool has_capabilities(
    const std::vector<std::string>& available,
    const std::vector<std::string>& required) {
    return std::all_of(
        required.begin(),
        required.end(),
        [&](const std::string& capability) {
            return std::find(available.begin(), available.end(), capability) != available.end();
        });
}

bool types_compatible(const std::string& source, const std::string& target) {
    return source == target || source == "*" || target == "*" ||
        target == "pamguard.acoustic-data-unit";
}

void add_issue(
    GraphValidation& validation,
    std::string code,
    std::string message,
    std::string module_id = {},
    std::string connection_id = {}) {
    validation.issues.push_back({
        std::move(code),
        std::move(message),
        std::move(module_id),
        std::move(connection_id),
    });
}

} // namespace

void ModuleRegistry::register_type(ModuleTypeDescriptor descriptor) {
    if (descriptor.id.empty() || descriptor.name.empty()) {
        throw std::invalid_argument("Module type id and name are required");
    }
    std::unordered_set<std::string> port_ids;
    for (const auto& port : descriptor.ports) {
        if (port.id.empty() || port.name.empty() || port.data_type.empty()) {
            throw std::invalid_argument("Module ports require an id, name, and data type");
        }
        const auto key = std::to_string(static_cast<int>(port.direction)) + ":" + port.id;
        if (!port_ids.insert(key).second) {
            throw std::invalid_argument("Duplicate module port id and direction");
        }
    }
    if (descriptor.maximum_instances &&
        *descriptor.maximum_instances < descriptor.minimum_instances) {
        throw std::invalid_argument("Module maximum instances cannot be below its minimum");
    }
    if (!types_.emplace(descriptor.id, std::move(descriptor)).second) {
        throw std::invalid_argument("Duplicate module type id");
    }
}

const ModuleTypeDescriptor* ModuleRegistry::find(const ModuleTypeId& id) const {
    const auto found = types_.find(id);
    return found == types_.end() ? nullptr : &found->second;
}

std::vector<ModuleTypeDescriptor> ModuleRegistry::list() const {
    std::vector<ModuleTypeDescriptor> result;
    result.reserve(types_.size());
    for (const auto& [_, descriptor] : types_) {
        result.push_back(descriptor);
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

ModuleGraph::ModuleGraph(const ModuleRegistry& registry)
    : registry_(registry) {}

ModuleGraphDocument ModuleGraph::snapshot() const {
    std::shared_lock lock(mutex_);
    return document_;
}

GraphValidation ModuleGraph::validate(const ModuleGraphDocument& document) const {
    std::shared_lock lock(mutex_);
    return validate_unlocked(document);
}

GraphValidation ModuleGraph::validate_unlocked(const ModuleGraphDocument& document) const {
    GraphValidation validation;
    if (document.schema_version != 1) {
        add_issue(
            validation,
            "unsupported_schema",
            "Only module graph schema version 1 is supported");
    }

    std::unordered_map<std::string, const ModuleInstance*> modules;
    std::unordered_map<std::string, std::size_t> type_counts;
    for (const auto& module : document.modules) {
        if (module.id.empty()) {
            add_issue(validation, "missing_module_id", "Module instance id is required");
            continue;
        }
        if (!modules.emplace(module.id, &module).second) {
            add_issue(
                validation,
                "duplicate_module_id",
                "Module instance id must be unique",
                module.id);
            continue;
        }
        if (module.name.empty()) {
            add_issue(
                validation,
                "missing_module_name",
                "Module instance name is required",
                module.id);
        }
        if (registry_.find(module.type_id) == nullptr) {
            add_issue(
                validation,
                "unknown_module_type",
                "Module instance references an unregistered type",
                module.id);
        }
        else {
            ++type_counts[module.type_id];
        }
    }

    for (const auto& type : registry_.list()) {
        const auto count = type_counts[type.id];
        if (count < type.minimum_instances) {
            add_issue(
                validation,
                "minimum_instances",
                "Module type has fewer instances than required");
        }
        if (type.maximum_instances && count > *type.maximum_instances) {
            add_issue(
                validation,
                "maximum_instances",
                "Module type has more instances than allowed");
        }
    }

    std::unordered_set<std::string> connection_ids;
    std::unordered_map<std::string, std::size_t> target_counts;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    std::unordered_map<std::string, std::size_t> indegree;
    for (const auto& [module_id, _] : modules) {
        indegree.emplace(module_id, 0);
    }

    for (const auto& connection : document.connections) {
        if (connection.id.empty() || !connection_ids.insert(connection.id).second) {
            add_issue(
                validation,
                connection.id.empty() ? "missing_connection_id" : "duplicate_connection_id",
                "Connection id is required and must be unique",
                {},
                connection.id);
        }

        const auto source_instance = modules.find(connection.source.module_id);
        const auto target_instance = modules.find(connection.target.module_id);
        if (source_instance == modules.end()) {
            add_issue(
                validation,
                "unknown_source_module",
                "Connection source module does not exist",
                connection.source.module_id,
                connection.id);
        }
        if (target_instance == modules.end()) {
            add_issue(
                validation,
                "unknown_target_module",
                "Connection target module does not exist",
                connection.target.module_id,
                connection.id);
        }
        if (source_instance == modules.end() || target_instance == modules.end()) {
            continue;
        }
        if (!source_instance->second->enabled &&
            target_instance->second->enabled) {
            add_issue(
                validation,
                "disabled_source",
                "Enabled module input is connected to a disabled source module",
                connection.target.module_id,
                connection.id);
        }

        const auto* source_type = registry_.find(source_instance->second->type_id);
        const auto* target_type = registry_.find(target_instance->second->type_id);
        if (source_type == nullptr || target_type == nullptr) {
            continue;
        }
        const auto* source_port = find_port(
            *source_type,
            connection.source.port_id,
            PortDirection::Output);
        const auto* target_port = find_port(
            *target_type,
            connection.target.port_id,
            PortDirection::Input);
        if (source_port == nullptr) {
            add_issue(
                validation,
                "unknown_source_port",
                "Connection source is not a registered output port",
                connection.source.module_id,
                connection.id);
        }
        if (target_port == nullptr) {
            add_issue(
                validation,
                "unknown_target_port",
                "Connection target is not a registered input port",
                connection.target.module_id,
                connection.id);
        }
        if (source_port == nullptr || target_port == nullptr) {
            continue;
        }
        if (!types_compatible(source_port->data_type, target_port->data_type)) {
            add_issue(
                validation,
                "incompatible_data_type",
                "Connection data types are incompatible",
                connection.target.module_id,
                connection.id);
        }
        if (!has_capabilities(source_port->capabilities, target_port->capabilities)) {
            add_issue(
                validation,
                "missing_capability",
                "Connection source does not provide all capabilities required by the target",
                connection.target.module_id,
                connection.id);
        }

        const auto target_key =
            connection.target.module_id + "\n" + connection.target.port_id;
        const auto count = ++target_counts[target_key];
        if (!target_port->accepts_multiple && count > 1) {
            add_issue(
                validation,
                "multiple_sources",
                "Input port accepts only one source",
                connection.target.module_id,
                connection.id);
        }

        adjacency[connection.source.module_id].push_back(connection.target.module_id);
        ++indegree[connection.target.module_id];
    }

    for (const auto& [module_id, instance] : modules) {
        const auto* type = registry_.find(instance->type_id);
        if (type == nullptr || !instance->enabled) {
            continue;
        }
        for (const auto& port : type->ports) {
            if (port.direction != PortDirection::Input || !port.required) {
                continue;
            }
            const auto key = module_id + "\n" + port.id;
            if (target_counts[key] == 0) {
                add_issue(
                    validation,
                    "missing_required_input",
                    "Enabled module has an unconnected required input",
                    module_id);
            }
        }
    }

    std::queue<std::string> roots;
    for (const auto& [module_id, degree] : indegree) {
        if (degree == 0) {
            roots.push(module_id);
        }
    }
    std::size_t visited = 0;
    while (!roots.empty()) {
        const auto module_id = roots.front();
        roots.pop();
        ++visited;
        for (const auto& target : adjacency[module_id]) {
            if (--indegree[target] == 0) {
                roots.push(target);
            }
        }
    }
    if (visited != modules.size()) {
        add_issue(
            validation,
            "cycle",
            "Module graph must be acyclic");
    }

    return validation;
}

GraphApplyResult ModuleGraph::apply(
    ModuleGraphDocument document,
    std::uint64_t expected_revision) {
    std::unique_lock lock(mutex_);
    if (expected_revision != document_.revision) {
        return {
            false,
            document_.revision,
            {{
                "revision_conflict",
                "Expected graph revision does not match the current revision",
                {},
                {},
            }},
        };
    }
    auto validation = validate_unlocked(document);
    if (!validation.valid()) {
        return {false, document_.revision, std::move(validation.issues)};
    }
    document.revision = document_.revision + 1;
    document_ = std::move(document);
    return {true, document_.revision, {}};
}

GraphValidation ModuleGraph::restore(ModuleGraphDocument document) {
    std::unique_lock lock(mutex_);
    auto validation = validate_unlocked(document);
    if (validation.valid()) {
        document_ = std::move(document);
    }
    return validation;
}

std::vector<CompatibleSource> ModuleGraph::compatible_sources(
    const ModuleGraphDocument& document,
    const ModuleEndpoint& target) const {
    std::shared_lock lock(mutex_);
    const auto target_instance = std::find_if(
        document.modules.begin(),
        document.modules.end(),
        [&](const ModuleInstance& module) { return module.id == target.module_id; });
    if (target_instance == document.modules.end()) {
        return {};
    }
    const auto* target_type = registry_.find(target_instance->type_id);
    if (target_type == nullptr) {
        return {};
    }
    const auto* target_port = find_port(*target_type, target.port_id, PortDirection::Input);
    if (target_port == nullptr) {
        return {};
    }

    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    for (const auto& connection : document.connections) {
        adjacency[connection.source.module_id].push_back(connection.target.module_id);
    }
    const auto would_create_cycle = [&](const std::string& source_module) {
        std::vector<std::string> pending{target.module_id};
        std::unordered_set<std::string> visited;
        while (!pending.empty()) {
            auto current = std::move(pending.back());
            pending.pop_back();
            if (!visited.insert(current).second) {
                continue;
            }
            if (current == source_module) {
                return true;
            }
            for (const auto& next : adjacency[current]) {
                pending.push_back(next);
            }
        }
        return false;
    };

    std::vector<CompatibleSource> result;
    for (const auto& module : document.modules) {
        if (!module.enabled || would_create_cycle(module.id)) {
            continue;
        }
        const auto* type = registry_.find(module.type_id);
        if (type == nullptr) {
            continue;
        }
        for (const auto& port : type->ports) {
            if (port.direction == PortDirection::Output &&
                types_compatible(port.data_type, target_port->data_type) &&
                has_capabilities(port.capabilities, target_port->capabilities)) {
                result.push_back({
                    {module.id, port.id},
                    module.name,
                    port.name,
                    port.data_type,
                    port.capabilities,
                });
            }
        }
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const CompatibleSource& left, const CompatibleSource& right) {
            if (left.module_name != right.module_name) {
                return left.module_name < right.module_name;
            }
            return left.port_name < right.port_name;
        });
    return result;
}

} // namespace pamguard::core
