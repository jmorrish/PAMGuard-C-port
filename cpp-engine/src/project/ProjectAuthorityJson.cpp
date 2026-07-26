#include "pamguard/project/ProjectAuthorityJson.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <json.hpp>

#include "CanonicalJson.h"
#include "pamguard/core/ModuleGraphJson.h"
#include "pamguard/project/ProjectJson.h"

namespace pamguard::project {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumMutationBodyBytes =
    2U * 1024U * 1024U;
constexpr std::size_t kMaximumCommandBodyBytes = 64U * 1024U;
constexpr std::size_t kMaximumOperations = 1024;
constexpr std::size_t kMaximumSources = 64;
constexpr std::size_t kMaximumControlledUnits = 1024;
constexpr std::size_t kMaximumDataModelNodes = 1024;
constexpr std::size_t kMaximumDisplayTabs = 128;
constexpr std::size_t kMaximumDisplaysPerTab = 128;
constexpr std::size_t kMaximumDisplays = 1024;
constexpr std::size_t kMaximumLayoutItems = 128;
// Leaves room for the longest documented ":clickDetector" child suffix
// while keeping every generated clientRef within ProjectEntityId's 128 bytes.
constexpr std::size_t kMaximumTemplateClientRefBytes = 114;
constexpr std::size_t kMaximumEmbeddedSettingsBytes =
    1U * 1024U * 1024U;
constexpr double kMaximumCoordinateMagnitude = 1'000'000.0;

[[noreturn]] void fail(
    std::string_view path,
    const std::string& message) {
    throw ProjectAuthorityJsonError(
        std::string(path) + ": " + message);
}

std::string child_path(
    std::string_view path,
    std::string_view child) {
    return path == "/"
        ? "/" + std::string(child)
        : std::string(path) + "/" + std::string(child);
}

std::string index_path(
    std::string_view path,
    std::size_t index) {
    return std::string(path) + "/" + std::to_string(index);
}

Json parse_body(
    std::string_view encoded,
    std::string_view context,
    std::size_t maximum_bytes) {
    if (encoded.size() > maximum_bytes) {
        fail(
            context,
            "body exceeds " + std::to_string(maximum_bytes) +
                " bytes");
    }
    try {
        validate_strict_json(encoded);
        return Json::parse(encoded.begin(), encoded.end());
    }
    catch (const ProjectAuthorityJsonError&) {
        throw;
    }
    catch (const std::exception& error) {
        fail(context, error.what());
    }
}

void require_fields(
    const Json& value,
    std::string_view path,
    std::initializer_list<std::string_view> required,
    std::initializer_list<std::string_view> optional = {}) {
    if (!value.is_object()) {
        fail(path, "must be an object");
    }
    std::set<std::string_view> allowed(required);
    allowed.insert(optional.begin(), optional.end());
    for (auto entry = value.begin();
         entry != value.end();
         ++entry) {
        if (!allowed.contains(entry.key())) {
            fail(
                child_path(path, entry.key()),
                "unknown field");
        }
    }
    for (const auto field : required) {
        if (!value.contains(std::string(field))) {
            fail(
                child_path(path, field),
                "required field is missing");
        }
    }
}

const Json& field(
    const Json& object,
    std::string_view path,
    std::string_view name) {
    const auto found = object.find(std::string(name));
    if (found == object.end()) {
        fail(
            child_path(path, name),
            "required field is missing");
    }
    return *found;
}

std::string string_field(
    const Json& object,
    std::string_view path,
    std::string_view name) {
    const auto& value = field(object, path, name);
    if (!value.is_string()) {
        fail(child_path(path, name), "must be a string");
    }
    return value.get<std::string>();
}

bool bool_field(
    const Json& object,
    std::string_view path,
    std::string_view name) {
    const auto& value = field(object, path, name);
    if (!value.is_boolean()) {
        fail(child_path(path, name), "must be a boolean");
    }
    return value.get<bool>();
}

std::uint64_t unsigned_field(
    const Json& object,
    std::string_view path,
    std::string_view name,
    std::uint64_t minimum = 0,
    std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max()) {
    const auto item_path = child_path(path, name);
    const auto& value = field(object, path, name);
    std::uint64_t result = 0;
    if (value.is_number_unsigned()) {
        result = value.get<std::uint64_t>();
    }
    else if (value.is_number_integer()) {
        const auto signed_value = value.get<std::int64_t>();
        if (signed_value < 0) {
            fail(item_path, "must be non-negative");
        }
        result = static_cast<std::uint64_t>(signed_value);
    }
    else {
        fail(item_path, "must be an integer");
    }
    if (result < minimum || result > maximum) {
        fail(
            item_path,
            "must be in " + std::to_string(minimum) + ".." +
                std::to_string(maximum));
    }
    return result;
}

double finite_field(
    const Json& object,
    std::string_view path,
    std::string_view name) {
    const auto item_path = child_path(path, name);
    const auto& value = field(object, path, name);
    if (!value.is_number()) {
        fail(item_path, "must be a number");
    }
    const auto result = value.get<double>();
    if (!std::isfinite(result)) {
        fail(item_path, "must be finite");
    }
    return result == 0.0 ? 0.0 : result;
}

const Json& bounded_array(
    const Json& object,
    std::string_view path,
    std::string_view name,
    std::size_t maximum) {
    const auto item_path = child_path(path, name);
    const auto& value = field(object, path, name);
    if (!value.is_array()) {
        fail(item_path, "must be an array");
    }
    if (value.size() > maximum) {
        fail(
            item_path,
            "cannot contain more than " +
                std::to_string(maximum) + " entries");
    }
    return value;
}

void require_uuid_value(
    std::string_view value,
    std::string_view path) {
    if (!is_uuid_v4(value)) {
        fail(path, "must be a lowercase RFC 4122 UUIDv4");
    }
}

void require_entity_value(
    std::string_view value,
    std::string_view path) {
    if (!is_entity_id(value)) {
        fail(path, "must be a stable entity ID");
    }
}

void require_role_value(
    std::string_view value,
    std::string_view path) {
    if (!is_role_id(value)) {
        fail(path, "must be a lower-camel role ID");
    }
}

std::string canonical_object(
    const Json& value,
    std::string_view path) {
    if (!value.is_object()) {
        fail(path, "must be an object");
    }
    auto result = detail::canonical_json_dump(value);
    if (result.size() > kMaximumEmbeddedSettingsBytes) {
        fail(
            path,
            "object exceeds " +
                std::to_string(kMaximumEmbeddedSettingsBytes) +
                " canonical bytes");
    }
    return result;
}

ProjectEntityReference parse_entity_reference(
    const Json& value,
    std::string_view path) {
    if (!value.is_object() || value.size() != 1) {
        fail(path, "must contain exactly one id or clientRef");
    }
    ProjectEntityReference result;
    if (value.contains("id")) {
        require_fields(value, path, {"id"});
        result.id = string_field(value, path, "id");
        require_uuid_value(*result.id, child_path(path, "id"));
        return result;
    }
    if (value.contains("clientRef")) {
        require_fields(value, path, {"clientRef"});
        result.client_ref =
            string_field(value, path, "clientRef");
        require_entity_value(
            *result.client_ref,
            child_path(path, "clientRef"));
        return result;
    }
    fail(path, "must contain exactly one id or clientRef");
}

MutationSourceReference parse_mutation_source(
    const Json& value,
    std::string_view path) {
    require_fields(value, path, {"unit", "outputRole"});
    auto output_role = string_field(value, path, "outputRole");
    require_role_value(
        output_role,
        child_path(path, "outputRole"));
    return {
        parse_entity_reference(
            field(value, path, "unit"),
            child_path(path, "unit")),
        std::move(output_role),
    };
}

SourceReference parse_source(
    const Json& value,
    std::string_view path) {
    require_fields(value, path, {"unitId", "outputRole"});
    auto unit_id = string_field(value, path, "unitId");
    auto output_role = string_field(value, path, "outputRole");
    require_uuid_value(unit_id, child_path(path, "unitId"));
    require_role_value(
        output_role,
        child_path(path, "outputRole"));
    return {std::move(unit_id), std::move(output_role)};
}

DisplayOwner parse_owner(
    const Json& value,
    std::string_view path) {
    require_fields(value, path, {"unitId", "role"});
    auto unit_id = string_field(value, path, "unitId");
    auto role = string_field(value, path, "role");
    require_uuid_value(unit_id, child_path(path, "unitId"));
    require_role_value(role, child_path(path, "role"));
    return {std::move(unit_id), std::move(role)};
}

DataModelLayout parse_data_model_layout(
    const Json& value,
    std::string_view path) {
    require_fields(value, path, {"nodes", "viewport"});
    DataModelLayout result;
    const auto& nodes = bounded_array(
        value,
        path,
        "nodes",
        kMaximumDataModelNodes);
    std::unordered_set<std::string> unit_ids;
    result.nodes.reserve(nodes.size());
    for (std::size_t index = 0;
         index < nodes.size();
         ++index) {
        const auto node_path =
            index_path(child_path(path, "nodes"), index);
        const auto& node = nodes[index];
        require_fields(node, node_path, {"unitId", "x", "y"});
        auto unit_id = string_field(node, node_path, "unitId");
        require_uuid_value(
            unit_id,
            child_path(node_path, "unitId"));
        if (!unit_ids.emplace(unit_id).second) {
            fail(
                child_path(node_path, "unitId"),
                "duplicate node position");
        }
        const auto x = finite_field(node, node_path, "x");
        const auto y = finite_field(node, node_path, "y");
        if (std::abs(x) > kMaximumCoordinateMagnitude ||
            std::abs(y) > kMaximumCoordinateMagnitude) {
            fail(node_path, "coordinates are out of range");
        }
        result.nodes.push_back({
            std::move(unit_id),
            x,
            y,
        });
    }

    const auto viewport_path = child_path(path, "viewport");
    const auto& viewport = field(value, path, "viewport");
    require_fields(
        viewport,
        viewport_path,
        {"x", "y", "zoom"});
    const auto x = finite_field(viewport, viewport_path, "x");
    const auto y = finite_field(viewport, viewport_path, "y");
    const auto zoom =
        finite_field(viewport, viewport_path, "zoom");
    if (std::abs(x) > kMaximumCoordinateMagnitude ||
        std::abs(y) > kMaximumCoordinateMagnitude ||
        zoom < 0.1 || zoom > 8.0) {
        fail(viewport_path, "viewport values are out of range");
    }
    result.viewport = {x, y, zoom};
    return result;
}

DisplayLayoutMode parse_display_layout_mode(
    const Json& value,
    std::string_view path) {
    if (!value.is_string()) {
        fail(path, "must be a string");
    }
    const auto& mode = value.get_ref<const std::string&>();
    if (mode == "grid") {
        return DisplayLayoutMode::Grid;
    }
    if (mode == "tabs") {
        return DisplayLayoutMode::Tabs;
    }
    fail(path, "must be 'grid' or 'tabs'");
}

std::vector<DisplayTab> parse_display_tabs(
    const Json& tabs,
    std::string_view path) {
    if (!tabs.is_array()) {
        fail(path, "must be an array");
    }
    if (tabs.size() > kMaximumDisplayTabs) {
        fail(path, "contains too many display tabs");
    }

    std::vector<DisplayTab> result;
    result.reserve(tabs.size());
    std::unordered_set<std::string> tab_ids;
    std::unordered_set<std::string> display_ids;
    std::size_t display_count = 0;

    for (std::size_t tab_index = 0;
         tab_index < tabs.size();
         ++tab_index) {
        const auto tab_path = index_path(path, tab_index);
        const auto& value = tabs[tab_index];
        require_fields(
            value,
            tab_path,
            {"id", "name", "owner", "displays", "layout"});
        DisplayTab tab;
        tab.id = string_field(value, tab_path, "id");
        require_entity_value(tab.id, child_path(tab_path, "id"));
        if (!tab_ids.emplace(tab.id).second) {
            fail(
                child_path(tab_path, "id"),
                "duplicate display-tab ID");
        }
        tab.name = trim_java_string(
            string_field(value, tab_path, "name"));
        if (tab.name.empty() ||
            java_utf16_code_unit_length(tab.name) > 128) {
            fail(
                child_path(tab_path, "name"),
                "must contain 1 to 128 UTF-16 code units");
        }
        tab.owner = parse_owner(
            field(value, tab_path, "owner"),
            child_path(tab_path, "owner"));

        const auto& displays = bounded_array(
            value,
            tab_path,
            "displays",
            kMaximumDisplaysPerTab);
        display_count += displays.size();
        if (display_count > kMaximumDisplays) {
            fail(path, "contains too many display instances");
        }
        std::unordered_set<std::string> tab_display_ids;
        tab.displays.reserve(displays.size());
        for (std::size_t display_index = 0;
             display_index < displays.size();
             ++display_index) {
            const auto display_path = index_path(
                child_path(tab_path, "displays"),
                display_index);
            const auto& display_value = displays[display_index];
            require_fields(
                display_value,
                display_path,
                {
                    "id",
                    "providerTypeId",
                    "providerVersion",
                    "owner",
                    "source",
                    "settingsVersion",
                    "settings",
                });
            DisplayInstance display;
            display.id =
                string_field(display_value, display_path, "id");
            require_entity_value(
                display.id,
                child_path(display_path, "id"));
            if (!display_ids.emplace(display.id).second) {
                fail(
                    child_path(display_path, "id"),
                    "duplicate display ID");
            }
            tab_display_ids.emplace(display.id);
            display.provider_type_id = string_field(
                display_value,
                display_path,
                "providerTypeId");
            require_entity_value(
                display.provider_type_id,
                child_path(display_path, "providerTypeId"));
            display.provider_version =
                static_cast<std::uint32_t>(
                    unsigned_field(
                        display_value,
                        display_path,
                        "providerVersion",
                        1,
                        std::numeric_limits<std::uint32_t>::max()));
            display.owner = parse_owner(
                field(display_value, display_path, "owner"),
                child_path(display_path, "owner"));
            const auto& source =
                field(display_value, display_path, "source");
            if (!source.is_null()) {
                display.source = parse_source(
                    source,
                    child_path(display_path, "source"));
            }
            display.settings_version =
                static_cast<std::uint32_t>(
                    unsigned_field(
                        display_value,
                        display_path,
                        "settingsVersion",
                        1,
                        std::numeric_limits<std::uint32_t>::max()));
            display.settings_json = canonical_object(
                field(display_value, display_path, "settings"),
                child_path(display_path, "settings"));
            tab.displays.push_back(std::move(display));
        }

        const auto layout_path = child_path(tab_path, "layout");
        const auto& layout = field(value, tab_path, "layout");
        require_fields(
            layout,
            layout_path,
            {"mode", "columns", "selectedDisplayId", "items"});
        tab.layout.mode = parse_display_layout_mode(
            field(layout, layout_path, "mode"),
            child_path(layout_path, "mode"));
        tab.layout.columns =
            static_cast<std::uint32_t>(
                unsigned_field(
                    layout,
                    layout_path,
                    "columns",
                    1,
                    24));
        const auto& selected =
            field(layout, layout_path, "selectedDisplayId");
        if (!selected.is_null()) {
            if (!selected.is_string()) {
                fail(
                    child_path(layout_path, "selectedDisplayId"),
                    "must be a string or null");
            }
            const auto selected_id =
                selected.get<std::string>();
            require_entity_value(
                selected_id,
                child_path(layout_path, "selectedDisplayId"));
            if (!tab_display_ids.contains(selected_id)) {
                fail(
                    child_path(layout_path, "selectedDisplayId"),
                    "must refer to a display in this tab");
            }
            tab.layout.selected_display_id = selected_id;
        }

        const auto& items = bounded_array(
            layout,
            layout_path,
            "items",
            kMaximumLayoutItems);
        std::unordered_set<std::string> item_ids;
        tab.layout.items.reserve(items.size());
        for (std::size_t item_index = 0;
             item_index < items.size();
             ++item_index) {
            const auto item_path = index_path(
                child_path(layout_path, "items"),
                item_index);
            const auto& item = items[item_index];
            require_fields(
                item,
                item_path,
                {"displayId", "column", "row", "width", "height"});
            auto display_id =
                string_field(item, item_path, "displayId");
            require_entity_value(
                display_id,
                child_path(item_path, "displayId"));
            if (!tab_display_ids.contains(display_id) ||
                !item_ids.emplace(display_id).second) {
                fail(
                    child_path(item_path, "displayId"),
                    "must uniquely refer to a display in this tab");
            }
            const auto column =
                static_cast<std::uint32_t>(
                    unsigned_field(
                        item,
                        item_path,
                        "column",
                        0,
                        23));
            const auto row =
                static_cast<std::uint32_t>(
                    unsigned_field(
                        item,
                        item_path,
                        "row",
                        0,
                        10000));
            const auto width =
                static_cast<std::uint32_t>(
                    unsigned_field(
                        item,
                        item_path,
                        "width",
                        1,
                        24));
            const auto height =
                static_cast<std::uint32_t>(
                    unsigned_field(
                        item,
                        item_path,
                        "height",
                        1,
                        1000));
            if (column + width > tab.layout.columns) {
                fail(item_path, "grid item exceeds the tab columns");
            }
            tab.layout.items.push_back({
                std::move(display_id),
                column,
                row,
                width,
                height,
            });
        }
        if (item_ids != tab_display_ids) {
            fail(
                child_path(layout_path, "items"),
                "must contain exactly one item per display");
        }
        result.push_back(std::move(tab));
    }
    return result;
}

DependencyPolicy parse_dependency_policy(
    const Json& value,
    std::string_view path) {
    if (!value.is_string()) {
        fail(path, "must be a string");
    }
    const auto& policy = value.get_ref<const std::string&>();
    if (policy == "reject") {
        return DependencyPolicy::Reject;
    }
    if (policy == "add-defaults") {
        return DependencyPolicy::AddDefaults;
    }
    fail(path, "must be 'reject' or 'add-defaults'");
}

DependantRemovalPolicy parse_dependant_policy(
    const Json& value,
    std::string_view path) {
    if (!value.is_string()) {
        fail(path, "must be a string");
    }
    const auto& policy = value.get_ref<const std::string&>();
    if (policy == "reject") {
        return DependantRemovalPolicy::Reject;
    }
    if (policy == "leave-unbound") {
        return DependantRemovalPolicy::LeaveUnbound;
    }
    fail(path, "must be 'reject' or 'leave-unbound'");
}

ProjectMutationOperation parse_operation(
    const Json& value,
    std::string_view path) {
    if (!value.is_object()) {
        fail(path, "must be an object");
    }
    const auto operation = string_field(value, path, "op");

    if (operation == "addControlledUnit") {
        require_fields(
            value,
            path,
            {
                "op",
                "clientRef",
                "typeId",
                "name",
                "dependencyPolicy",
            });
        AddControlledUnitOperation result;
        result.client_ref =
            string_field(value, path, "clientRef");
        require_entity_value(
            result.client_ref,
            child_path(path, "clientRef"));
        result.type_id = string_field(value, path, "typeId");
        require_entity_value(
            result.type_id,
            child_path(path, "typeId"));
        const auto& name = field(value, path, "name");
        if (!name.is_null()) {
            if (!name.is_string() ||
                !is_valid_java_item_name(
                    name.get_ref<const std::string&>())) {
                fail(
                    child_path(path, "name"),
                    "must be null or a valid PAMGuard item name");
            }
            result.name = name.get<std::string>();
        }
        result.dependency_policy = parse_dependency_policy(
            field(value, path, "dependencyPolicy"),
            child_path(path, "dependencyPolicy"));
        return result;
    }

    if (operation == "addConfigurationTemplate") {
        require_fields(
            value,
            path,
            {
                "op",
                "clientRef",
                "templateId",
            });
        AddConfigurationTemplateOperation result;
        result.client_ref =
            string_field(value, path, "clientRef");
        require_entity_value(
            result.client_ref,
            child_path(path, "clientRef"));
        if (result.client_ref.size() >
            kMaximumTemplateClientRefBytes) {
            fail(
                child_path(path, "clientRef"),
                "is too long to create stable template child "
                "references");
        }
        result.template_id =
            string_field(value, path, "templateId");
        require_entity_value(
            result.template_id,
            child_path(path, "templateId"));
        if (result.template_id !=
            kClickMonitoringConfigurationTemplateId) {
            fail(
                child_path(path, "templateId"),
                "must be 'pamguard.click-monitoring'");
        }
        return result;
    }

    if (operation == "renameControlledUnit") {
        require_fields(value, path, {"op", "unit", "name"});
        const auto name = string_field(value, path, "name");
        if (!is_valid_java_item_name(name)) {
            fail(
                child_path(path, "name"),
                "must be a valid PAMGuard item name");
        }
        return RenameControlledUnitOperation{
            parse_entity_reference(
                field(value, path, "unit"),
                child_path(path, "unit")),
            name,
        };
    }

    if (operation == "removeControlledUnit") {
        require_fields(
            value,
            path,
            {"op", "unit", "dependantPolicy"});
        return RemoveControlledUnitOperation{
            parse_entity_reference(
                field(value, path, "unit"),
                child_path(path, "unit")),
            parse_dependant_policy(
                field(value, path, "dependantPolicy"),
                child_path(path, "dependantPolicy")),
        };
    }

    if (operation == "reorderControlledUnits") {
        require_fields(value, path, {"op", "units"});
        const auto& units = bounded_array(
            value,
            path,
            "units",
            kMaximumControlledUnits);
        ReorderControlledUnitsOperation result;
        result.units.reserve(units.size());
        for (std::size_t index = 0;
             index < units.size();
             ++index) {
            result.units.push_back(
                parse_entity_reference(
                    units[index],
                    index_path(child_path(path, "units"), index)));
        }
        return result;
    }

    if (operation == "replaceSettings") {
        require_fields(
            value,
            path,
            {"op", "unit", "settingsVersion", "settings"});
        return ReplaceControlledUnitSettingsOperation{
            parse_entity_reference(
                field(value, path, "unit"),
                child_path(path, "unit")),
            static_cast<std::uint32_t>(
                unsigned_field(
                    value,
                    path,
                    "settingsVersion",
                    1,
                    std::numeric_limits<std::uint32_t>::max())),
            canonical_object(
                field(value, path, "settings"),
                child_path(path, "settings")),
        };
    }

    if (operation == "replaceGlobalSettings") {
        require_fields(
            value,
            path,
            {"op", "typeId", "settingsVersion", "settings"});
        auto type_id =
            string_field(value, path, "typeId");
        require_entity_value(
            type_id,
            child_path(path, "typeId"));
        return ReplaceGlobalSettingsOperation{
            std::move(type_id),
            static_cast<std::uint32_t>(
                unsigned_field(
                    value,
                    path,
                    "settingsVersion",
                    1,
                    std::numeric_limits<std::uint32_t>::max())),
            canonical_object(
                field(value, path, "settings"),
                child_path(path, "settings")),
        };
    }

    if (operation == "setBinding") {
        require_fields(
            value,
            path,
            {"op", "unit", "inputRole", "sources"});
        SetControlledUnitBindingOperation result;
        result.unit = parse_entity_reference(
            field(value, path, "unit"),
            child_path(path, "unit"));
        result.input_role =
            string_field(value, path, "inputRole");
        require_role_value(
            result.input_role,
            child_path(path, "inputRole"));
        const auto& sources = bounded_array(
            value,
            path,
            "sources",
            kMaximumSources);
        result.sources.reserve(sources.size());
        std::set<std::string> source_keys;
        for (std::size_t index = 0;
             index < sources.size();
             ++index) {
            const auto source_path =
                index_path(child_path(path, "sources"), index);
            auto source =
                parse_mutation_source(sources[index], source_path);
            const auto reference_key = source.unit.id
                ? "id:" + *source.unit.id
                : "clientRef:" + *source.unit.client_ref;
            if (!source_keys.emplace(
                    reference_key + "\n" +
                    source.output_role)
                     .second) {
                fail(source_path, "duplicate source reference");
            }
            result.sources.push_back(std::move(source));
        }
        return result;
    }

    if (operation == "replaceDataModelLayout") {
        require_fields(value, path, {"op", "layout"});
        return ReplaceDataModelLayoutOperation{
            parse_data_model_layout(
                field(value, path, "layout"),
                child_path(path, "layout")),
        };
    }

    if (operation == "replaceDisplayHierarchy") {
        require_fields(value, path, {"op", "displayTabs"});
        return ReplaceDisplayHierarchyOperation{
            parse_display_tabs(
                field(value, path, "displayTabs"),
                child_path(path, "displayTabs")),
        };
    }

    fail(
        child_path(path, "op"),
        "unknown operation '" + operation + "'");
}

Json parse_generated_json(
    const std::string& encoded,
    std::string_view context) {
    try {
        return Json::parse(encoded);
    }
    catch (const std::exception& error) {
        fail(context, error.what());
    }
}

Json parse_generated_object(
    const std::string& encoded,
    std::string_view context) {
    auto value = parse_generated_json(encoded, context);
    if (!value.is_object()) {
        fail(context, "must encode an object");
    }
    return value;
}

Json nullable_string(const std::string& value) {
    return value.empty() ? Json(nullptr) : Json(value);
}

template <typename Value>
Json optional_json(const std::optional<Value>& value) {
    return value ? Json(*value) : Json(nullptr);
}

Json string_array(const std::vector<std::string>& values) {
    Json result = Json::array();
    for (const auto& value : values) {
        result.push_back(value);
    }
    return result;
}

Json source_json(const SourceReference& source) {
    return {
        {"unitId", source.unit_id},
        {"outputRole", source.output_role},
    };
}

Json sources_json(const std::vector<SourceReference>& sources) {
    Json result = Json::array();
    for (const auto& source : sources) {
        result.push_back(source_json(source));
    }
    return result;
}

std::string projection_status_string(
    ProjectionStatus status) {
    switch (status) {
    case ProjectionStatus::Invalid:
        return "invalid";
    case ProjectionStatus::NeedsConfiguration:
        return "needs-configuration";
    case ProjectionStatus::Runnable:
        return "runnable";
    }
    fail("/projection/status", "unknown projection status");
}

std::string issue_class_string(
    ProjectionIssueClass issue_class) {
    switch (issue_class) {
    case ProjectionIssueClass::EditorInvalid:
        return "editor-invalid";
    case ProjectionIssueClass::NeedsConfiguration:
        return "needs-configuration";
    }
    fail("/projection/issues", "unknown projection issue class");
}

std::string connection_kind_string(
    ProjectedConnectionKind kind) {
    switch (kind) {
    case ProjectedConnectionKind::Internal:
        return "internal";
    case ProjectedConnectionKind::External:
        return "external";
    }
    fail(
        "/projection/connections/kind",
        "unknown projected connection kind");
}

Json projection_issues_json(
    const std::vector<ProjectionIssue>& issues) {
    Json result = Json::array();
    for (const auto& issue : issues) {
        result.push_back({
            {"class", issue_class_string(issue.issue_class)},
            {"code", issue.code},
            {"message", issue.message},
            {"unitId", nullable_string(issue.unit_id)},
            {"roleId", nullable_string(issue.role_id)},
            {"displayId", nullable_string(issue.display_id)},
        });
    }
    return result;
}

Json public_output_json(const ProjectedPublicOutput& output) {
    return {
        {"unitId", output.unit_id},
        {"outputRole", output.output_role},
        {"runtimeNodeId", output.runtime_node_id},
        {"runtimePortId", output.runtime_port_id},
        {"blockId", output.block_id},
        {"dataType", output.data_type},
        {"capabilities", string_array(output.capabilities)},
    };
}

Json public_outputs_json(
    const std::vector<ProjectedPublicOutput>& outputs) {
    Json result = Json::array();
    for (const auto& output : outputs) {
        result.push_back(public_output_json(output));
    }
    return result;
}

Json data_model_layout_json(const DataModelLayout& layout) {
    Json nodes = Json::array();
    for (const auto& node : layout.nodes) {
        nodes.push_back({
            {"unitId", node.unit_id},
            {"x", node.x},
            {"y", node.y},
        });
    }
    return {
        {"nodes", std::move(nodes)},
        {
            "viewport",
            {
                {"x", layout.viewport.x},
                {"y", layout.viewport.y},
                {"zoom", layout.viewport.zoom},
            },
        },
    };
}

std::string display_layout_mode_string(
    DisplayLayoutMode mode) {
    switch (mode) {
    case DisplayLayoutMode::Grid:
        return "grid";
    case DisplayLayoutMode::Tabs:
        return "tabs";
    }
    fail("/displayTabs/layout/mode", "unknown display layout mode");
}

Json display_tabs_json(const std::vector<DisplayTab>& tabs) {
    Json result = Json::array();
    for (const auto& tab : tabs) {
        Json displays = Json::array();
        for (const auto& display : tab.displays) {
            displays.push_back({
                {"id", display.id},
                {"providerTypeId", display.provider_type_id},
                {"providerVersion", display.provider_version},
                {
                    "owner",
                    {
                        {"unitId", display.owner.unit_id},
                        {"role", display.owner.role},
                    },
                },
                {
                    "source",
                    display.source
                        ? source_json(*display.source)
                        : Json(nullptr),
                },
                {"settingsVersion", display.settings_version},
                {
                    "settings",
                    parse_generated_object(
                        display.settings_json,
                        "/displayTabs/settings"),
                },
            });
        }
        Json items = Json::array();
        for (const auto& item : tab.layout.items) {
            items.push_back({
                {"displayId", item.display_id},
                {"column", item.column},
                {"row", item.row},
                {"width", item.width},
                {"height", item.height},
            });
        }
        result.push_back({
            {"id", tab.id},
            {"name", tab.name},
            {
                "owner",
                {
                    {"unitId", tab.owner.unit_id},
                    {"role", tab.owner.role},
                },
            },
            {"displays", std::move(displays)},
            {
                "layout",
                {
                    {"mode", display_layout_mode_string(tab.layout.mode)},
                    {"columns", tab.layout.columns},
                    {
                        "selectedDisplayId",
                        optional_json(
                            tab.layout.selected_display_id),
                    },
                    {"items", std::move(items)},
                },
            },
        });
    }
    return result;
}

Json entity_reference_json(
    const ProjectEntityReference& reference) {
    if (reference.id.has_value() ==
        reference.client_ref.has_value()) {
        fail(
            "/operations/unit",
            "must contain exactly one id or clientRef");
    }
    if (reference.id) {
        require_uuid_value(*reference.id, "/operations/unit/id");
        return {{"id", *reference.id}};
    }
    require_entity_value(
        *reference.client_ref,
        "/operations/unit/clientRef");
    return {{"clientRef", *reference.client_ref}};
}

Json mutation_source_json(
    const MutationSourceReference& source) {
    return {
        {"unit", entity_reference_json(source.unit)},
        {"outputRole", source.output_role},
    };
}

std::string dependency_policy_string(
    DependencyPolicy policy) {
    switch (policy) {
    case DependencyPolicy::Reject:
        return "reject";
    case DependencyPolicy::AddDefaults:
        return "add-defaults";
    }
    fail("/operations/dependencyPolicy", "unknown dependency policy");
}

std::string dependant_policy_string(
    DependantRemovalPolicy policy) {
    switch (policy) {
    case DependantRemovalPolicy::Reject:
        return "reject";
    case DependantRemovalPolicy::LeaveUnbound:
        return "leave-unbound";
    }
    fail("/operations/dependantPolicy", "unknown dependant policy");
}

Json operation_json(const ProjectMutationOperation& operation) {
    return std::visit(
        [](const auto& value) -> Json {
            using Operation = std::decay_t<decltype(value)>;
            if constexpr (
                std::is_same_v<
                    Operation,
                    AddControlledUnitOperation>) {
                return {
                    {"op", "addControlledUnit"},
                    {"clientRef", value.client_ref},
                    {"typeId", value.type_id},
                    {
                        "name",
                        value.name
                            ? Json(*value.name)
                            : Json(nullptr),
                    },
                    {
                        "dependencyPolicy",
                        dependency_policy_string(
                            value.dependency_policy),
                    },
                };
            }
            else if constexpr (
                std::is_same_v<
                    Operation,
                    AddConfigurationTemplateOperation>) {
                return {
                    {"op", "addConfigurationTemplate"},
                    {"clientRef", value.client_ref},
                    {"templateId", value.template_id},
                };
            }
            else if constexpr (
                std::is_same_v<
                    Operation,
                    RenameControlledUnitOperation>) {
                return {
                    {"op", "renameControlledUnit"},
                    {"unit", entity_reference_json(value.unit)},
                    {"name", value.name},
                };
            }
            else if constexpr (
                std::is_same_v<
                    Operation,
                    RemoveControlledUnitOperation>) {
                return {
                    {"op", "removeControlledUnit"},
                    {"unit", entity_reference_json(value.unit)},
                    {
                        "dependantPolicy",
                        dependant_policy_string(
                            value.dependant_policy),
                    },
                };
            }
            else if constexpr (
                std::is_same_v<
                    Operation,
                    ReorderControlledUnitsOperation>) {
                Json units = Json::array();
                for (const auto& unit : value.units) {
                    units.push_back(entity_reference_json(unit));
                }
                return {
                    {"op", "reorderControlledUnits"},
                    {"units", std::move(units)},
                };
            }
            else if constexpr (
                std::is_same_v<
                    Operation,
                    ReplaceControlledUnitSettingsOperation>) {
                return {
                    {"op", "replaceSettings"},
                    {"unit", entity_reference_json(value.unit)},
                    {"settingsVersion", value.settings_version},
                    {
                        "settings",
                        parse_generated_object(
                            value.settings_json,
                            "/operations/settings"),
                    },
                };
            }
            else if constexpr (
                std::is_same_v<
                    Operation,
                    ReplaceGlobalSettingsOperation>) {
                return {
                    {"op", "replaceGlobalSettings"},
                    {"typeId", value.type_id},
                    {"settingsVersion", value.settings_version},
                    {
                        "settings",
                        parse_generated_object(
                            value.settings_json,
                            "/operations/settings"),
                    },
                };
            }
            else if constexpr (
                std::is_same_v<
                    Operation,
                    SetControlledUnitBindingOperation>) {
                Json sources = Json::array();
                for (const auto& source : value.sources) {
                    sources.push_back(
                        mutation_source_json(source));
                }
                return {
                    {"op", "setBinding"},
                    {"unit", entity_reference_json(value.unit)},
                    {"inputRole", value.input_role},
                    {"sources", std::move(sources)},
                };
            }
            else if constexpr (
                std::is_same_v<
                    Operation,
                    ReplaceDataModelLayoutOperation>) {
                return {
                    {"op", "replaceDataModelLayout"},
                    {
                        "layout",
                        data_model_layout_json(value.layout),
                    },
                };
            }
            else {
                return {
                    {"op", "replaceDisplayHierarchy"},
                    {
                        "displayTabs",
                        display_tabs_json(value.display_tabs),
                    },
                };
            }
        },
        operation);
}

Json snapshot_json(const ActiveProjectSnapshot& snapshot) {
    return {
        {"schemaVersion", kProjectAuthorityJsonSchemaVersion},
        {
            "project",
            parse_generated_json(
                project_document_to_json(snapshot.project),
                "/project"),
        },
        {"workingRevision", snapshot.working_revision},
        {
            "savedRevision",
            optional_json(snapshot.saved_revision),
        },
        {"authorityRevision", snapshot.authority_revision},
        {"workingContentHash", snapshot.working_content_hash},
        {
            "savedContentHash",
            optional_json(snapshot.saved_content_hash),
        },
        {"dirty", snapshot.dirty},
        {"etag", snapshot.etag},
        {
            "projection",
            {
                {
                    "status",
                    projection_status_string(
                        snapshot.projection.status()),
                },
                {
                    "issues",
                    projection_issues_json(
                        snapshot.projection.issues),
                },
            },
        },
    };
}

Json inspection_projection_json(
    const ProjectProjectionResult& projection) {
    Json runtime_children = Json::array();
    for (const auto& node : projection.index.runtime_nodes) {
        runtime_children.push_back({
            {"ownerUnitId", node.owner_unit_id},
            {"childRole", node.child_role_id},
            {"runtimeNodeId", node.runtime_node_id},
            {"runtimeTypeId", node.runtime_type_id},
        });
    }

    Json data_blocks = Json::array();
    for (const auto& block : projection.index.data_blocks) {
        data_blocks.push_back({
            {"ownerUnitId", block.owner_unit_id},
            {"childRole", block.child_role_id},
            {"runtimeNodeId", block.runtime_node_id},
            {"runtimePortId", block.port_id},
            {"blockId", block.block_id},
            {"dataType", block.data_type},
            {"capabilities", string_array(block.capabilities)},
        });
    }

    Json public_inputs = Json::array();
    for (const auto& input : projection.index.public_inputs) {
        public_inputs.push_back({
            {"unitId", input.unit_id},
            {"inputRole", input.input_role},
            {"runtimeNodeId", input.runtime_node_id},
            {"runtimePortId", input.runtime_port_id},
            {"dataType", input.data_type},
            {"cardinality", to_string(input.cardinality)},
            {"sources", sources_json(input.sources)},
            {
                "connectionIds",
                string_array(input.connection_ids),
            },
        });
    }

    Json connections = Json::array();
    for (const auto& connection :
         projection.index.connections) {
        connections.push_back({
            {"id", connection.connection_id},
            {
                "kind",
                connection_kind_string(connection.kind),
            },
            {"ownerUnitId", connection.owner_unit_id},
            {
                "internalEdgeRole",
                nullable_string(
                    connection.internal_edge_role),
            },
            {
                "targetInputRole",
                nullable_string(
                    connection.target_input_role),
            },
            {
                "publicSource",
                connection.public_source
                    ? source_json(*connection.public_source)
                    : Json(nullptr),
            },
        });
    }

    Json display_tabs = Json::array();
    for (const auto& tab : projection.index.display_tabs) {
        display_tabs.push_back({
            {"tabId", tab.tab_id},
            {"ownerUnitId", tab.owner_unit_id},
            {"ownerRole", tab.owner_role},
        });
    }

    Json displays = Json::array();
    for (const auto& display : projection.index.displays) {
        displays.push_back({
            {"tabId", display.tab_id},
            {"displayId", display.display_id},
            {"ownerUnitId", display.owner_unit_id},
            {"ownerRole", display.owner_role},
            {"providerTypeId", display.provider_type_id},
            {
                "publicSource",
                display.public_source
                    ? source_json(*display.public_source)
                    : Json(nullptr),
            },
            {
                "sourceBlockId",
                optional_json(display.source_block_id),
            },
        });
    }

    return {
        {
            "status",
            projection_status_string(projection.status()),
        },
        {"issues", projection_issues_json(projection.issues)},
        {"runtimeChildren", std::move(runtime_children)},
        {"dataBlocks", std::move(data_blocks)},
        {
            "publicOutputs",
            public_outputs_json(projection.index.public_outputs),
        },
        {"publicInputs", std::move(public_inputs)},
        {"connections", std::move(connections)},
        {"displayTabs", std::move(display_tabs)},
        {"displays", std::move(displays)},
        {
            "graph",
            parse_generated_json(
                core::module_graph_to_json(projection.graph),
                "/projection/graph"),
        },
    };
}

std::string saved_status_string(SavedProjectStatus status) {
    switch (status) {
    case SavedProjectStatus::Available:
        return "available";
    case SavedProjectStatus::Corrupt:
        return "corrupt";
    case SavedProjectStatus::Unsupported:
        return "unsupported";
    }
    fail("/projects/status", "unknown saved-project status");
}

std::string dump(Json value, bool pretty) {
    try {
        detail::normalize_json_numbers(
            value,
            "Project authority JSON",
            detail::kMaximumJsonDepth);
        return value.dump(
            pretty ? 2 : -1,
            ' ',
            false,
            Json::error_handler_t::strict);
    }
    catch (const ProjectAuthorityJsonError&) {
        throw;
    }
    catch (const std::exception& error) {
        fail("/", error.what());
    }
}

std::string parse_project_name(
    const Json& value,
    std::string_view path) {
    if (!value.is_string()) {
        fail(path, "must be a string");
    }
    auto name = trim_java_string(value.get<std::string>());
    if (name.empty() ||
        java_utf16_code_unit_length(name) > 128) {
        fail(path, "must contain 1 to 128 UTF-16 code units");
    }
    return name;
}

} // namespace

std::string active_project_snapshot_to_json(
    const ActiveProjectSnapshot& snapshot,
    bool pretty) {
    return dump(snapshot_json(snapshot), pretty);
}

std::string project_inspection_to_json(
    const ActiveProjectSnapshot& snapshot,
    bool pretty) {
    return dump(
        {
            {
                "schemaVersion",
                kProjectAuthorityJsonSchemaVersion,
            },
            {"projectId", snapshot.project.project_id},
            {"workingRevision", snapshot.working_revision},
            {
                "authorityRevision",
                snapshot.authority_revision,
            },
            {
                "projection",
                inspection_projection_json(snapshot.projection),
            },
        },
        pretty);
}

std::string project_compatible_sources_to_json(
    std::string_view unit_id,
    std::string_view input_role,
    const std::vector<ProjectedPublicOutput>& sources,
    bool pretty) {
    require_uuid_value(unit_id, "/target/unitId");
    require_role_value(input_role, "/target/inputRole");
    return dump(
        {
            {
                "schemaVersion",
                kProjectAuthorityJsonSchemaVersion,
            },
            {
                "target",
                {
                    {"unitId", unit_id},
                    {"inputRole", input_role},
                },
            },
            {"sources", public_outputs_json(sources)},
        },
        pretty);
}

std::string saved_project_list_to_json(
    const std::vector<SavedProjectSummary>& projects,
    bool pretty) {
    Json entries = Json::array();
    for (const auto& project : projects) {
        entries.push_back({
            {"projectId", project.project_id},
            {"name", project.name},
            {"description", project.description},
            {"savedRevision", project.saved_revision},
            {"savedAtUnixMs", project.saved_at_unix_ms},
            {"status", saved_status_string(project.status)},
            {
                "issue",
                project.issue.empty()
                    ? Json(nullptr)
                    : Json(project.issue),
            },
        });
    }
    return dump(
        {
            {
                "schemaVersion",
                kProjectAuthorityJsonSchemaVersion,
            },
            {"projects", std::move(entries)},
        },
        pretty);
}

std::string project_mutation_result_to_json(
    const ProjectMutationResult& result,
    bool pretty) {
    Json created = Json::array();
    for (const auto& entity : result.created_entities) {
        created.push_back({
            {"clientRef", entity.client_ref},
            {"id", entity.id},
        });
    }
    return dump(
        {
            {
                "schemaVersion",
                kProjectAuthorityJsonSchemaVersion,
            },
            {"changed", result.changed},
            {"validatedOnly", result.validated_only},
            {"createdEntities", std::move(created)},
            {"active", snapshot_json(result.active)},
        },
        pretty);
}

ProjectMutationBatch project_mutation_batch_from_json(
    std::string_view encoded) {
    const auto root = parse_body(
        encoded,
        "/",
        kMaximumMutationBodyBytes);
    require_fields(
        root,
        "/",
        {"schemaVersion", "validateOnly", "operations"});
    const auto schema_version = unsigned_field(
        root,
        "/",
        "schemaVersion",
        1,
        std::numeric_limits<std::uint32_t>::max());
    if (schema_version != kProjectAuthorityJsonSchemaVersion) {
        fail(
            "/schemaVersion",
            "only schemaVersion 1 is supported");
    }

    ProjectMutationBatch result;
    result.schema_version =
        static_cast<std::uint32_t>(schema_version);
    result.validate_only =
        bool_field(root, "/", "validateOnly");
    const auto& operations = bounded_array(
        root,
        "/",
        "operations",
        kMaximumOperations);
    result.operations.reserve(operations.size());
    for (std::size_t index = 0;
         index < operations.size();
         ++index) {
        result.operations.push_back(
            parse_operation(
                operations[index],
                index_path("/operations", index)));
    }
    return result;
}

std::string project_mutation_batch_to_json(
    const ProjectMutationBatch& batch,
    bool pretty) {
    if (batch.schema_version !=
        kProjectAuthorityJsonSchemaVersion) {
        fail(
            "/schemaVersion",
            "only schemaVersion 1 is supported");
    }
    if (batch.operations.size() > kMaximumOperations) {
        fail("/operations", "contains too many operations");
    }
    Json operations = Json::array();
    for (const auto& operation : batch.operations) {
        operations.push_back(operation_json(operation));
    }
    auto encoded = dump(
        {
            {"schemaVersion", batch.schema_version},
            {"validateOnly", batch.validate_only},
            {"operations", std::move(operations)},
        },
        pretty);
    static_cast<void>(
        project_mutation_batch_from_json(encoded));
    return encoded;
}

NewProjectRequest new_project_request_from_json(
    std::string_view encoded) {
    const auto root = parse_body(
        encoded,
        "/",
        kMaximumCommandBodyBytes);
    require_fields(
        root,
        "/",
        {
            "schemaVersion",
            "name",
            "description",
            "discardDirty",
        });
    if (unsigned_field(
            root,
            "/",
            "schemaVersion",
            1,
            std::numeric_limits<std::uint32_t>::max()) !=
        kProjectAuthorityJsonSchemaVersion) {
        fail(
            "/schemaVersion",
            "only schemaVersion 1 is supported");
    }
    const auto& description =
        field(root, "/", "description");
    if (!description.is_string()) {
        fail("/description", "must be a string");
    }
    if (java_utf16_code_unit_length(
            description.get_ref<const std::string&>()) > 4096) {
        fail(
            "/description",
            "cannot exceed 4096 UTF-16 code units");
    }
    return {
        parse_project_name(field(root, "/", "name"), "/name"),
        description.get<std::string>(),
        bool_field(root, "/", "discardDirty"),
    };
}

OpenProjectRequest open_project_request_from_json(
    std::string_view encoded) {
    const auto root = parse_body(
        encoded,
        "/",
        kMaximumCommandBodyBytes);
    require_fields(
        root,
        "/",
        {"schemaVersion", "projectId", "discardDirty"});
    if (unsigned_field(
            root,
            "/",
            "schemaVersion",
            1,
            std::numeric_limits<std::uint32_t>::max()) !=
        kProjectAuthorityJsonSchemaVersion) {
        fail(
            "/schemaVersion",
            "only schemaVersion 1 is supported");
    }
    auto project_id = string_field(root, "/", "projectId");
    require_uuid_value(project_id, "/projectId");
    return {
        std::move(project_id),
        bool_field(root, "/", "discardDirty"),
    };
}

SaveAsProjectRequest save_as_project_request_from_json(
    std::string_view encoded) {
    const auto root = parse_body(
        encoded,
        "/",
        kMaximumCommandBodyBytes);
    require_fields(root, "/", {"schemaVersion", "name"});
    if (unsigned_field(
            root,
            "/",
            "schemaVersion",
            1,
            std::numeric_limits<std::uint32_t>::max()) !=
        kProjectAuthorityJsonSchemaVersion) {
        fail(
            "/schemaVersion",
            "only schemaVersion 1 is supported");
    }
    return {
        parse_project_name(field(root, "/", "name"), "/name"),
    };
}

} // namespace pamguard::project
