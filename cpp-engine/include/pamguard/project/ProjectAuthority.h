#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "pamguard/core/ModuleGraph.h"
#include "pamguard/project/ControlledUnitRegistry.h"
#include "pamguard/project/ProjectDocument.h"
#include "pamguard/project/ProjectProjection.h"
#include "pamguard/project/ProjectStore.h"

namespace pamguard::project {

enum class DependencyPolicy {
    Reject,
    AddDefaults,
};

enum class DependantRemovalPolicy {
    Reject,
    LeaveUnbound,
};

struct ProjectEntityReference {
    std::optional<std::string> id;
    std::optional<std::string> client_ref;

    bool operator==(const ProjectEntityReference&) const = default;
};

struct MutationSourceReference {
    ProjectEntityReference unit;
    std::string output_role;

    bool operator==(const MutationSourceReference&) const = default;
};

struct AddControlledUnitOperation {
    std::string client_ref;
    std::string type_id;
    std::optional<std::string> name;
    DependencyPolicy dependency_policy =
        DependencyPolicy::Reject;
};

inline constexpr std::string_view
    kClickMonitoringConfigurationTemplateId =
        "pamguard.click-monitoring";

/**
 * Atomically add one registered project configuration template.
 *
 * A template owns no persistent settings of its own. It expands to ordinary
 * controlled units, bindings, displays, and layout records in the candidate
 * ProjectDocument, so later edits use the same typed project authority.
 */
struct AddConfigurationTemplateOperation {
    std::string client_ref;
    std::string template_id;
};

struct RenameControlledUnitOperation {
    ProjectEntityReference unit;
    std::string name;
};

struct RemoveControlledUnitOperation {
    ProjectEntityReference unit;
    DependantRemovalPolicy dependant_policy =
        DependantRemovalPolicy::Reject;
};

struct ReorderControlledUnitsOperation {
    std::vector<ProjectEntityReference> units;
};

struct ReplaceControlledUnitSettingsOperation {
    ProjectEntityReference unit;
    std::uint32_t settings_version = 1;
    std::string settings_json = "{}";
};

/** Typed complete replacement of one registered global-settings component. */
struct ReplaceGlobalSettingsOperation {
    std::string type_id;
    std::uint32_t settings_version = 1;
    std::string settings_json = "{}";
};

struct SetControlledUnitBindingOperation {
    ProjectEntityReference unit;
    std::string input_role;
    std::vector<MutationSourceReference> sources;
};

struct ReplaceDataModelLayoutOperation {
    DataModelLayout layout;
};

/**
 * Typed complete replacement of the explicitly owned presentation hierarchy.
 *
 * Provider-specific settings and source references are still validated by the
 * project/registry projection. This is not a second Workspace authority.
 */
struct ReplaceDisplayHierarchyOperation {
    std::vector<DisplayTab> display_tabs;
};

using ProjectMutationOperation = std::variant<
    AddControlledUnitOperation,
    AddConfigurationTemplateOperation,
    RenameControlledUnitOperation,
    RemoveControlledUnitOperation,
    ReorderControlledUnitsOperation,
    ReplaceControlledUnitSettingsOperation,
    ReplaceGlobalSettingsOperation,
    SetControlledUnitBindingOperation,
    ReplaceDataModelLayoutOperation,
    ReplaceDisplayHierarchyOperation>;

struct ProjectMutationBatch {
    std::uint32_t schema_version = 1;
    bool validate_only = false;
    std::vector<ProjectMutationOperation> operations;
};

struct CreatedProjectEntity {
    std::string client_ref;
    std::string id;

    bool operator==(const CreatedProjectEntity&) const = default;
};

struct ActiveProjectSnapshot {
    ProjectDocument project;
    ProjectProjectionResult projection;
    std::uint64_t working_revision = 0;
    std::optional<std::uint64_t> saved_revision;
    std::uint64_t authority_revision = 0;
    std::string working_content_hash;
    std::optional<std::string> saved_content_hash;
    bool dirty = true;
    std::string etag;
};

struct ProjectMutationResult {
    bool changed = false;
    bool validated_only = false;
    std::vector<CreatedProjectEntity> created_entities;
    ActiveProjectSnapshot active;
};

/**
 * Opaque, exact candidate produced by a typed mutation.
 *
 * The candidate owns the generated controlled-unit identities used by its
 * projection. A service may prepare it, preflight that exact runtime graph,
 * then commit it. Committing rechecks the active ETag, so an intervening
 * writer cannot make a preflight stale.
 */
class PreparedProjectMutation {
public:
    PreparedProjectMutation(PreparedProjectMutation&&) noexcept = default;
    PreparedProjectMutation& operator=(
        PreparedProjectMutation&&) noexcept = default;

    PreparedProjectMutation(const PreparedProjectMutation&) = delete;
    PreparedProjectMutation& operator=(
        const PreparedProjectMutation&) = delete;

    [[nodiscard]] const ProjectMutationResult& preview() const noexcept;

private:
    friend class ProjectAuthority;

    PreparedProjectMutation() = default;

    std::string base_etag_;
    std::uint64_t committed_working_revision_ = 0;
    std::uint64_t committed_authority_revision_ = 0;
    ProjectMutationResult preview_;
};

/**
 * Opaque New/Open candidate used to stage a full project/runtime switch.
 *
 * The active authority remains unchanged until commit_project_switch().
 */
class PreparedProjectSwitch {
public:
    PreparedProjectSwitch(PreparedProjectSwitch&&) noexcept = default;
    PreparedProjectSwitch& operator=(
        PreparedProjectSwitch&&) noexcept = default;

    PreparedProjectSwitch(const PreparedProjectSwitch&) = delete;
    PreparedProjectSwitch& operator=(
        const PreparedProjectSwitch&) = delete;

    [[nodiscard]] const ActiveProjectSnapshot& preview() const noexcept;

private:
    friend class ProjectAuthority;

    PreparedProjectSwitch() = default;

    std::string base_etag_;
    ActiveProjectSnapshot preview_;
    std::optional<ProjectFileFingerprint> durable_fingerprint_;
};

/**
 * Opaque Save As candidate. Persistence and authority publication occur
 * together only in commit_save_as(), after callers preflight this exact
 * project identity and projection.
 */
class PreparedProjectSaveAs {
public:
    PreparedProjectSaveAs(PreparedProjectSaveAs&&) noexcept = default;
    PreparedProjectSaveAs& operator=(
        PreparedProjectSaveAs&&) noexcept = default;

    PreparedProjectSaveAs(const PreparedProjectSaveAs&) = delete;
    PreparedProjectSaveAs& operator=(
        const PreparedProjectSaveAs&) = delete;

    [[nodiscard]] const ActiveProjectSnapshot& preview() const noexcept;

private:
    friend class ProjectAuthority;

    PreparedProjectSaveAs() = default;

    std::string base_etag_;
    ActiveProjectSnapshot preview_;
};

class ProjectAuthorityError : public std::runtime_error {
public:
    ProjectAuthorityError(
        std::string code,
        std::string message,
        std::string current_etag = {});

    [[nodiscard]] const std::string& code() const noexcept;
    [[nodiscard]] const std::string& current_etag() const noexcept;

private:
    std::string code_;
    std::string current_etag_;
};

/**
 * One in-memory working project plus its separate durable saved baseline.
 *
 * This class owns project revisions, hashes, ETags, typed project mutations,
 * and persistence transactions. Runtime stop/prepare/swap remains coordinated
 * by the service under its outer lifecycle lock; candidates returned here
 * already passed pure projection and editor validation.
 */
class ProjectAuthority {
public:
    ProjectAuthority(
        const ControlledUnitRegistry& controlled_unit_registry,
        const core::ModuleRegistry& runtime_registry,
        ProjectStore& store);

    [[nodiscard]] ActiveProjectSnapshot snapshot() const;

    /**
     * Validate and project a mutation without changing active authority.
     *
     * Generated UUIDs and the projected graph are retained in the returned
     * opaque candidate, so runtime preflight and commit use identical
     * identities. validateOnly batches may be prepared but not committed.
     */
    [[nodiscard]] PreparedProjectMutation prepare_mutation(
        const std::string& expected_etag,
        const ProjectMutationBatch& batch) const;

    /** Commit a previously prepared exact mutation candidate. */
    [[nodiscard]] ProjectMutationResult commit_mutation(
        PreparedProjectMutation prepared);

    [[nodiscard]] ProjectMutationResult mutate(
        const std::string& expected_etag,
        const ProjectMutationBatch& batch);

    [[nodiscard]] PreparedProjectSwitch prepare_new_project(
        const std::string& expected_etag,
        std::string name,
        std::string description,
        bool discard_dirty) const;

    [[nodiscard]] PreparedProjectSwitch prepare_open(
        const std::string& expected_etag,
        const std::string& project_id,
        bool discard_dirty) const;

    /** Commit an exact New/Open candidate after runtime switch preflight. */
    [[nodiscard]] ActiveProjectSnapshot commit_project_switch(
        PreparedProjectSwitch prepared);

    [[nodiscard]] ActiveProjectSnapshot new_project(
        const std::string& expected_etag,
        std::string name,
        std::string description,
        bool discard_dirty);

    [[nodiscard]] ActiveProjectSnapshot open(
        const std::string& expected_etag,
        const std::string& project_id,
        bool discard_dirty);

    [[nodiscard]] ActiveProjectSnapshot save(
        const std::string& expected_etag);

    [[nodiscard]] PreparedProjectSaveAs prepare_save_as(
        const std::string& expected_etag,
        std::string name) const;

    /** Atomically create and publish a prepared Save As candidate. */
    [[nodiscard]] ActiveProjectSnapshot commit_save_as(
        PreparedProjectSaveAs prepared);

    [[nodiscard]] ActiveProjectSnapshot save_as(
        const std::string& expected_etag,
        std::string name);

    [[nodiscard]] std::vector<ProjectedPublicOutput>
    compatible_sources(
        const std::string& unit_id,
        const std::string& input_role) const;

private:
    struct State {
        ProjectDocument project;
        ProjectProjectionResult projection;
        std::uint64_t working_revision = 0;
        std::optional<std::uint64_t> saved_revision;
        std::uint64_t authority_revision = 0;
        std::string working_content_hash;
        std::optional<std::string> saved_content_hash;
        std::optional<ProjectFileFingerprint> durable_fingerprint;
    };

    [[nodiscard]] ActiveProjectSnapshot snapshot_unlocked() const;
    void require_etag_unlocked(
        const std::string& expected_etag) const;

    const ControlledUnitRegistry& controlled_unit_registry_;
    const core::ModuleRegistry& runtime_registry_;
    ProjectStore& store_;
    mutable std::mutex mutex_;
    State state_;
};

} // namespace pamguard::project
