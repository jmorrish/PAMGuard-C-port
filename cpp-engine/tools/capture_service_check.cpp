#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "pamguard/service/CaptureService.h"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

template <typename Exception, typename Operation>
Exception require_throws(
    Operation&& operation,
    const char* message) {
    try {
        operation();
    }
    catch (const Exception& error) {
        return error;
    }
    catch (...) {
        std::cerr << message << " (wrong exception type)\n";
        std::exit(1);
    }
    std::cerr << message << " (no exception)\n";
    std::exit(1);
}

std::string option_value(
    const std::vector<std::string>& command,
    const std::string& option) {
    for (std::size_t index = 0; index + 1 < command.size(); ++index) {
        if (command[index] == option) {
            return command[index + 1];
        }
    }
    return {};
}

bool has_option(
    const std::vector<std::string>& command,
    const std::string& option) {
    for (const auto& argument : command) {
        if (argument == option) {
            return true;
        }
    }
    return false;
}

bool has_option_value(
    const std::vector<std::string>& command,
    const std::string& option,
    const std::string& value) {
    for (std::size_t index = 0;
         index + 1 < command.size();
         ++index) {
        if (command[index] == option &&
            command[index + 1] == value) {
            return true;
        }
    }
    return false;
}

struct FakeCapture {
    bool running = false;
    bool closed = false;
};

} // namespace

int main() {
    using pamguard::service::AcquisitionHostBindingConflict;
    using pamguard::service::AcquisitionCaptureTarget;
    using pamguard::service::AcquisitionHostBindingSource;
    using pamguard::service::AcquisitionHostBindingStore;
    using pamguard::service::CaptureIngestCommandOptions;
    using pamguard::service::CaptureSourceKind;
    using pamguard::service::ExactAudioDeviceHostBinding;
    using pamguard::service::InactiveAcquisitionCaptureTarget;
    using pamguard::service::NonSecretHttpUrlHostBinding;
    using pamguard::service::StaleAcquisitionCaptureTarget;

    static_assert(
        std::variant_size_v<AcquisitionHostBindingSource> == 2);

    CaptureIngestCommandOptions module_options;
    module_options.ingest_executable = "ffmpeg_stream_ingest.exe";
    module_options.ffmpeg_executable = "ffmpeg.exe";
    module_options.engine_url = "http://127.0.0.1:8080";
    module_options.module_id = "input-one";
    module_options.source = "https://example.invalid/live.mp3";
    module_options.source_kind = CaptureSourceKind::HttpUrl;
    module_options.sample_rate_hz = 48000;
    module_options.channel_count = 2;
    module_options.pass_api_key_environment = true;

    const auto module_key =
        pamguard::service::capture_target_key(
            module_options.session_id,
            module_options.module_id);
    const auto module_command =
        pamguard::service::build_capture_ingest_command(
            module_options);
    require(
        module_key == "module:input-one",
        "Module capture registry key lost its namespace");
    require(
        option_value(module_command, "--module") == "input-one",
        "Module ingest command did not receive the raw module ID");
    require(
        option_value(module_command, "--module") != module_key,
        "Module registry key leaked into the ingest API argument");
    require(
        !has_option(module_command, "--session"),
        "Module command unexpectedly contains a legacy session target");
    require(
        !has_option(module_command, "--resume-from-engine"),
        "Module command unexpectedly enabled legacy session resume");
    require(
        option_value(module_command, "--api-key-env") ==
            "PAMGUARD_CAPTURE_API_KEY",
        "API key environment forwarding changed");

    auto project_options = module_options;
    project_options.module_id.clear();
    project_options.project_id =
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    project_options.acquisition_unit_id =
        "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
    project_options.working_revision = 7;
    const auto project_command =
        pamguard::service::build_capture_ingest_command(
            project_options);
    require(
        option_value(project_command, "--project-id") ==
            project_options.project_id &&
            option_value(
                project_command,
                "--acquisition-unit-id") ==
                project_options.acquisition_unit_id &&
            option_value(project_command, "--working-revision") ==
                "7",
        "Project capture command lost its stable target identity");
    require(
        !has_option(project_command, "--module") &&
            !has_option(project_command, "--session") &&
            !has_option(
                project_command,
                "--resume-from-engine"),
        "Project capture command leaked a compatibility target");
    auto incomplete_project_options = project_options;
    incomplete_project_options.working_revision.reset();
    require_throws<std::invalid_argument>(
        [&] {
            (void)pamguard::service::
                build_capture_ingest_command(
                    incomplete_project_options);
        },
        "Incomplete project capture target was accepted");
    auto mixed_project_options = project_options;
    mixed_project_options.module_id = "hidden-runtime-node";
    require_throws<std::invalid_argument>(
        [&] {
            (void)pamguard::service::
                build_capture_ingest_command(
                    mixed_project_options);
        },
        "Mixed project/runtime capture target was accepted");

    auto session_options = module_options;
    session_options.module_id.clear();
    session_options.session_id = "legacy-one";
    session_options.source = "Microphone (Exact Name)";
    session_options.source_kind =
        CaptureSourceKind::DirectShowDevice;
    const auto session_command =
        pamguard::service::build_capture_ingest_command(
            session_options);
    require(
        pamguard::service::capture_target_key(
            session_options.session_id,
            session_options.module_id) ==
            "session:legacy-one",
        "Session capture registry key lost its namespace");
    require(
        option_value(session_command, "--session") ==
            "legacy-one",
        "Session ingest command did not receive the raw session ID");
    require(
        option_value(session_command, "--source") ==
            "audio=Microphone (Exact Name)",
        "DirectShow source argument changed");
    require(
        has_option_value(
            session_command,
            "--ffmpeg-input-option",
            "-audio_buffer_size") &&
            has_option_value(
                session_command,
                "--ffmpeg-input-option",
                "30") &&
            option_value(
                session_command,
                "--chunk-frames") == "480",
        "DirectShow capture lost its low-latency buffering contract");
    require(
        has_option(session_command, "--resume-from-engine") &&
            has_option(session_command, "--click-waveforms"),
        "Legacy session capture options changed");

    require(
        pamguard::service::is_http_capture_url(
            "http://example.invalid/live") &&
            pamguard::service::is_http_capture_url(
                "https://example.invalid/live"),
        "HTTP(S) capture URLs were rejected");
    require(
        !pamguard::service::is_http_capture_url(
            "HTTP://example.invalid/live") &&
            !pamguard::service::is_http_capture_url(
                "file:///tmp/audio.wav") &&
            !pamguard::service::is_http_capture_url(
                "rtsp://example.invalid/live") &&
            !pamguard::service::is_http_capture_url(
                "C:\\audio.wav"),
        "Non-http(s) input escaped the capture URL trust boundary");

    const std::vector<std::pair<std::string, std::string>>
        devices = {
            {"Microphone (Exact Name)", "audio"},
            {"Hydrophone Array 2", "audio"},
            {"Camera", "video"},
        };
    require(
        pamguard::service::is_exact_enumerated_audio_device(
            devices,
            "Microphone (Exact Name)"),
        "Exact enumerated audio device was rejected");
    require(
        !pamguard::service::is_exact_enumerated_audio_device(
            devices,
            "microphone (exact name)") &&
            !pamguard::service::is_exact_enumerated_audio_device(
                devices,
                "Microphone") &&
            !pamguard::service::is_exact_enumerated_audio_device(
                devices,
                "Microphone (Exact Name) ") &&
            !pamguard::service::is_exact_enumerated_audio_device(
                devices,
                "Camera"),
        "Device trust boundary stopped requiring an exact audio match");

    require(
        pamguard::service::is_non_secret_http_capture_url(
            "http://example.invalid/live") &&
            pamguard::service::is_non_secret_http_capture_url(
                "https://example.invalid:8443/audio/channel-1") &&
            pamguard::service::is_non_secret_http_capture_url(
                "https://[2001:db8::1]:8443/live") &&
            pamguard::service::is_non_secret_http_capture_url(
                "https://example.invalid/live?channel=1"),
        "Valid non-secret lower-case HTTP(S) URLs were rejected");
    require(
        !pamguard::service::is_non_secret_http_capture_url(
            "HTTP://example.invalid/live") &&
            !pamguard::service::is_non_secret_http_capture_url(
                "HTTPS://example.invalid/live") &&
            !pamguard::service::is_non_secret_http_capture_url(
                "file:///tmp/audio.wav") &&
            !pamguard::service::is_non_secret_http_capture_url(
                "C:\\audio.wav"),
        "Strict host binding accepted a scheme or arbitrary path");
    require(
        !pamguard::service::is_non_secret_http_capture_url(
            "https://operator:secret@example.invalid/live") &&
            !pamguard::service::is_non_secret_http_capture_url(
                "https://token@example.invalid/live"),
        "Strict host binding accepted URL credentials");
    require(
        !pamguard::service::is_non_secret_http_capture_url(
            "https://example.invalid/live\nnext") &&
            !pamguard::service::is_non_secret_http_capture_url(
                "https://example.invalid/live\tevil") &&
            !pamguard::service::is_non_secret_http_capture_url(
                "https://example.invalid\\@evil.invalid/live") &&
            !pamguard::service::is_non_secret_http_capture_url(
                "https:///missing-host") &&
            !pamguard::service::is_non_secret_http_capture_url(
                "https://example.invalid/bad path"),
        "Strict host binding accepted control, backslash, or malformed URL text");
    require(
        !pamguard::service::is_non_secret_http_capture_url(
            "https://user%3Asecret%40example.invalid/live") &&
            !pamguard::service::is_non_secret_http_capture_url(
                "https://example.invalid:0/live") &&
            !pamguard::service::is_non_secret_http_capture_url(
                "https://example.invalid:65536/live") &&
            !pamguard::service::is_non_secret_http_capture_url(
                "https://[2001:db8::1]:bad/live"),
        "Strict host binding accepted encoded authority or invalid port");

    const AcquisitionHostBindingSource exact_device =
        ExactAudioDeviceHostBinding{
            "Microphone (Exact Name)",
        };
    const AcquisitionHostBindingSource different_device =
        ExactAudioDeviceHostBinding{
            "Hydrophone Array 2",
        };
    const AcquisitionHostBindingSource http_stream =
        NonSecretHttpUrlHostBinding{
            "https://example.invalid/live",
        };
    require(
        pamguard::service::is_valid_acquisition_host_binding(
            exact_device,
            devices) &&
            pamguard::service::is_valid_acquisition_host_binding(
                http_stream,
                devices),
        "Valid Acquisition host binding variants were rejected");
    require(
        !pamguard::service::is_valid_acquisition_host_binding(
            ExactAudioDeviceHostBinding{
                "microphone (exact name)",
            },
            devices) &&
            !pamguard::service::is_valid_acquisition_host_binding(
                ExactAudioDeviceHostBinding{"Camera"},
                devices) &&
            !pamguard::service::is_valid_acquisition_host_binding(
                NonSecretHttpUrlHostBinding{
                    "https://user:password@example.invalid/live",
                },
                devices),
        "Acquisition host binding variant validation became permissive");

    const AcquisitionCaptureTarget first_target{
        "project:alpha",
        "acquisition:primary",
        10,
    };
    const AcquisitionCaptureTarget second_target{
        "project:alpha",
        "acquisition:backup",
        10,
    };
    require(
        pamguard::service::is_valid_acquisition_capture_target(
            first_target),
        "Valid project/Acquisition target was rejected");
    require(
        !pamguard::service::is_valid_acquisition_capture_target(
            {"", "acquisition", 1}) &&
            !pamguard::service::is_valid_acquisition_capture_target(
                {"project", "acquisition\\child", 1}) &&
            !pamguard::service::is_valid_acquisition_capture_target(
                {"project name", "acquisition", 1}),
        "Unsafe project/Acquisition target identifier was accepted");

    const auto first_key =
        pamguard::service::acquisition_capture_target_key(
            first_target);
    require(
        first_key ==
            pamguard::service::acquisition_capture_target_key(
                first_target),
        "Project/Acquisition target key is not stable");
    require(
        first_key !=
            pamguard::service::acquisition_capture_target_key(
                {"project", "alpha:acquisition:primary", 10}) &&
            first_key !=
                pamguard::service::acquisition_capture_target_key(
                    {"project:alpha", "acquisition:primary", 11}),
        "Length-safe target key aliased a different target");
    require_throws<std::invalid_argument>(
        [] {
            (void)pamguard::service::
                acquisition_capture_target_key(
                    {"../project", "acquisition", 1});
        },
        "Unsafe target key input was not rejected");

    AcquisitionHostBindingStore binding_store;
    const auto first_created = binding_store.put(
        first_target,
        0,
        exact_device,
        devices);
    const auto second_created = binding_store.put(
        second_target,
        0,
        http_stream,
        devices);
    require(
        first_created.target == first_target &&
            first_created.binding_revision == 1 &&
            std::get<ExactAudioDeviceHostBinding>(
                first_created.source).device_name ==
                "Microphone (Exact Name)" &&
            second_created.target == second_target &&
            second_created.binding_revision == 1 &&
            binding_store.size() == 2,
        "Independent Acquisition bindings were not created");

    const auto first_updated = binding_store.put(
        first_target,
        first_created.binding_revision,
        different_device,
        devices);
    require(
        first_updated.binding_revision == 2 &&
            std::get<ExactAudioDeviceHostBinding>(
                first_updated.source).device_name ==
                "Hydrophone Array 2" &&
            binding_store.find(second_target) ==
                std::optional{second_created},
        "Updating one Acquisition binding affected another instance");

    const auto create_conflict =
        require_throws<AcquisitionHostBindingConflict>(
            [&] {
                (void)binding_store.put(
                    first_target,
                    0,
                    http_stream,
                    devices);
            },
            "Create-only binding write overwrote an existing binding");
    require(
        create_conflict.expected_binding_revision() == 0 &&
            create_conflict.current_binding_revision() ==
                std::optional<std::uint64_t>{2},
        "Create conflict did not expose safe revision metadata");
    const auto update_conflict =
        require_throws<AcquisitionHostBindingConflict>(
            [&] {
                (void)binding_store.put(
                    first_target,
                    1,
                    http_stream,
                    devices);
            },
            "Stale binding revision overwrote a newer binding");
    require(
        update_conflict.expected_binding_revision() == 1 &&
            update_conflict.current_binding_revision() ==
                std::optional<std::uint64_t>{2},
        "Update conflict revision metadata changed");
    const auto missing_update_conflict =
        require_throws<AcquisitionHostBindingConflict>(
            [&] {
                (void)binding_store.put(
                    {
                        "project:alpha",
                        "acquisition:not-created",
                        10,
                    },
                    4,
                    http_stream,
                    devices);
            },
            "Update-only binding write created a missing binding");
    require(
        missing_update_conflict.expected_binding_revision() == 4 &&
            !missing_update_conflict
                 .current_binding_revision()
                 .has_value(),
        "Missing update conflict revision metadata changed");

    require_throws<std::invalid_argument>(
        [&] {
            (void)binding_store.put(
                {"project:alpha", "acquisition:invalid-device", 10},
                0,
                ExactAudioDeviceHostBinding{"Microphone"},
                devices);
        },
        "Non-enumerated device binding was persisted");
    require_throws<std::invalid_argument>(
        [&] {
            (void)binding_store.put(
                {"project:alpha", "acquisition:invalid-url", 10},
                0,
                NonSecretHttpUrlHostBinding{
                    "https://user:secret@example.invalid/live",
                },
                devices);
        },
        "Credential-bearing URL binding was persisted");
    require(
        binding_store.size() == 2,
        "Rejected bindings mutated the binding store");

    const AcquisitionCaptureTarget rebound_target{
        first_target.project_id,
        first_target.acquisition_unit_id,
        11,
    };
    const auto rebound = binding_store.put(
        rebound_target,
        first_updated.binding_revision,
        http_stream,
        devices);
    require(
        rebound.binding_revision == 3 &&
            rebound.target.working_revision == 11,
        "Binding was not explicitly rebound to a newer project revision");
    const auto stale_find =
        require_throws<StaleAcquisitionCaptureTarget>(
            [&] {
                (void)binding_store.find(first_target);
            },
            "Old project revision read a rebound Acquisition binding");
    require(
        stale_find.requested_working_revision() == 10 &&
            stale_find.current_working_revision() == 11,
        "Stale target read did not expose working revisions");
    require_throws<StaleAcquisitionCaptureTarget>(
        [&] {
            (void)binding_store.put(
                first_target,
                rebound.binding_revision,
                exact_device,
                devices);
        },
        "Older project revision replaced a newer Acquisition binding");
    require_throws<StaleAcquisitionCaptureTarget>(
        [&] {
            (void)binding_store.erase(
                first_target,
                rebound.binding_revision);
        },
        "Older project revision deleted a newer Acquisition binding");

    const auto delete_conflict =
        require_throws<AcquisitionHostBindingConflict>(
            [&] {
                (void)binding_store.erase(
                    second_target,
                    99);
            },
            "Stale binding revision deleted an Acquisition binding");
    require(
        delete_conflict.current_binding_revision() ==
            std::optional<std::uint64_t>{1} &&
            binding_store.find(second_target).has_value(),
        "Delete conflict changed the target binding");
    require(
        binding_store.erase(second_target, 1) &&
            !binding_store.find(second_target).has_value() &&
            !binding_store.erase(second_target, 0),
        "Exact or idempotent Acquisition binding deletion failed");
    const auto missing_delete_conflict =
        require_throws<AcquisitionHostBindingConflict>(
            [&] {
                (void)binding_store.erase(
                    second_target,
                    1);
            },
            "Update-only delete treated a missing binding as success");
    require(
        !missing_delete_conflict
             .current_binding_revision()
             .has_value(),
        "Missing delete conflict reported a current revision");
    const auto recreated_second = binding_store.put(
        second_target,
        0,
        http_stream,
        devices);
    require(
        recreated_second.binding_revision == 2,
        "Delete/recreate reset the binding revision");
    require_throws<AcquisitionHostBindingConflict>(
        [&] {
            (void)binding_store.put(
                second_target,
                1,
                exact_device,
                devices);
        },
        "Pre-delete binding revision passed after recreation");
    require(
        binding_store.erase(
            second_target,
            recreated_second.binding_revision),
        "Recreated binding could not be deleted exactly");

    AcquisitionHostBindingStore reconciliation_store;
    const AcquisitionCaptureTarget kept_target{
        "project:reconcile",
        "acquisition:kept",
        20,
    };
    const AcquisitionCaptureTarget deleted_target{
        "project:reconcile",
        "acquisition:deleted",
        20,
    };
    const AcquisitionCaptureTarget other_project_target{
        "project:other",
        "acquisition:other",
        3,
    };
    (void)reconciliation_store.put(
        kept_target,
        0,
        exact_device,
        devices);
    const auto deleted_binding =
        reconciliation_store.put(
            deleted_target,
            0,
            http_stream,
            devices);
    (void)reconciliation_store.put(
        other_project_target,
        0,
        http_stream,
        devices);
    const auto removed_deleted =
        reconciliation_store.reconcile_project(
            "project:reconcile",
            20,
            {"acquisition:kept"});
    require(
        removed_deleted.size() == 1 &&
            removed_deleted.front() == deleted_binding &&
            reconciliation_store.find(kept_target).has_value() &&
            reconciliation_store.find(other_project_target)
                .has_value(),
        "Project reconciliation did not isolate a deleted Acquisition unit");
    require_throws<InactiveAcquisitionCaptureTarget>(
        [&] {
            (void)reconciliation_store.put(
                deleted_target,
                0,
                http_stream,
                devices);
        },
        "Deleted Acquisition unit was rebound after reconciliation");
    const auto removed_on_revision_advance =
        reconciliation_store.reconcile_project(
            "project:reconcile",
            21,
            {"acquisition:kept"});
    const AcquisitionCaptureTarget current_kept_target{
        kept_target.project_id,
        kept_target.acquisition_unit_id,
        21,
    };
    const auto carried_forward =
        reconciliation_store.find(current_kept_target);
    require(
        removed_on_revision_advance.empty() &&
            carried_forward.has_value() &&
            carried_forward->target == current_kept_target &&
            carried_forward->binding_revision == 1 &&
            std::get<ExactAudioDeviceHostBinding>(
                carried_forward->source).device_name ==
                "Microphone (Exact Name)" &&
            reconciliation_store.size() == 2,
        "Unrelated project revision discarded the stable Acquisition binding");
    const auto reconciled_stale =
        require_throws<StaleAcquisitionCaptureTarget>(
            [&] {
                (void)reconciliation_store.put(
                    kept_target,
                    0,
                    exact_device,
                    devices);
            },
            "Old project revision was rebound after reconciliation");
    require(
        reconciled_stale.requested_working_revision() == 20 &&
            reconciled_stale.current_working_revision() == 21,
        "Reconciled stale target did not report the current revision");
    const auto updated_kept =
        reconciliation_store.put(
            current_kept_target,
            carried_forward->binding_revision,
            http_stream,
            devices);
    require(
        updated_kept.binding_revision == 2 &&
            std::get<NonSecretHttpUrlHostBinding>(
                updated_kept.source).url ==
                "https://example.invalid/live",
        "Carried-forward binding lost compare-and-swap revision semantics");
    require_throws<std::invalid_argument>(
        [&] {
            (void)reconciliation_store.reconcile_project(
                "project:other",
                3,
                {"acquisition:other", "acquisition:other"});
        },
        "Project reconciliation accepted duplicate Acquisition IDs");
    require(
        reconciliation_store.find(other_project_target)
            .has_value(),
        "Rejected reconciliation mutated another project");

    std::unordered_map<std::string, FakeCapture> captures;
    captures.emplace(
        "module:live",
        FakeCapture{true, false});
    captures.emplace(
        "module:dead",
        FakeCapture{false, false});
    std::size_t close_count = 0;
    const auto reaped =
        pamguard::service::reap_dead_capture_entries(
            captures,
            [](const FakeCapture& capture) {
                return capture.running;
            },
            [&close_count](FakeCapture& capture) {
                capture.closed = true;
                ++close_count;
            });
    require(
        reaped == 1 &&
            close_count == 1 &&
            captures.size() == 1 &&
            captures.contains("module:live"),
        "Dead capture entries were not closed and reaped");

    std::cout
        << "capture service command/trust/binding/reaping checks passed\n";
    return 0;
}
