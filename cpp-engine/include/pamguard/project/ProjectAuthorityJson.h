#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "pamguard/project/ProjectAuthority.h"

namespace pamguard::project {

inline constexpr std::uint32_t kProjectAuthorityJsonSchemaVersion = 1;

class ProjectAuthorityJsonError : public std::invalid_argument {
public:
    explicit ProjectAuthorityJsonError(const std::string& message)
        : std::invalid_argument(message) {}
};

struct NewProjectRequest {
    std::string name;
    std::string description;
    bool discard_dirty = false;

    bool operator==(const NewProjectRequest&) const = default;
};

struct OpenProjectRequest {
    std::string project_id;
    bool discard_dirty = false;

    bool operator==(const OpenProjectRequest&) const = default;
};

struct SaveAsProjectRequest {
    std::string name;

    bool operator==(const SaveAsProjectRequest&) const = default;
};

/** Deterministic schemaVersion 1 active-project response, including ETag. */
[[nodiscard]] std::string active_project_snapshot_to_json(
    const ActiveProjectSnapshot& snapshot,
    bool pretty = false);

/** Deterministic full generated-runtime/ownership inspection response. */
[[nodiscard]] std::string project_inspection_to_json(
    const ActiveProjectSnapshot& snapshot,
    bool pretty = false);

[[nodiscard]] std::string project_compatible_sources_to_json(
    std::string_view unit_id,
    std::string_view input_role,
    const std::vector<ProjectedPublicOutput>& sources,
    bool pretty = false);

[[nodiscard]] std::string saved_project_list_to_json(
    const std::vector<SavedProjectSummary>& projects,
    bool pretty = false);

[[nodiscard]] std::string project_mutation_result_to_json(
    const ProjectMutationResult& result,
    bool pretty = false);

/**
 * Strict typed mutation-batch parsing.
 *
 * The parser rejects malformed UTF-8, duplicate and unknown fields, invalid
 * enum values/references, oversized bodies/collections, and non-object
 * settings. Concurrency is carried only by the HTTP If-Match ETag; an
 * expectedRevision body member is never accepted.
 */
[[nodiscard]] ProjectMutationBatch project_mutation_batch_from_json(
    std::string_view json);

/** Deterministic mutation JSON, primarily for clients and contract fixtures. */
[[nodiscard]] std::string project_mutation_batch_to_json(
    const ProjectMutationBatch& batch,
    bool pretty = false);

[[nodiscard]] NewProjectRequest new_project_request_from_json(
    std::string_view json);
[[nodiscard]] OpenProjectRequest open_project_request_from_json(
    std::string_view json);
[[nodiscard]] SaveAsProjectRequest save_as_project_request_from_json(
    std::string_view json);

} // namespace pamguard::project
