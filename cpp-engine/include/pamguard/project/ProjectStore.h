#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "pamguard/project/ProjectDocument.h"
#include "pamguard/project/ProjectMigration.h"

namespace pamguard::project {

namespace detail {
class ProjectStoreRootHandle;
}

inline constexpr std::uint32_t kProjectFileFormatVersion = 1;
inline constexpr std::uint32_t kProjectCanonicalizationVersion = 1;

struct ProjectFileEnvelope {
    std::uint32_t file_format_version = kProjectFileFormatVersion;
    std::uint32_t canonicalization_version =
        kProjectCanonicalizationVersion;
    std::uint64_t authority_revision = 0;
    std::uint64_t saved_revision = 0;
    std::string content_hash;
    std::int64_t saved_at_unix_ms = 0;
    ProjectDocument project;

    bool operator==(const ProjectFileEnvelope&) const = default;
};

/**
 * The durable identity observed when a project was loaded.
 *
 * Save compares this tuple, including a hash of the normalized complete
 * envelope, with the current file before replacing it. Semantically identical
 * whitespace/key-order rewrites remain identical. The comparison is
 * independent of filesystem timestamps, which are neither portable nor
 * sufficiently precise to be a concurrency token.
 */
struct ProjectFileFingerprint {
    std::uint64_t authority_revision = 0;
    std::uint64_t saved_revision = 0;
    std::string content_hash;
    /**
     * Hash of the normalized complete file envelope, including saved time and
     * envelope versions. This closes the gap where an external writer could
     * change a valid envelope without changing the project content tuple.
     */
    std::string envelope_hash;

    bool operator==(const ProjectFileFingerprint&) const = default;
};

struct LoadedProjectFile {
    ProjectFileEnvelope envelope;
    ProjectFileFingerprint fingerprint;
    ProjectMigrationReport migration;
};

enum class SavedProjectStatus {
    Available,
    Corrupt,
    Unsupported,
};

struct SavedProjectSummary {
    std::string project_id;
    std::string name;
    std::string description;
    std::uint64_t saved_revision = 0;
    std::int64_t saved_at_unix_ms = 0;
    SavedProjectStatus status = SavedProjectStatus::Available;
    std::string issue;
};

class ProjectStoreError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ProjectFileConflict : public ProjectStoreError {
public:
    using ProjectStoreError::ProjectStoreError;
};

/**
 * The atomic publish point was crossed, but the platform could not prove the
 * directory entry durable. Callers must reload before retrying; blindly
 * repeating a create or replace could misreport the already-published state.
 */
class ProjectDurabilityError : public ProjectStoreError {
public:
    using ProjectStoreError::ProjectStoreError;

    [[nodiscard]] bool publication_may_have_committed() const noexcept {
        return true;
    }
};

class UnsupportedProjectFile : public ProjectStoreError {
public:
    using ProjectStoreError::ProjectStoreError;
};

class CorruptProjectFile : public ProjectStoreError {
public:
    using ProjectStoreError::ProjectStoreError;
};

/**
 * UUID-addressed, server-rooted storage for portable PAMGuard projects.
 *
 * Clients never supply paths. create() is an exclusive Save As operation;
 * replace() succeeds only if the durable fingerprint is unchanged since load.
 * The configured root and its parent are an engine-owned trust boundary and
 * must not be writable by untrusted principals. Identity/reparse checks and
 * the cooperative writer lock are defense in depth, not a substitute for
 * directory ownership. POSIX storage operations are anchored to a retained
 * directory descriptor. Windows retains a directory handle as an identity
 * reference and revalidates the configured path around operations, but Win32
 * has no general public handle-relative rename API; the engine-owned root and
 * parent therefore remain mandatory there.
 */
class ProjectStore {
public:
    explicit ProjectStore(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::filesystem::path path_for(
        const std::string& project_id) const;
    [[nodiscard]] bool exists(const std::string& project_id) const;

    [[nodiscard]] LoadedProjectFile load(
        const std::string& project_id) const;
    [[nodiscard]] std::vector<SavedProjectSummary> list() const;

    [[nodiscard]] ProjectFileFingerprint create(
        const ProjectFileEnvelope& envelope);
    [[nodiscard]] ProjectFileFingerprint replace(
        const ProjectFileEnvelope& envelope,
        const ProjectFileFingerprint& expected);

private:
    std::filesystem::path root_;
    std::uint64_t root_identity_high_ = 0;
    std::uint64_t root_identity_low_ = 0;
    std::shared_ptr<detail::ProjectStoreRootHandle> root_handle_;
};

[[nodiscard]] std::string project_file_envelope_to_json(
    const ProjectFileEnvelope& envelope,
    bool pretty = false);
[[nodiscard]] ProjectFileEnvelope project_file_envelope_from_json(
    std::string_view json);
[[nodiscard]] ProjectFileFingerprint project_file_fingerprint(
    const ProjectFileEnvelope& envelope);

} // namespace pamguard::project
