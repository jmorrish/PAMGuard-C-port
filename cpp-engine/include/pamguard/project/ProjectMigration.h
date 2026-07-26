#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "pamguard/project/ProjectDocument.h"

namespace pamguard::project {

/**
 * One deterministic, ordered persisted-project schema migration.
 *
 * The identifier is stable diagnostic data. It must not be derived from
 * implementation details or translated operator text.
 */
struct ProjectMigrationDiagnostic {
    std::string id;
    std::uint32_t from_schema_version = 0;
    std::uint32_t to_schema_version = 0;
    std::string summary;

    bool operator==(const ProjectMigrationDiagnostic&) const = default;
};

struct ProjectMigrationReport {
    std::uint32_t source_schema_version = kProjectSchemaVersion;
    std::uint32_t target_schema_version = kProjectSchemaVersion;
    std::vector<ProjectMigrationDiagnostic> applied;

    [[nodiscard]] bool migrated() const noexcept {
        return source_schema_version != target_schema_version;
    }

    bool operator==(const ProjectMigrationReport&) const = default;
};

struct MigratedProjectDocument {
    ProjectDocument project;
    /**
     * Hash expected in the source envelope before migration.
     *
     * Current v1 documents use the normal project content hash. The declared
     * v0 fixture uses canonical JSON for its pre-version document shape.
     */
    std::string source_content_hash;
    ProjectMigrationReport report;
};

class UnsupportedProjectSchema : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/**
 * Parse a persisted project document through the ordered migration registry.
 *
 * Current documents pass through the strict project parser unchanged. Older
 * registered schemas are transformed one version at a time and then parsed,
 * normalized, and validated as the current authoritative document. Unknown
 * past schemas and all future schemas are rejected before use.
 */
[[nodiscard]] MigratedProjectDocument
migrate_persisted_project_document_from_json(std::string_view json);

} // namespace pamguard::project
