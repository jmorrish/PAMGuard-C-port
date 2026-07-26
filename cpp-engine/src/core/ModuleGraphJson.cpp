#include "pamguard/core/ModuleGraphJson.h"

#include <stdexcept>

#include <json.hpp>

namespace pamguard::core {

namespace {

using Json = nlohmann::json;

Json parse_object(const std::string& value, const char* field_name) {
    const auto parsed = Json::parse(value);
    if (!parsed.is_object()) {
        throw std::invalid_argument(std::string(field_name) + " must contain a JSON object");
    }
    return parsed;
}

} // namespace

std::string module_graph_to_json(
    const ModuleGraphDocument& document,
    bool pretty) {
    Json root = {
        {"schemaVersion", document.schema_version},
        {"revision", document.revision},
        {"modules", Json::array()},
        {"connections", Json::array()},
        {"acquisition",
         parse_object(
             document.acquisition_json,
             "Graph acquisition policy")},
        {"clock",
         parse_object(document.clock_json, "Graph clock policy")},
        {"persistence",
         parse_object(
             document.persistence_json,
             "Graph persistence policy")},
    };
    for (const auto& module : document.modules) {
        root["modules"].push_back({
            {"id", module.id},
            {"typeId", module.type_id},
            {"name", module.name},
            {"enabled", module.enabled},
            {"settings", parse_object(module.settings_json, "Module settings")},
        });
    }
    for (const auto& connection : document.connections) {
        root["connections"].push_back({
            {"id", connection.id},
            {"source", {
                {"moduleId", connection.source.module_id},
                {"portId", connection.source.port_id},
            }},
            {"target", {
                {"moduleId", connection.target.module_id},
                {"portId", connection.target.port_id},
            }},
        });
    }
    return root.dump(pretty ? 2 : -1);
}

ModuleGraphDocument module_graph_from_json(std::string_view json) {
    const auto root = Json::parse(json);
    if (!root.is_object()) {
        throw std::invalid_argument("Module graph must be a JSON object");
    }

    ModuleGraphDocument document;
    document.schema_version = root.at("schemaVersion").get<std::uint32_t>();
    document.revision = root.value("revision", std::uint64_t{0});
    for (const auto* field :
         {"acquisition", "clock", "persistence"}) {
        if (root.contains(field) && !root.at(field).is_object()) {
            throw std::invalid_argument(
                std::string("Graph ") + field +
                " policy must be a JSON object");
        }
    }
    document.acquisition_json =
        root.value("acquisition", Json::object()).dump();
    document.clock_json =
        root.value("clock", Json::object()).dump();
    document.persistence_json =
        root.value("persistence", Json::object()).dump();
    for (const auto& value : root.at("modules")) {
        if (!value.is_object() || !value.at("settings").is_object()) {
            throw std::invalid_argument("Each module and its settings must be JSON objects");
        }
        document.modules.push_back({
            value.at("id").get<std::string>(),
            value.at("typeId").get<std::string>(),
            value.at("name").get<std::string>(),
            value.value("enabled", true),
            value.at("settings").dump(),
        });
    }
    for (const auto& value : root.at("connections")) {
        document.connections.push_back({
            value.at("id").get<std::string>(),
            {
                value.at("source").at("moduleId").get<std::string>(),
                value.at("source").at("portId").get<std::string>(),
            },
            {
                value.at("target").at("moduleId").get<std::string>(),
                value.at("target").at("portId").get<std::string>(),
            },
        });
    }
    return document;
}

} // namespace pamguard::core
