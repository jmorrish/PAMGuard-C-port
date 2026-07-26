#include "pamguard/project/ProjectJson.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <json.hpp>

#include "CanonicalJson.h"
#include "Sha256.h"

namespace pamguard::project {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumControlledUnits = 1024;
constexpr std::size_t kMaximumBindingsPerUnit = 64;
constexpr std::size_t kMaximumSourcesPerBinding = 64;
constexpr std::size_t kMaximumGlobalComponents = 64;
constexpr std::size_t kMaximumDisplayTabs = 128;
constexpr std::size_t kMaximumDisplaysPerTab = 128;
constexpr std::size_t kMaximumDisplays = 1024;
constexpr std::size_t kMaximumLayoutItemsPerTab = 128;
constexpr std::size_t kMaximumDataModelNodes = 1024;
constexpr std::size_t kMaximumProjectNameUnits = 128;
constexpr std::size_t kMaximumDescriptionUnits = 4096;
constexpr std::size_t kMaximumDisplayNameUnits = 128;
constexpr double kMaximumCoordinateMagnitude = 1'000'000.0;
constexpr double kMinimumZoom = 0.1;
constexpr double kMaximumZoom = 8.0;

[[noreturn]] void fail(
    std::string_view path,
    const std::string& message) {
    throw ProjectJsonError(std::string(path) + ": " + message);
}

std::string child_path(
    std::string_view path,
    std::string_view field) {
    if (path == "/") {
        return "/" + std::string(field);
    }
    return std::string(path) + "/" + std::string(field);
}

std::string index_path(
    std::string_view path,
    std::size_t index) {
    return std::string(path) + "/" + std::to_string(index);
}

void require_object_fields(
    const Json& value,
    std::string_view path,
    std::initializer_list<std::string_view> fields) {
    if (!value.is_object()) {
        fail(path, "must be an object");
    }
    std::set<std::string_view> allowed(fields);
    for (auto iterator = value.begin();
         iterator != value.end();
         ++iterator) {
        if (!allowed.contains(iterator.key())) {
            fail(
                child_path(path, iterator.key()),
                "unknown field");
        }
    }
    for (const auto field : fields) {
        if (!value.contains(std::string(field))) {
            fail(
                child_path(path, field),
                "required field is missing");
        }
    }
}

const Json& field(
    const Json& value,
    std::string_view path,
    std::string_view name) {
    const auto iterator = value.find(std::string(name));
    if (iterator == value.end()) {
        fail(child_path(path, name), "required field is missing");
    }
    return *iterator;
}

std::string string_field(
    const Json& value,
    std::string_view path,
    std::string_view name) {
    const auto& item = field(value, path, name);
    if (!item.is_string()) {
        fail(child_path(path, name), "must be a string");
    }
    return item.get<std::string>();
}

std::uint32_t unsigned_field(
    const Json& value,
    std::string_view path,
    std::string_view name,
    std::uint32_t minimum = 0,
    std::uint32_t maximum =
        std::numeric_limits<std::uint32_t>::max()) {
    const auto item_path = child_path(path, name);
    const auto& item = field(value, path, name);
    std::uint64_t number = 0;
    if (item.is_number_unsigned()) {
        number = item.get<std::uint64_t>();
    }
    else if (item.is_number_integer()) {
        const auto signed_number = item.get<std::int64_t>();
        if (signed_number < 0) {
            fail(item_path, "must be non-negative");
        }
        number = static_cast<std::uint64_t>(signed_number);
    }
    else {
        fail(item_path, "must be an integer");
    }
    if (number < minimum || number > maximum) {
        fail(
            item_path,
            "must be in " + std::to_string(minimum) + ".." +
                std::to_string(maximum));
    }
    return static_cast<std::uint32_t>(number);
}

double finite_number_field(
    const Json& value,
    std::string_view path,
    std::string_view name) {
    const auto item_path = child_path(path, name);
    const auto& item = field(value, path, name);
    if (!item.is_number()) {
        fail(item_path, "must be a number");
    }
    const auto number = item.get<double>();
    if (!std::isfinite(number)) {
        fail(item_path, "must be finite");
    }
    return number == 0.0 ? 0.0 : number;
}

const Json& bounded_array_field(
    const Json& value,
    std::string_view path,
    std::string_view name,
    std::size_t maximum) {
    const auto item_path = child_path(path, name);
    const auto& item = field(value, path, name);
    if (!item.is_array()) {
        fail(item_path, "must be an array");
    }
    if (item.size() > maximum) {
        fail(
            item_path,
            "cannot contain more than " +
                std::to_string(maximum) + " entries");
    }
    return item;
}

std::string canonical_settings(
    const Json& value,
    std::string_view path) {
    if (!value.is_object()) {
        fail(path, "must be an object");
    }
    auto normalized = value;
    detail::normalize_json_numbers(
        normalized,
        path,
        detail::kMaximumEmbeddedSettingsDepth);
    auto canonical =
        detail::canonical_json_dump(std::move(normalized));
    if (canonical.size() >
        detail::kMaximumEmbeddedSettingsBytes) {
        fail(
            path,
            "canonical settings exceed " +
                std::to_string(
                    detail::kMaximumEmbeddedSettingsBytes) +
                " bytes");
    }
    return canonical;
}

std::string normalize_embedded_settings(
    std::string_view json,
    std::string_view path) {
    auto value = detail::parse_strict_json(
        json,
        path,
        detail::kMaximumEmbeddedSettingsBytes,
        detail::kMaximumEmbeddedSettingsDepth);
    return canonical_settings(value, path);
}

ProjectMode parse_project_mode(
    const Json& value,
    std::string_view path) {
    const auto raw = string_field(value, path, "mode");
    if (raw == "normal") {
        return ProjectMode::Normal;
    }
    if (raw == "mixed") {
        return ProjectMode::Mixed;
    }
    if (raw == "viewer") {
        return ProjectMode::Viewer;
    }
    fail(child_path(path, "mode"), "unknown project mode '" + raw + "'");
}

std::string project_mode_json(ProjectMode mode) {
    switch (mode) {
    case ProjectMode::Normal:
        return "normal";
    case ProjectMode::Mixed:
        return "mixed";
    case ProjectMode::Viewer:
        return "viewer";
    }
    fail("/mode", "project mode is invalid");
}

DisplayLayoutMode parse_display_layout_mode(
    const Json& value,
    std::string_view path) {
    const auto raw = string_field(value, path, "mode");
    if (raw == "grid") {
        return DisplayLayoutMode::Grid;
    }
    if (raw == "tabs") {
        return DisplayLayoutMode::Tabs;
    }
    fail(child_path(path, "mode"), "unknown display layout mode '" + raw + "'");
}

std::string display_layout_mode_json(DisplayLayoutMode mode) {
    switch (mode) {
    case DisplayLayoutMode::Grid:
        return "grid";
    case DisplayLayoutMode::Tabs:
        return "tabs";
    }
    fail("/displayTabs/layout/mode", "display layout mode is invalid");
}

SourceReference parse_source_reference(
    const Json& value,
    std::string_view path) {
    require_object_fields(value, path, {"unitId", "outputRole"});
    return {
        string_field(value, path, "unitId"),
        string_field(value, path, "outputRole"),
    };
}

DisplayOwner parse_display_owner(
    const Json& value,
    std::string_view path) {
    require_object_fields(value, path, {"unitId", "role"});
    return {
        string_field(value, path, "unitId"),
        string_field(value, path, "role"),
    };
}

ProjectDocument parse_document(const Json& root) {
    require_object_fields(
        root,
        "/",
        {
            "schemaVersion",
            "projectId",
            "metadata",
            "mode",
            "descriptorSet",
            "controlledUnits",
            "globalSettings",
            "displayTabs",
            "dataModelLayout",
        });

    ProjectDocument document;
    document.schema_version =
        unsigned_field(root, "/", "schemaVersion", 1);
    if (document.schema_version != kProjectSchemaVersion) {
        fail(
            "/schemaVersion",
            "unsupported project schema version " +
                std::to_string(document.schema_version));
    }
    document.project_id = string_field(root, "/", "projectId");
    document.mode = parse_project_mode(root, "/");

    const auto& metadata = field(root, "/", "metadata");
    require_object_fields(
        metadata,
        "/metadata",
        {"name", "description"});
    document.metadata = {
        string_field(metadata, "/metadata", "name"),
        string_field(metadata, "/metadata", "description"),
    };

    const auto& descriptor_set =
        field(root, "/", "descriptorSet");
    require_object_fields(
        descriptor_set,
        "/descriptorSet",
        {"id", "version"});
    document.descriptor_set = {
        string_field(descriptor_set, "/descriptorSet", "id"),
        unsigned_field(
            descriptor_set,
            "/descriptorSet",
            "version",
            1),
    };

    const auto& units = bounded_array_field(
        root,
        "/",
        "controlledUnits",
        kMaximumControlledUnits);
    document.controlled_units.reserve(units.size());
    for (std::size_t index = 0; index < units.size(); ++index) {
        const auto path = index_path("/controlledUnits", index);
        const auto& value = units[index];
        require_object_fields(
            value,
            path,
            {
                "id",
                "typeId",
                "descriptorVersion",
                "recipe",
                "name",
                "settingsVersion",
                "settings",
                "bindings",
            });
        ControlledUnitInstance unit;
        unit.id = string_field(value, path, "id");
        unit.type_id = string_field(value, path, "typeId");
        unit.descriptor_version =
            unsigned_field(
                value,
                path,
                "descriptorVersion",
                1);
        unit.name = string_field(value, path, "name");
        unit.settings_version =
            unsigned_field(value, path, "settingsVersion", 1);
        unit.settings_json = canonical_settings(
            field(value, path, "settings"),
            child_path(path, "settings"));

        const auto recipe_path = child_path(path, "recipe");
        const auto& recipe = field(value, path, "recipe");
        require_object_fields(
            recipe,
            recipe_path,
            {"id", "version"});
        unit.recipe = {
            string_field(recipe, recipe_path, "id"),
            unsigned_field(recipe, recipe_path, "version", 1),
        };

        const auto& bindings = bounded_array_field(
            value,
            path,
            "bindings",
            kMaximumBindingsPerUnit);
        unit.bindings.reserve(bindings.size());
        for (std::size_t binding_index = 0;
             binding_index < bindings.size();
             ++binding_index) {
            const auto binding_path = index_path(
                child_path(path, "bindings"),
                binding_index);
            const auto& binding_value = bindings[binding_index];
            require_object_fields(
                binding_value,
                binding_path,
                {"inputRole", "sources"});
            InputBinding binding;
            binding.input_role = string_field(
                binding_value,
                binding_path,
                "inputRole");
            const auto& sources = bounded_array_field(
                binding_value,
                binding_path,
                "sources",
                kMaximumSourcesPerBinding);
            binding.sources.reserve(sources.size());
            for (std::size_t source_index = 0;
                 source_index < sources.size();
                 ++source_index) {
                const auto source_path = index_path(
                    child_path(binding_path, "sources"),
                    source_index);
                binding.sources.push_back(
                    parse_source_reference(
                        sources[source_index],
                        source_path));
            }
            unit.bindings.push_back(std::move(binding));
        }
        document.controlled_units.push_back(std::move(unit));
    }

    const auto& global = field(root, "/", "globalSettings");
    require_object_fields(
        global,
        "/globalSettings",
        {"schemaVersion", "components"});
    document.global_settings.schema_version =
        unsigned_field(
            global,
            "/globalSettings",
            "schemaVersion",
            1);
    const auto& components = bounded_array_field(
        global,
        "/globalSettings",
        "components",
        kMaximumGlobalComponents);
    document.global_settings.components.reserve(components.size());
    for (std::size_t index = 0;
         index < components.size();
         ++index) {
        const auto path =
            index_path("/globalSettings/components", index);
        const auto& value = components[index];
        require_object_fields(
            value,
            path,
            {"typeId", "settingsVersion", "settings"});
        document.global_settings.components.push_back({
            string_field(value, path, "typeId"),
            unsigned_field(value, path, "settingsVersion", 1),
            canonical_settings(
                field(value, path, "settings"),
                child_path(path, "settings")),
        });
    }

    const auto& tabs = bounded_array_field(
        root,
        "/",
        "displayTabs",
        kMaximumDisplayTabs);
    document.display_tabs.reserve(tabs.size());
    for (std::size_t tab_index = 0;
         tab_index < tabs.size();
         ++tab_index) {
        const auto tab_path =
            index_path("/displayTabs", tab_index);
        const auto& tab_value = tabs[tab_index];
        require_object_fields(
            tab_value,
            tab_path,
            {"id", "name", "owner", "displays", "layout"});
        DisplayTab tab;
        tab.id = string_field(tab_value, tab_path, "id");
        tab.name = string_field(tab_value, tab_path, "name");
        tab.owner = parse_display_owner(
            field(tab_value, tab_path, "owner"),
            child_path(tab_path, "owner"));

        const auto& displays = bounded_array_field(
            tab_value,
            tab_path,
            "displays",
            kMaximumDisplaysPerTab);
        tab.displays.reserve(displays.size());
        for (std::size_t display_index = 0;
             display_index < displays.size();
             ++display_index) {
            const auto display_path = index_path(
                child_path(tab_path, "displays"),
                display_index);
            const auto& display_value = displays[display_index];
            require_object_fields(
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
            display.provider_type_id = string_field(
                display_value,
                display_path,
                "providerTypeId");
            display.provider_version = unsigned_field(
                display_value,
                display_path,
                "providerVersion",
                1);
            display.owner = parse_display_owner(
                field(display_value, display_path, "owner"),
                child_path(display_path, "owner"));
            const auto& source =
                field(display_value, display_path, "source");
            if (!source.is_null()) {
                display.source = parse_source_reference(
                    source,
                    child_path(display_path, "source"));
            }
            display.settings_version = unsigned_field(
                display_value,
                display_path,
                "settingsVersion",
                1);
            display.settings_json = canonical_settings(
                field(display_value, display_path, "settings"),
                child_path(display_path, "settings"));
            tab.displays.push_back(std::move(display));
        }

        const auto layout_path = child_path(tab_path, "layout");
        const auto& layout = field(tab_value, tab_path, "layout");
        require_object_fields(
            layout,
            layout_path,
            {"mode", "columns", "selectedDisplayId", "items"});
        tab.layout.mode =
            parse_display_layout_mode(layout, layout_path);
        tab.layout.columns = unsigned_field(
            layout,
            layout_path,
            "columns",
            1,
            24);
        const auto& selected =
            field(layout, layout_path, "selectedDisplayId");
        if (!selected.is_null()) {
            if (!selected.is_string()) {
                fail(
                    child_path(layout_path, "selectedDisplayId"),
                    "must be a string or null");
            }
            tab.layout.selected_display_id =
                selected.get<std::string>();
        }
        const auto& items = bounded_array_field(
            layout,
            layout_path,
            "items",
            kMaximumLayoutItemsPerTab);
        tab.layout.items.reserve(items.size());
        for (std::size_t item_index = 0;
             item_index < items.size();
             ++item_index) {
            const auto item_path = index_path(
                child_path(layout_path, "items"),
                item_index);
            const auto& item = items[item_index];
            require_object_fields(
                item,
                item_path,
                {"displayId", "column", "row", "width", "height"});
            tab.layout.items.push_back({
                string_field(item, item_path, "displayId"),
                unsigned_field(item, item_path, "column", 0, 23),
                unsigned_field(item, item_path, "row", 0, 10000),
                unsigned_field(item, item_path, "width", 1, 24),
                unsigned_field(item, item_path, "height", 1, 1000),
            });
        }
        document.display_tabs.push_back(std::move(tab));
    }

    const auto& data_model =
        field(root, "/", "dataModelLayout");
    require_object_fields(
        data_model,
        "/dataModelLayout",
        {"nodes", "viewport"});
    const auto& nodes = bounded_array_field(
        data_model,
        "/dataModelLayout",
        "nodes",
        kMaximumDataModelNodes);
    document.data_model_layout.nodes.reserve(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto path =
            index_path("/dataModelLayout/nodes", index);
        const auto& value = nodes[index];
        require_object_fields(
            value,
            path,
            {"unitId", "x", "y"});
        document.data_model_layout.nodes.push_back({
            string_field(value, path, "unitId"),
            finite_number_field(value, path, "x"),
            finite_number_field(value, path, "y"),
        });
    }
    const auto& viewport =
        field(data_model, "/dataModelLayout", "viewport");
    require_object_fields(
        viewport,
        "/dataModelLayout/viewport",
        {"x", "y", "zoom"});
    document.data_model_layout.viewport = {
        finite_number_field(
            viewport,
            "/dataModelLayout/viewport",
            "x"),
        finite_number_field(
            viewport,
            "/dataModelLayout/viewport",
            "y"),
        finite_number_field(
            viewport,
            "/dataModelLayout/viewport",
            "zoom"),
    };
    return document;
}

void require_entity_id(
    std::string_view value,
    std::string_view path) {
    if (!is_entity_id(value)) {
        fail(path, "must be a valid entity identifier");
    }
}

void require_role_id(
    std::string_view value,
    std::string_view path) {
    if (!is_role_id(value)) {
        fail(path, "must be a valid stable role identifier");
    }
}

void require_uuid(
    std::string_view value,
    std::string_view path) {
    if (!is_uuid_v4(value)) {
        fail(path, "must be a lowercase UUIDv4");
    }
}

void require_utf16_limit(
    std::string_view value,
    std::size_t maximum,
    std::string_view path) {
    if (java_utf16_code_unit_length(value) > maximum) {
        fail(
            path,
            "cannot exceed " + std::to_string(maximum) +
                " UTF-16 code units");
    }
}

void validate_source_reference(
    const SourceReference& source,
    std::string_view path,
    const std::unordered_set<std::string>& unit_ids) {
    require_uuid(
        source.unit_id,
        child_path(path, "unitId"));
    require_role_id(
        source.output_role,
        child_path(path, "outputRole"));
    if (!unit_ids.contains(source.unit_id)) {
        fail(
            child_path(path, "unitId"),
            "references an unknown controlled unit");
    }
}

void validate_display_owner(
    const DisplayOwner& owner,
    std::string_view path,
    const std::unordered_set<std::string>& unit_ids) {
    require_uuid(owner.unit_id, child_path(path, "unitId"));
    require_role_id(owner.role, child_path(path, "role"));
    if (!unit_ids.contains(owner.unit_id)) {
        fail(
            child_path(path, "unitId"),
            "references an unknown controlled unit");
    }
}

ProjectDocument normalize_and_validate(ProjectDocument document) {
    std::size_t aggregate_settings_bytes = 0;
    const auto account_settings =
        [&](const std::string& settings, std::string_view path) {
            if (settings.size() >
                detail::kMaximumAggregateEmbeddedSettingsBytes -
                    aggregate_settings_bytes) {
                fail(
                    path,
                    "project settings exceed the aggregate " +
                        std::to_string(
                            detail::
                                kMaximumAggregateEmbeddedSettingsBytes) +
                        "-byte budget");
            }
            aggregate_settings_bytes += settings.size();
        };
    if (document.schema_version != kProjectSchemaVersion) {
        fail(
            "/schemaVersion",
            "unsupported project schema version " +
                std::to_string(document.schema_version));
    }
    require_uuid(document.project_id, "/projectId");

    document.metadata.name =
        trim_java_string(document.metadata.name);
    if (document.metadata.name.empty()) {
        fail("/metadata/name", "must not be empty");
    }
    require_utf16_limit(
        document.metadata.name,
        kMaximumProjectNameUnits,
        "/metadata/name");
    require_utf16_limit(
        document.metadata.description,
        kMaximumDescriptionUnits,
        "/metadata/description");

    require_entity_id(
        document.descriptor_set.id,
        "/descriptorSet/id");
    if (document.descriptor_set.version == 0) {
        fail("/descriptorSet/version", "must be at least 1");
    }
    if (document.controlled_units.size() >
        kMaximumControlledUnits) {
        fail(
            "/controlledUnits",
            "cannot contain more than " +
                std::to_string(kMaximumControlledUnits) +
                " entries");
    }

    std::unordered_set<std::string> unit_ids;
    for (std::size_t unit_index = 0;
         unit_index < document.controlled_units.size();
         ++unit_index) {
        auto& unit = document.controlled_units[unit_index];
        const auto path =
            index_path("/controlledUnits", unit_index);
        require_uuid(unit.id, child_path(path, "id"));
        if (!unit_ids.insert(unit.id).second) {
            fail(child_path(path, "id"), "duplicate controlled-unit ID");
        }
        require_entity_id(
            unit.type_id,
            child_path(path, "typeId"));
        if (unit.descriptor_version == 0) {
            fail(
                child_path(path, "descriptorVersion"),
                "must be at least 1");
        }
        require_entity_id(
            unit.recipe.id,
            child_path(child_path(path, "recipe"), "id"));
        if (unit.recipe.version == 0) {
            fail(
                child_path(child_path(path, "recipe"), "version"),
                "must be at least 1");
        }
        unit.name = trim_java_string(unit.name);
        if (!is_valid_java_item_name(unit.name)) {
            fail(
                child_path(path, "name"),
                "must contain 1 to " +
                    std::to_string(
                        kMaximumJavaItemNameUtf16Units) +
                    " UTF-16 code units after Java trimming");
        }
        if (unit.settings_version == 0) {
            fail(
                child_path(path, "settingsVersion"),
                "must be at least 1");
        }
        unit.settings_json = normalize_embedded_settings(
            unit.settings_json,
            child_path(path, "settings"));
        account_settings(
            unit.settings_json,
            child_path(path, "settings"));
        if (unit.bindings.size() > kMaximumBindingsPerUnit) {
            fail(
                child_path(path, "bindings"),
                "contains too many bindings");
        }
        std::unordered_set<std::string> input_roles;
        for (std::size_t binding_index = 0;
             binding_index < unit.bindings.size();
             ++binding_index) {
            auto& binding = unit.bindings[binding_index];
            const auto binding_path = index_path(
                child_path(path, "bindings"),
                binding_index);
            require_role_id(
                binding.input_role,
                child_path(binding_path, "inputRole"));
            if (!input_roles.insert(binding.input_role).second) {
                fail(
                    child_path(binding_path, "inputRole"),
                    "duplicate input role");
            }
            if (binding.sources.size() >
                kMaximumSourcesPerBinding) {
                fail(
                    child_path(binding_path, "sources"),
                    "contains too many sources");
            }
            std::set<std::pair<std::string, std::string>>
                source_keys;
            for (std::size_t source_index = 0;
                 source_index < binding.sources.size();
                 ++source_index) {
                const auto& source =
                    binding.sources[source_index];
                const auto source_path = index_path(
                    child_path(binding_path, "sources"),
                    source_index);
                require_uuid(
                    source.unit_id,
                    child_path(source_path, "unitId"));
                require_role_id(
                    source.output_role,
                    child_path(source_path, "outputRole"));
                if (!source_keys.emplace(
                        source.unit_id,
                        source.output_role)
                         .second) {
                    fail(source_path, "duplicate source reference");
                }
            }
        }
        std::sort(
            unit.bindings.begin(),
            unit.bindings.end(),
            [](const auto& left, const auto& right) {
                return left.input_role < right.input_role;
            });
    }

    for (std::size_t unit_index = 0;
         unit_index < document.controlled_units.size();
         ++unit_index) {
        const auto& unit = document.controlled_units[unit_index];
        const auto bindings_path = child_path(
            index_path("/controlledUnits", unit_index),
            "bindings");
        for (std::size_t binding_index = 0;
             binding_index < unit.bindings.size();
             ++binding_index) {
            const auto sources_path = child_path(
                index_path(bindings_path, binding_index),
                "sources");
            const auto& sources =
                unit.bindings[binding_index].sources;
            for (std::size_t source_index = 0;
                 source_index < sources.size();
                 ++source_index) {
                validate_source_reference(
                    sources[source_index],
                    index_path(sources_path, source_index),
                    unit_ids);
            }
        }
    }

    if (document.global_settings.schema_version !=
        kGlobalSettingsSchemaVersion) {
        fail(
            "/globalSettings/schemaVersion",
            "unsupported global-settings schema version");
    }
    if (document.global_settings.components.size() >
        kMaximumGlobalComponents) {
        fail(
            "/globalSettings/components",
            "contains too many components");
    }
    std::unordered_set<std::string> global_type_ids;
    for (std::size_t index = 0;
         index < document.global_settings.components.size();
         ++index) {
        auto& component =
            document.global_settings.components[index];
        const auto path =
            index_path("/globalSettings/components", index);
        require_entity_id(
            component.type_id,
            child_path(path, "typeId"));
        if (!global_type_ids.insert(component.type_id).second) {
            fail(
                child_path(path, "typeId"),
                "duplicate global-settings component");
        }
        if (component.settings_version == 0) {
            fail(
                child_path(path, "settingsVersion"),
                "must be at least 1");
        }
        component.settings_json = normalize_embedded_settings(
            component.settings_json,
            child_path(path, "settings"));
        account_settings(
            component.settings_json,
            child_path(path, "settings"));
    }
    std::sort(
        document.global_settings.components.begin(),
        document.global_settings.components.end(),
        [](const auto& left, const auto& right) {
            return left.type_id < right.type_id;
        });

    if (document.display_tabs.size() > kMaximumDisplayTabs) {
        fail("/displayTabs", "contains too many display tabs");
    }
    std::unordered_set<std::string> tab_ids;
    std::unordered_set<std::string> all_display_ids;
    std::size_t display_count = 0;
    for (std::size_t tab_index = 0;
         tab_index < document.display_tabs.size();
         ++tab_index) {
        auto& tab = document.display_tabs[tab_index];
        const auto tab_path =
            index_path("/displayTabs", tab_index);
        require_entity_id(tab.id, child_path(tab_path, "id"));
        if (!tab_ids.insert(tab.id).second) {
            fail(child_path(tab_path, "id"), "duplicate display-tab ID");
        }
        tab.name = trim_java_string(tab.name);
        if (tab.name.empty()) {
            fail(child_path(tab_path, "name"), "must not be empty");
        }
        require_utf16_limit(
            tab.name,
            kMaximumDisplayNameUnits,
            child_path(tab_path, "name"));
        validate_display_owner(
            tab.owner,
            child_path(tab_path, "owner"),
            unit_ids);
        if (tab.displays.size() > kMaximumDisplaysPerTab) {
            fail(
                child_path(tab_path, "displays"),
                "contains too many displays");
        }
        display_count += tab.displays.size();
        if (display_count > kMaximumDisplays) {
            fail(
                "/displayTabs",
                "project contains too many displays");
        }

        std::unordered_set<std::string> tab_display_ids;
        for (std::size_t display_index = 0;
             display_index < tab.displays.size();
             ++display_index) {
            auto& display = tab.displays[display_index];
            const auto display_path = index_path(
                child_path(tab_path, "displays"),
                display_index);
            require_entity_id(
                display.id,
                child_path(display_path, "id"));
            if (!all_display_ids.insert(display.id).second) {
                fail(
                    child_path(display_path, "id"),
                    "duplicate project display ID");
            }
            tab_display_ids.insert(display.id);
            require_entity_id(
                display.provider_type_id,
                child_path(display_path, "providerTypeId"));
            if (display.provider_version == 0) {
                fail(
                    child_path(display_path, "providerVersion"),
                    "must be at least 1");
            }
            validate_display_owner(
                display.owner,
                child_path(display_path, "owner"),
                unit_ids);
            if (display.source) {
                validate_source_reference(
                    *display.source,
                    child_path(display_path, "source"),
                    unit_ids);
            }
            if (display.settings_version == 0) {
                fail(
                    child_path(display_path, "settingsVersion"),
                    "must be at least 1");
            }
            display.settings_json = normalize_embedded_settings(
                display.settings_json,
                child_path(display_path, "settings"));
            account_settings(
                display.settings_json,
                child_path(display_path, "settings"));
        }

        const auto layout_path = child_path(tab_path, "layout");
        if (tab.layout.columns == 0 ||
            tab.layout.columns > 24) {
            fail(
                child_path(layout_path, "columns"),
                "must be in 1..24");
        }
        if (tab.layout.selected_display_id) {
            require_entity_id(
                *tab.layout.selected_display_id,
                child_path(layout_path, "selectedDisplayId"));
            if (!tab_display_ids.contains(
                    *tab.layout.selected_display_id)) {
                fail(
                    child_path(layout_path, "selectedDisplayId"),
                    "references a display outside this tab");
            }
        }
        if (tab.layout.items.size() >
            kMaximumLayoutItemsPerTab) {
            fail(
                child_path(layout_path, "items"),
                "contains too many layout items");
        }
        std::unordered_set<std::string> item_display_ids;
        for (std::size_t item_index = 0;
             item_index < tab.layout.items.size();
             ++item_index) {
            const auto& item = tab.layout.items[item_index];
            const auto item_path = index_path(
                child_path(layout_path, "items"),
                item_index);
            require_entity_id(
                item.display_id,
                child_path(item_path, "displayId"));
            if (!tab_display_ids.contains(item.display_id)) {
                fail(
                    child_path(item_path, "displayId"),
                    "references a display outside this tab");
            }
            if (!item_display_ids.insert(item.display_id).second) {
                fail(
                    child_path(item_path, "displayId"),
                    "duplicate display layout item");
            }
            if (item.column > 23 ||
                item.row > 10000 ||
                item.width == 0 ||
                item.width > 24 ||
                item.height == 0 ||
                item.height > 1000 ||
                item.column + item.width > tab.layout.columns) {
                fail(item_path, "grid bounds are invalid");
            }
        }
        if (item_display_ids.size() != tab_display_ids.size()) {
            fail(
                child_path(layout_path, "items"),
                "must contain exactly one item for every display");
        }
        std::unordered_map<std::string, std::size_t>
            display_order;
        display_order.reserve(tab.displays.size());
        for (std::size_t index = 0;
             index < tab.displays.size();
             ++index) {
            display_order.emplace(
                tab.displays[index].id,
                index);
        }
        std::sort(
            tab.layout.items.begin(),
            tab.layout.items.end(),
            [&](const auto& left, const auto& right) {
                return display_order.at(left.display_id) <
                    display_order.at(right.display_id);
            });
    }

    if (document.data_model_layout.nodes.size() >
        kMaximumDataModelNodes) {
        fail("/dataModelLayout/nodes", "contains too many nodes");
    }
    std::unordered_set<std::string> positioned_units;
    for (std::size_t index = 0;
         index < document.data_model_layout.nodes.size();
         ++index) {
        auto& node = document.data_model_layout.nodes[index];
        const auto path =
            index_path("/dataModelLayout/nodes", index);
        require_uuid(node.unit_id, child_path(path, "unitId"));
        if (!unit_ids.contains(node.unit_id)) {
            fail(
                child_path(path, "unitId"),
                "references an unknown controlled unit");
        }
        if (!positioned_units.insert(node.unit_id).second) {
            fail(
                child_path(path, "unitId"),
                "duplicate Data Model node position");
        }
        if (!std::isfinite(node.x) ||
            !std::isfinite(node.y) ||
            std::abs(node.x) > kMaximumCoordinateMagnitude ||
            std::abs(node.y) > kMaximumCoordinateMagnitude) {
            fail(path, "Data Model coordinates are invalid");
        }
        if (node.x == 0.0) {
            node.x = 0.0;
        }
        if (node.y == 0.0) {
            node.y = 0.0;
        }
    }
    auto& viewport = document.data_model_layout.viewport;
    if (!std::isfinite(viewport.x) ||
        !std::isfinite(viewport.y) ||
        !std::isfinite(viewport.zoom) ||
        std::abs(viewport.x) > kMaximumCoordinateMagnitude ||
        std::abs(viewport.y) > kMaximumCoordinateMagnitude ||
        viewport.zoom < kMinimumZoom ||
        viewport.zoom > kMaximumZoom) {
        fail(
            "/dataModelLayout/viewport",
            "viewport values are invalid");
    }
    if (viewport.x == 0.0) {
        viewport.x = 0.0;
    }
    if (viewport.y == 0.0) {
        viewport.y = 0.0;
    }
    std::unordered_map<std::string, std::size_t>
        controlled_unit_order;
    controlled_unit_order.reserve(
        document.controlled_units.size());
    for (std::size_t index = 0;
         index < document.controlled_units.size();
         ++index) {
        controlled_unit_order.emplace(
            document.controlled_units[index].id,
            index);
    }
    std::sort(
        document.data_model_layout.nodes.begin(),
        document.data_model_layout.nodes.end(),
        [&](const auto& left, const auto& right) {
            return controlled_unit_order.at(left.unit_id) <
                controlled_unit_order.at(right.unit_id);
        });
    return document;
}

Json embedded_object(
    std::string_view json,
    std::string_view path) {
    auto value = detail::parse_strict_json(
        json,
        path,
        detail::kMaximumEmbeddedSettingsBytes,
        detail::kMaximumEmbeddedSettingsDepth);
    if (!value.is_object()) {
        fail(path, "must contain a JSON object");
    }
    return value;
}

Json source_reference_json(const SourceReference& source) {
    return {
        {"unitId", source.unit_id},
        {"outputRole", source.output_role},
    };
}

Json display_owner_json(const DisplayOwner& owner) {
    return {
        {"unitId", owner.unit_id},
        {"role", owner.role},
    };
}

Json document_json(ProjectDocument document) {
    document = normalize_and_validate(std::move(document));
    Json root = {
        {"schemaVersion", document.schema_version},
        {"projectId", document.project_id},
        {"metadata", {
            {"name", document.metadata.name},
            {"description", document.metadata.description},
        }},
        {"mode", project_mode_json(document.mode)},
        {"descriptorSet", {
            {"id", document.descriptor_set.id},
            {"version", document.descriptor_set.version},
        }},
        {"controlledUnits", Json::array()},
        {"globalSettings", {
            {"schemaVersion", document.global_settings.schema_version},
            {"components", Json::array()},
        }},
        {"displayTabs", Json::array()},
        {"dataModelLayout", {
            {"nodes", Json::array()},
            {"viewport", {
                {"x", document.data_model_layout.viewport.x},
                {"y", document.data_model_layout.viewport.y},
                {"zoom", document.data_model_layout.viewport.zoom},
            }},
        }},
    };

    for (std::size_t unit_index = 0;
         unit_index < document.controlled_units.size();
         ++unit_index) {
        const auto& unit = document.controlled_units[unit_index];
        const auto path =
            index_path("/controlledUnits", unit_index);
        Json value = {
            {"id", unit.id},
            {"typeId", unit.type_id},
            {"descriptorVersion", unit.descriptor_version},
            {"recipe", {
                {"id", unit.recipe.id},
                {"version", unit.recipe.version},
            }},
            {"name", unit.name},
            {"settingsVersion", unit.settings_version},
            {"settings", embedded_object(
                unit.settings_json,
                child_path(path, "settings"))},
            {"bindings", Json::array()},
        };
        for (const auto& binding : unit.bindings) {
            Json binding_value = {
                {"inputRole", binding.input_role},
                {"sources", Json::array()},
            };
            for (const auto& source : binding.sources) {
                binding_value["sources"].push_back(
                    source_reference_json(source));
            }
            value["bindings"].push_back(std::move(binding_value));
        }
        root["controlledUnits"].push_back(std::move(value));
    }

    for (std::size_t index = 0;
         index < document.global_settings.components.size();
         ++index) {
        const auto& component =
            document.global_settings.components[index];
        const auto path =
            index_path("/globalSettings/components", index);
        root["globalSettings"]["components"].push_back({
            {"typeId", component.type_id},
            {"settingsVersion", component.settings_version},
            {"settings", embedded_object(
                component.settings_json,
                child_path(path, "settings"))},
        });
    }

    for (std::size_t tab_index = 0;
         tab_index < document.display_tabs.size();
         ++tab_index) {
        const auto& tab = document.display_tabs[tab_index];
        const auto tab_path =
            index_path("/displayTabs", tab_index);
        Json tab_value = {
            {"id", tab.id},
            {"name", tab.name},
            {"owner", display_owner_json(tab.owner)},
            {"displays", Json::array()},
            {"layout", {
                {"mode", display_layout_mode_json(tab.layout.mode)},
                {"columns", tab.layout.columns},
                {"selectedDisplayId",
                 tab.layout.selected_display_id
                     ? Json(*tab.layout.selected_display_id)
                     : Json(nullptr)},
                {"items", Json::array()},
            }},
        };
        for (std::size_t display_index = 0;
             display_index < tab.displays.size();
             ++display_index) {
            const auto& display = tab.displays[display_index];
            const auto display_path = index_path(
                child_path(tab_path, "displays"),
                display_index);
            tab_value["displays"].push_back({
                {"id", display.id},
                {"providerTypeId", display.provider_type_id},
                {"providerVersion", display.provider_version},
                {"owner", display_owner_json(display.owner)},
                {"source",
                 display.source
                     ? source_reference_json(*display.source)
                     : Json(nullptr)},
                {"settingsVersion", display.settings_version},
                {"settings", embedded_object(
                    display.settings_json,
                    child_path(display_path, "settings"))},
            });
        }
        for (const auto& item : tab.layout.items) {
            tab_value["layout"]["items"].push_back({
                {"displayId", item.display_id},
                {"column", item.column},
                {"row", item.row},
                {"width", item.width},
                {"height", item.height},
            });
        }
        root["displayTabs"].push_back(std::move(tab_value));
    }

    for (const auto& node : document.data_model_layout.nodes) {
        root["dataModelLayout"]["nodes"].push_back({
            {"unitId", node.unit_id},
            {"x", node.x},
            {"y", node.y},
        });
    }
    detail::normalize_json_numbers(
        root,
        "Project document",
        detail::kMaximumJsonDepth);
    return root;
}

} // namespace

bool is_uuid_v4(std::string_view value) noexcept {
    if (value.size() != 36) {
        return false;
    }
    const auto hexadecimal = [](char character) {
        return (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f');
    };
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 ||
            index == 18 || index == 23) {
            if (value[index] != '-') {
                return false;
            }
        }
        else if (!hexadecimal(value[index])) {
            return false;
        }
    }
    return value[14] == '4' &&
        (value[19] == '8' || value[19] == '9' ||
         value[19] == 'a' || value[19] == 'b');
}

bool is_entity_id(std::string_view value) noexcept {
    if (value.empty() || value.size() > 128) {
        return false;
    }
    const auto initial = value.front();
    if (!((initial >= 'A' && initial <= 'Z') ||
          (initial >= 'a' && initial <= 'z') ||
          (initial >= '0' && initial <= '9'))) {
        return false;
    }
    return std::all_of(
        value.begin(),
        value.end(),
        [](char character) {
            return (character >= 'A' && character <= 'Z') ||
                (character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') ||
                character == '.' ||
                character == '_' ||
                character == ':' ||
                character == '-';
        });
}

bool is_role_id(std::string_view value) noexcept {
    if (value.empty() || value.size() > 64 ||
        value.front() < 'a' || value.front() > 'z') {
        return false;
    }
    return std::all_of(
        value.begin() + 1,
        value.end(),
        [](char character) {
            return (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9');
        });
}

std::size_t java_utf16_code_unit_length(
    std::string_view utf8) {
    detail::validate_utf8(utf8, "Java string");
    std::size_t units = 0;
    for (std::size_t index = 0; index < utf8.size();) {
        const auto first =
            static_cast<unsigned char>(utf8[index]);
        if (first <= 0x7FU) {
            ++units;
            ++index;
        }
        else if (first <= 0xDFU) {
            ++units;
            index += 2;
        }
        else if (first <= 0xEFU) {
            ++units;
            index += 3;
        }
        else {
            units += 2;
            index += 4;
        }
    }
    return units;
}

std::string trim_java_string(std::string_view utf8) {
    detail::validate_utf8(utf8, "Java string");
    std::size_t begin = 0;
    while (begin < utf8.size() &&
           static_cast<unsigned char>(utf8[begin]) <= 0x20U) {
        ++begin;
    }
    std::size_t end = utf8.size();
    while (end > begin &&
           static_cast<unsigned char>(utf8[end - 1]) <= 0x20U) {
        --end;
    }
    return std::string(utf8.substr(begin, end - begin));
}

bool is_valid_java_item_name(std::string_view utf8) {
    try {
        const auto trimmed = trim_java_string(utf8);
        return !trimmed.empty() &&
            java_utf16_code_unit_length(trimmed) <=
                kMaximumJavaItemNameUtf16Units;
    }
    catch (const std::invalid_argument&) {
        return false;
    }
}

void validate_strict_json(std::string_view json) {
    (void) detail::parse_strict_json(
        json,
        "JSON document",
        detail::kMaximumProjectJsonBytes,
        detail::kMaximumJsonDepth);
}

ProjectDocument project_document_from_json(
    std::string_view json) {
    auto root = detail::parse_strict_json(
        json,
        "Project document",
        detail::kMaximumProjectJsonBytes,
        detail::kMaximumJsonDepth);
    return normalize_and_validate(parse_document(root));
}

std::string project_document_to_json(
    const ProjectDocument& document,
    bool pretty) {
    auto root = document_json(document);
    if (!pretty) {
        return detail::canonical_json_dump(std::move(root));
    }
    try {
        return root.dump(
            2,
            ' ',
            false,
            Json::error_handler_t::strict);
    }
    catch (const nlohmann::json::exception& error) {
        throw ProjectJsonError(
            std::string("Project document: ") + error.what());
    }
}

std::string project_document_to_canonical_json(
    const ProjectDocument& document) {
    return project_document_to_json(document, false);
}

std::string project_content_hash(
    const ProjectDocument& document) {
    return "sha256:" +
        sha256_hex(
            project_document_to_canonical_json(document));
}

std::string sha256_hex(std::string_view bytes) {
    return detail::sha256_hex_digest(bytes);
}

} // namespace pamguard::project
