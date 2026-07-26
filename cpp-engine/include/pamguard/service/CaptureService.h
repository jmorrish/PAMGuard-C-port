#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace pamguard::service {

enum class CaptureSourceKind {
    DirectShowDevice,
    HttpUrl,
};

struct CaptureIngestCommandOptions {
    std::string ingest_executable;
    std::string ffmpeg_executable;
    std::string engine_url;
    std::string session_id;
    std::string module_id;
    std::string project_id;
    std::string acquisition_unit_id;
    std::optional<std::uint64_t> working_revision;
    std::string source;
    CaptureSourceKind source_kind = CaptureSourceKind::HttpUrl;
    std::size_t sample_rate_hz = 0;
    std::size_t channel_count = 0;
    bool pass_api_key_environment = false;
};

/**
 * Stable, operator-visible capture identity.
 *
 * This deliberately names a project controlled-unit instance, not a legacy
 * session or a hidden runtime-graph module. The working revision protects
 * capture commands and child ingress from stale project state. Reconciliation
 * advances a retained host binding to the current revision while the same
 * stable Acquisition instance still exists.
 */
struct AcquisitionCaptureTarget {
    std::string project_id;
    std::string acquisition_unit_id;
    std::uint64_t working_revision = 0;

    bool operator==(const AcquisitionCaptureTarget&) const = default;
};

struct ExactAudioDeviceHostBinding {
    std::string device_name;

    bool operator==(const ExactAudioDeviceHostBinding&) const = default;
};

struct NonSecretHttpUrlHostBinding {
    std::string url;

    bool operator==(const NonSecretHttpUrlHostBinding&) const = default;
};

/**
 * The complete persisted host-binding payload.
 *
 * Executable paths, FFmpeg options, credentials, child PIDs, and running state
 * cannot be represented by this type.
 */
using AcquisitionHostBindingSource = std::variant<
    ExactAudioDeviceHostBinding,
    NonSecretHttpUrlHostBinding>;

struct AcquisitionHostBindingSnapshot {
    AcquisitionCaptureTarget target;
    AcquisitionHostBindingSource source;
    /** Compare-and-swap revision; zero is reserved for "does not exist". */
    std::uint64_t binding_revision = 0;

    bool operator==(const AcquisitionHostBindingSnapshot&) const = default;
};

class AcquisitionHostBindingConflict : public std::runtime_error {
public:
    AcquisitionHostBindingConflict(
        std::uint64_t expected_binding_revision,
        std::optional<std::uint64_t> current_binding_revision);

    [[nodiscard]] std::uint64_t
    expected_binding_revision() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t>
    current_binding_revision() const noexcept;

private:
    std::uint64_t expected_binding_revision_ = 0;
    std::optional<std::uint64_t> current_binding_revision_;
};

class StaleAcquisitionCaptureTarget : public std::runtime_error {
public:
    StaleAcquisitionCaptureTarget(
        std::uint64_t requested_working_revision,
        std::uint64_t current_working_revision);

    [[nodiscard]] std::uint64_t
    requested_working_revision() const noexcept;
    [[nodiscard]] std::uint64_t
    current_working_revision() const noexcept;

private:
    std::uint64_t requested_working_revision_ = 0;
    std::uint64_t current_working_revision_ = 0;
};

class InactiveAcquisitionCaptureTarget : public std::runtime_error {
public:
    InactiveAcquisitionCaptureTarget();
};

std::string capture_target_key(
    std::string_view session_id,
    std::string_view module_id);

/** Validate the bounded, delimiter-safe identifier syntax used by targets. */
[[nodiscard]] bool is_valid_acquisition_capture_target(
    const AcquisitionCaptureTarget& target) noexcept;

/**
 * Produce a collision-free registry key for the complete target.
 *
 * Components are length-prefixed, so delimiter-bearing valid IDs cannot alias
 * another project/unit/revision tuple.
 */
[[nodiscard]] std::string acquisition_capture_target_key(
    const AcquisitionCaptureTarget& target);

bool is_http_capture_url(std::string_view url);

/**
 * Strict host-binding URL policy: exact lowercase HTTP(S), a non-empty host,
 * no URI user-info/credentials, and no whitespace, controls, or backslashes.
 */
[[nodiscard]] bool is_non_secret_http_capture_url(
    std::string_view url) noexcept;

bool is_exact_enumerated_audio_device(
    const std::vector<std::pair<std::string, std::string>>& devices,
    std::string_view requested_device);

[[nodiscard]] bool is_valid_acquisition_host_binding(
    const AcquisitionHostBindingSource& source,
    const std::vector<std::pair<std::string, std::string>>& devices);

/**
 * Thread-safe host binding registry for project-owned Acquisition instances.
 *
 * `put()` uses zero as create-only and a non-zero exact binding revision as
 * update-only. An older revision is always rejected as stale. The service
 * integration must call reconcile_project() from the active authority
 * snapshot before exposing writes and after every project change.
 */
class AcquisitionHostBindingStore {
public:
    [[nodiscard]] AcquisitionHostBindingSnapshot put(
        const AcquisitionCaptureTarget& target,
        std::uint64_t expected_binding_revision,
        AcquisitionHostBindingSource source,
        const std::vector<std::pair<std::string, std::string>>& devices);

    /**
     * Return the exact target binding. A binding for the same project/unit at a
     * different working revision raises StaleAcquisitionCaptureTarget.
     */
    [[nodiscard]] std::optional<AcquisitionHostBindingSnapshot> find(
        const AcquisitionCaptureTarget& target) const;

    /**
     * Delete with compare-and-swap semantics.
     *
     * Expected revision zero makes deletion of an absent target idempotent,
     * but conflicts if a binding exists.
     */
    bool erase(
        const AcquisitionCaptureTarget& target,
        std::uint64_t expected_binding_revision);

    /**
     * Atomically advance retained bindings to the current project revision and
     * remove bindings whose stable Acquisition unit no longer exists.
     *
     * Removed snapshots are returned so an integration layer can first/also
     * quiesce any corresponding ephemeral capture child.
     */
    [[nodiscard]] std::vector<AcquisitionHostBindingSnapshot>
    reconcile_project(
        std::string_view project_id,
        std::uint64_t working_revision,
        const std::vector<std::string>& acquisition_unit_ids);

    [[nodiscard]] std::size_t size() const;

private:
    struct ProjectBindingScope {
        std::uint64_t working_revision = 0;
        std::vector<std::string> acquisition_unit_ids;
    };

    void require_active_target_unlocked(
        const AcquisitionCaptureTarget& target) const;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, AcquisitionHostBindingSnapshot>
        bindings_;
    /** Retained across deletion to prevent binding-revision ABA. */
    std::unordered_map<std::string, std::uint64_t>
        last_binding_revisions_;
    /** Populated by reconciliation to reject stale/deleted target writes. */
    std::unordered_map<std::string, ProjectBindingScope>
        project_scopes_;
};

std::vector<std::string> build_capture_ingest_command(
    const CaptureIngestCommandOptions& options);

template <typename CaptureMap, typename IsRunning, typename CloseCapture>
std::size_t reap_dead_capture_entries(
    CaptureMap& captures,
    IsRunning&& is_running,
    CloseCapture&& close_capture) {
    std::size_t reaped = 0;
    for (auto capture = captures.begin(); capture != captures.end();) {
        if (is_running(capture->second)) {
            ++capture;
            continue;
        }
        close_capture(capture->second);
        capture = captures.erase(capture);
        ++reaped;
    }
    return reaped;
}

} // namespace pamguard::service
