#include "pamguard/project/ProjectMigration.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

#include <json.hpp>

#include "CanonicalJson.h"
#include "pamguard/project/ProjectJson.h"

namespace pamguard::project {

namespace {

using Json = nlohmann::json;

using MigrationTransform = void (*)(Json&);

struct MigrationStep {
    std::uint32_t from_schema_version;
    std::uint32_t to_schema_version;
    const char* id;
    const char* summary;
    MigrationTransform transform;
};

void migrate_source_role_v0_to_v1(
    Json& source,
    const std::unordered_map<std::string, std::string>& unit_types) {
    if (!source.is_object()) {
        return;
    }
    const auto unit_id = source.find("unitId");
    const auto output_role = source.find("outputRole");
    if (unit_id == source.end() ||
        output_role == source.end() ||
        !unit_id->is_string() ||
        !output_role->is_string()) {
        return;
    }
    const auto type = unit_types.find(unit_id->get<std::string>());
    if (type == unit_types.end()) {
        return;
    }
    const auto role = output_role->get<std::string>();
    if (type->second == "pamguard.acquisition" && role == "raw") {
        *output_role = "rawAudio";
    }
    else if (type->second == "pamguard.fft" &&
             role == "fftOutput") {
        *output_role = "fft";
    }
    else if (type->second == "pamguard.click-detector" &&
             role == "clickDetections") {
        *output_role = "clicks";
    }
}

void upgrade_zero_version(Json& object, const char* field) {
    if (!object.is_object()) {
        return;
    }
    const auto value = object.find(field);
    if (value != object.end() &&
        value->is_number_unsigned() &&
        value->get<std::uint64_t>() == 0) {
        *value = 1;
    }
}

/**
 * Schema 0 was the deliberately narrow pre-version project fixture used to
 * prove the migration boundary before a production v2 can be published. Its
 * object shape is otherwise v1, but descriptor/recipe/provider references
 * were unversioned (encoded as zero) and three public output roles used their
 * prototype names.
 */
void migrate_v0_to_v1(Json& root) {
    root["schemaVersion"] = 1;

    const auto descriptor_set = root.find("descriptorSet");
    if (descriptor_set != root.end()) {
        upgrade_zero_version(*descriptor_set, "version");
    }

    std::unordered_map<std::string, std::string> unit_types;
    const auto units = root.find("controlledUnits");
    if (units != root.end() && units->is_array()) {
        for (auto& unit : *units) {
            if (!unit.is_object()) {
                continue;
            }
            const auto id = unit.find("id");
            const auto type_id = unit.find("typeId");
            if (id != unit.end() &&
                type_id != unit.end() &&
                id->is_string() &&
                type_id->is_string()) {
                unit_types.emplace(
                    id->get<std::string>(),
                    type_id->get<std::string>());
            }
            upgrade_zero_version(unit, "descriptorVersion");
            const auto recipe = unit.find("recipe");
            if (recipe != unit.end()) {
                upgrade_zero_version(*recipe, "version");
            }
        }

        for (auto& unit : *units) {
            if (!unit.is_object()) {
                continue;
            }
            const auto bindings = unit.find("bindings");
            if (bindings == unit.end() || !bindings->is_array()) {
                continue;
            }
            for (auto& binding : *bindings) {
                if (!binding.is_object()) {
                    continue;
                }
                const auto sources = binding.find("sources");
                if (sources == binding.end() ||
                    !sources->is_array()) {
                    continue;
                }
                for (auto& source : *sources) {
                    migrate_source_role_v0_to_v1(
                        source,
                        unit_types);
                }
            }
        }
    }

    const auto display_tabs = root.find("displayTabs");
    if (display_tabs == root.end() || !display_tabs->is_array()) {
        return;
    }
    for (auto& tab : *display_tabs) {
        if (!tab.is_object()) {
            continue;
        }
        const auto displays = tab.find("displays");
        if (displays == tab.end() || !displays->is_array()) {
            continue;
        }
        for (auto& display : *displays) {
            if (!display.is_object()) {
                continue;
            }
            upgrade_zero_version(display, "providerVersion");
            const auto source = display.find("source");
            if (source != display.end() && !source->is_null()) {
                migrate_source_role_v0_to_v1(
                    *source,
                    unit_types);
            }
        }
    }
}

constexpr std::array<MigrationStep, 1> kMigrationRegistry{{
    {
        0,
        1,
        "project-schema-v0-to-v1",
        "Versioned prototype descriptors and canonicalized public "
        "output-role aliases",
        &migrate_v0_to_v1,
    },
}};

std::uint32_t source_schema_version(const Json& root) {
    if (!root.is_object()) {
        throw ProjectJsonError("/: must be an object");
    }
    const auto version = root.find("schemaVersion");
    if (version == root.end()) {
        throw ProjectJsonError(
            "/schemaVersion: required field is missing");
    }
    if (!version->is_number_unsigned()) {
        throw ProjectJsonError(
            "/schemaVersion: must be an unsigned integer");
    }
    const auto value = version->get<std::uint64_t>();
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw UnsupportedProjectSchema(
            "Unsupported project document schema version " +
            std::to_string(value) +
            "; this engine supports through version " +
            std::to_string(kProjectSchemaVersion));
    }
    return static_cast<std::uint32_t>(value);
}

const MigrationStep* migration_from(std::uint32_t version) {
    const auto step = std::find_if(
        kMigrationRegistry.begin(),
        kMigrationRegistry.end(),
        [version](const auto& candidate) {
            return candidate.from_schema_version == version;
        });
    return step == kMigrationRegistry.end() ? nullptr : &*step;
}

} // namespace

MigratedProjectDocument
migrate_persisted_project_document_from_json(std::string_view json) {
    auto source = detail::parse_strict_json(
        json,
        "Persisted project document",
        detail::kMaximumProjectJsonBytes,
        detail::kMaximumJsonDepth);
    const auto source_version = source_schema_version(source);

    if (source_version > kProjectSchemaVersion) {
        throw UnsupportedProjectSchema(
            "Unsupported future project document schema version " +
            std::to_string(source_version) +
            "; this engine supports through version " +
            std::to_string(kProjectSchemaVersion));
    }

    MigratedProjectDocument result;
    result.report.source_schema_version = source_version;
    result.report.target_schema_version = kProjectSchemaVersion;

    if (source_version == kProjectSchemaVersion) {
        result.project = project_document_from_json(json);
        result.source_content_hash =
            project_content_hash(result.project);
        return result;
    }

    result.source_content_hash =
        "sha256:" +
        sha256_hex(detail::canonical_json_dump(source));

    auto version = source_version;
    while (version < kProjectSchemaVersion) {
        const auto* step = migration_from(version);
        if (step == nullptr ||
            step->to_schema_version <= version ||
            step->to_schema_version > kProjectSchemaVersion) {
            throw UnsupportedProjectSchema(
                "No ordered project migration is registered from schema "
                "version " +
                std::to_string(version) +
                " to current version " +
                std::to_string(kProjectSchemaVersion));
        }
        step->transform(source);
        const auto transformed_version =
            source_schema_version(source);
        if (transformed_version != step->to_schema_version) {
            throw std::logic_error(
                std::string("Project migration '") +
                step->id +
                "' produced schema version " +
                std::to_string(transformed_version) +
                " instead of " +
                std::to_string(step->to_schema_version));
        }
        result.report.applied.push_back({
            step->id,
            step->from_schema_version,
            step->to_schema_version,
            step->summary,
        });
        version = transformed_version;
    }

    result.project = project_document_from_json(
        detail::canonical_json_dump(std::move(source)));
    return result;
}

} // namespace pamguard::project
