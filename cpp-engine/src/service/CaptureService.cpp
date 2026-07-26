#include "pamguard/service/CaptureService.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace pamguard::service {

namespace {

constexpr std::size_t kMaximumCaptureIdentityLength = 128;
constexpr std::size_t kMaximumCaptureUrlLength = 8192;

bool is_capture_identity(std::string_view value) noexcept {
    if (value.empty() ||
        value.size() > kMaximumCaptureIdentityLength) {
        return false;
    }
    const auto is_alphanumeric = [](const char character) {
        return (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9');
    };
    if (!is_alphanumeric(value.front())) {
        return false;
    }
    return std::all_of(
        value.begin(),
        value.end(),
        [is_alphanumeric](const char character) {
            return is_alphanumeric(character) ||
                character == '.' ||
                character == '_' ||
                character == ':' ||
                character == '-';
        });
}

bool is_valid_url_port(
    const std::string_view port) noexcept {
    if (port.empty() || port.size() > 5) {
        return false;
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(
        port.data(),
        port.data() + port.size(),
        value);
    return error == std::errc{} &&
        end == port.data() + port.size() &&
        value > 0 &&
        value <= 65535;
}

void append_key_part(
    std::string& destination,
    std::string_view value) {
    std::array<char, 32> length_buffer{};
    const auto [end, error] = std::to_chars(
        length_buffer.data(),
        length_buffer.data() + length_buffer.size(),
        value.size());
    if (error != std::errc{}) {
        throw std::runtime_error(
            "Could not encode capture target component");
    }
    destination.append(length_buffer.data(), end);
    destination.push_back(':');
    destination.append(value);
    destination.push_back(';');
}

void append_key_revision(
    std::string& destination,
    const std::uint64_t revision) {
    std::array<char, 32> revision_buffer{};
    const auto [end, error] = std::to_chars(
        revision_buffer.data(),
        revision_buffer.data() + revision_buffer.size(),
        revision);
    if (error != std::errc{}) {
        throw std::runtime_error(
            "Could not encode capture target revision");
    }
    append_key_part(
        destination,
        std::string_view(
            revision_buffer.data(),
            static_cast<std::size_t>(
                end - revision_buffer.data())));
}

std::string acquisition_instance_key(
    std::string_view project_id,
    std::string_view acquisition_unit_id) {
    if (!is_capture_identity(project_id) ||
        !is_capture_identity(acquisition_unit_id)) {
        throw std::invalid_argument(
            "Acquisition target identifiers are invalid");
    }
    std::string key = "project-acquisition-instance-v1;";
    key.reserve(
        key.size() +
        project_id.size() +
        acquisition_unit_id.size() +
        32);
    append_key_part(key, project_id);
    append_key_part(key, acquisition_unit_id);
    return key;
}

std::uint64_t next_binding_revision(
    const std::uint64_t current) {
    if (current == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "Acquisition host binding revision exhausted");
    }
    return current + 1;
}

void require_host_binding(
    const AcquisitionHostBindingSource& source,
    const std::vector<std::pair<std::string, std::string>>& devices) {
    if (!is_valid_acquisition_host_binding(source, devices)) {
        throw std::invalid_argument(
            "Acquisition host binding violates the capture trust boundary");
    }
}

void require_same_target_revision(
    const AcquisitionCaptureTarget& requested,
    const AcquisitionHostBindingSnapshot& current) {
    if (requested.working_revision !=
        current.target.working_revision) {
        throw StaleAcquisitionCaptureTarget(
            requested.working_revision,
            current.target.working_revision);
    }
}

void require_binding_revision(
    const std::uint64_t expected,
    const std::optional<std::uint64_t> current) {
    if (!current || expected != *current) {
        throw AcquisitionHostBindingConflict(
            expected,
            current);
    }
}

} // namespace

AcquisitionHostBindingConflict::AcquisitionHostBindingConflict(
    const std::uint64_t expected_binding_revision,
    std::optional<std::uint64_t> current_binding_revision)
    : std::runtime_error(
          "Acquisition host binding revision conflict"),
      expected_binding_revision_(expected_binding_revision),
      current_binding_revision_(current_binding_revision) {
}

std::uint64_t
AcquisitionHostBindingConflict::expected_binding_revision() const noexcept {
    return expected_binding_revision_;
}

std::optional<std::uint64_t>
AcquisitionHostBindingConflict::current_binding_revision() const noexcept {
    return current_binding_revision_;
}

StaleAcquisitionCaptureTarget::StaleAcquisitionCaptureTarget(
    const std::uint64_t requested_working_revision,
    const std::uint64_t current_working_revision)
    : std::runtime_error(
          "Acquisition capture target working revision is stale"),
      requested_working_revision_(requested_working_revision),
      current_working_revision_(current_working_revision) {
}

std::uint64_t
StaleAcquisitionCaptureTarget::requested_working_revision() const noexcept {
    return requested_working_revision_;
}

std::uint64_t
StaleAcquisitionCaptureTarget::current_working_revision() const noexcept {
    return current_working_revision_;
}

InactiveAcquisitionCaptureTarget::
InactiveAcquisitionCaptureTarget()
    : std::runtime_error(
          "Acquisition controlled-unit instance is not active") {
}

std::string capture_target_key(
    std::string_view session_id,
    std::string_view module_id) {
    if (session_id.empty() == module_id.empty()) {
        throw std::invalid_argument(
            "exactly one capture target is required");
    }
    return module_id.empty()
        ? "session:" + std::string(session_id)
        : "module:" + std::string(module_id);
}

bool is_valid_acquisition_capture_target(
    const AcquisitionCaptureTarget& target) noexcept {
    return is_capture_identity(target.project_id) &&
        is_capture_identity(target.acquisition_unit_id);
}

std::string acquisition_capture_target_key(
    const AcquisitionCaptureTarget& target) {
    if (!is_valid_acquisition_capture_target(target)) {
        throw std::invalid_argument(
            "Acquisition capture target is invalid");
    }
    std::string key = "project-acquisition-target-v1;";
    key.reserve(
        key.size() +
        target.project_id.size() +
        target.acquisition_unit_id.size() +
        64);
    append_key_part(key, target.project_id);
    append_key_part(key, target.acquisition_unit_id);
    append_key_revision(key, target.working_revision);
    return key;
}

bool is_http_capture_url(std::string_view url) {
    return url.starts_with("http://") ||
        url.starts_with("https://");
}

bool is_non_secret_http_capture_url(
    const std::string_view url) noexcept {
    if (url.empty() || url.size() > kMaximumCaptureUrlLength) {
        return false;
    }

    std::size_t authority_offset = 0;
    if (url.starts_with("http://")) {
        authority_offset = 7;
    }
    else if (url.starts_with("https://")) {
        authority_offset = 8;
    }
    else {
        return false;
    }

    for (const auto character : url) {
        const auto byte =
            static_cast<unsigned char>(character);
        if (byte <= 0x20U ||
            byte == 0x7FU ||
            character == '\\') {
            return false;
        }
    }

    const auto authority_end =
        url.find_first_of("/?#", authority_offset);
    const auto authority = url.substr(
        authority_offset,
        authority_end == std::string_view::npos
            ? url.size() - authority_offset
            : authority_end - authority_offset);
    if (authority.empty() ||
        authority.find('@') != std::string_view::npos) {
        return false;
    }

    if (authority.front() == '[') {
        const auto bracket = authority.find(']');
        if (bracket == std::string_view::npos ||
            bracket == 1) {
            return false;
        }
        for (std::size_t index = 1; index < bracket; ++index) {
            const auto character = authority[index];
            const bool hexadecimal =
                (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f') ||
                (character >= 'A' && character <= 'F');
            if (!hexadecimal &&
                character != ':' &&
                character != '.') {
                return false;
            }
        }
        if (bracket + 1 == authority.size()) {
            return true;
        }
        if (authority[bracket + 1] != ':') {
            return false;
        }
        return is_valid_url_port(
            authority.substr(bracket + 2));
    }

    const auto colon = authority.rfind(':');
    const auto host = colon == std::string_view::npos
        ? authority
        : authority.substr(0, colon);
    const auto port = colon == std::string_view::npos
        ? std::string_view{}
        : authority.substr(colon + 1);
    if (host.empty() ||
        host.find(':') != std::string_view::npos) {
        return false;
    }
    if (!std::all_of(
            host.begin(),
            host.end(),
            [](const char character) {
                return (character >= 'A' && character <= 'Z') ||
                    (character >= 'a' && character <= 'z') ||
                    (character >= '0' && character <= '9') ||
                    character == '.' ||
                    character == '-';
            })) {
        return false;
    }
    return colon == std::string_view::npos ||
        is_valid_url_port(port);
}

bool is_exact_enumerated_audio_device(
    const std::vector<std::pair<std::string, std::string>>& devices,
    std::string_view requested_device) {
    return std::any_of(
        devices.begin(),
        devices.end(),
        [requested_device](const auto& entry) {
            return entry.second == "audio" &&
                entry.first == requested_device;
        });
}

bool is_valid_acquisition_host_binding(
    const AcquisitionHostBindingSource& source,
    const std::vector<std::pair<std::string, std::string>>& devices) {
    return std::visit(
        [&devices](const auto& binding) {
            using Binding = std::decay_t<decltype(binding)>;
            if constexpr (
                std::is_same_v<
                    Binding,
                    ExactAudioDeviceHostBinding>) {
                return is_exact_enumerated_audio_device(
                    devices,
                    binding.device_name);
            }
            else {
                return is_non_secret_http_capture_url(
                    binding.url);
            }
        },
        source);
}

AcquisitionHostBindingSnapshot
AcquisitionHostBindingStore::put(
    const AcquisitionCaptureTarget& target,
    const std::uint64_t expected_binding_revision,
    AcquisitionHostBindingSource source,
    const std::vector<std::pair<std::string, std::string>>& devices) {
    if (!is_valid_acquisition_capture_target(target)) {
        throw std::invalid_argument(
            "Acquisition capture target is invalid");
    }
    require_host_binding(source, devices);

    const auto key = acquisition_instance_key(
        target.project_id,
        target.acquisition_unit_id);
    std::lock_guard lock(mutex_);
    require_active_target_unlocked(target);
    const auto existing = bindings_.find(key);
    if (existing == bindings_.end()) {
        if (expected_binding_revision != 0) {
            throw AcquisitionHostBindingConflict(
                expected_binding_revision,
                std::nullopt);
        }
        const auto previous_revision =
            last_binding_revisions_.contains(key)
            ? last_binding_revisions_.at(key)
            : 0;
        AcquisitionHostBindingSnapshot created{
            target,
            std::move(source),
            next_binding_revision(previous_revision),
        };
        bindings_.emplace(key, created);
        last_binding_revisions_.insert_or_assign(
            key,
            created.binding_revision);
        return created;
    }

    if (target.working_revision <
        existing->second.target.working_revision) {
        throw StaleAcquisitionCaptureTarget(
            target.working_revision,
            existing->second.target.working_revision);
    }
    require_binding_revision(
        expected_binding_revision,
        existing->second.binding_revision);
    existing->second = {
        target,
        std::move(source),
        next_binding_revision(
            existing->second.binding_revision),
    };
    last_binding_revisions_.insert_or_assign(
        key,
        existing->second.binding_revision);
    return existing->second;
}

std::optional<AcquisitionHostBindingSnapshot>
AcquisitionHostBindingStore::find(
    const AcquisitionCaptureTarget& target) const {
    if (!is_valid_acquisition_capture_target(target)) {
        throw std::invalid_argument(
            "Acquisition capture target is invalid");
    }
    const auto key = acquisition_instance_key(
        target.project_id,
        target.acquisition_unit_id);
    std::lock_guard lock(mutex_);
    require_active_target_unlocked(target);
    const auto existing = bindings_.find(key);
    if (existing == bindings_.end()) {
        return std::nullopt;
    }
    require_same_target_revision(target, existing->second);
    return existing->second;
}

bool AcquisitionHostBindingStore::erase(
    const AcquisitionCaptureTarget& target,
    const std::uint64_t expected_binding_revision) {
    if (!is_valid_acquisition_capture_target(target)) {
        throw std::invalid_argument(
            "Acquisition capture target is invalid");
    }
    const auto key = acquisition_instance_key(
        target.project_id,
        target.acquisition_unit_id);
    std::lock_guard lock(mutex_);
    require_active_target_unlocked(target);
    const auto existing = bindings_.find(key);
    if (existing == bindings_.end()) {
        if (expected_binding_revision == 0) {
            return false;
        }
        throw AcquisitionHostBindingConflict(
            expected_binding_revision,
            std::nullopt);
    }
    require_same_target_revision(target, existing->second);
    require_binding_revision(
        expected_binding_revision,
        existing->second.binding_revision);
    bindings_.erase(existing);
    return true;
}

std::vector<AcquisitionHostBindingSnapshot>
AcquisitionHostBindingStore::reconcile_project(
    const std::string_view project_id,
    const std::uint64_t working_revision,
    const std::vector<std::string>& acquisition_unit_ids) {
    if (!is_capture_identity(project_id)) {
        throw std::invalid_argument(
            "Acquisition project identifier is invalid");
    }
    std::unordered_set<std::string> active_units;
    active_units.reserve(acquisition_unit_ids.size());
    for (const auto& unit_id : acquisition_unit_ids) {
        if (!is_capture_identity(unit_id)) {
            throw std::invalid_argument(
                "Acquisition unit identifier is invalid");
        }
        if (!active_units.emplace(unit_id).second) {
            throw std::invalid_argument(
                "Acquisition unit identifiers must be unique");
        }
    }

    std::vector<AcquisitionHostBindingSnapshot> removed;
    std::lock_guard lock(mutex_);
    project_scopes_.insert_or_assign(
        std::string(project_id),
        ProjectBindingScope{
            working_revision,
            acquisition_unit_ids,
        });
    for (auto binding = bindings_.begin();
         binding != bindings_.end();) {
        auto& target = binding->second.target;
        if (target.project_id != project_id) {
            ++binding;
            continue;
        }
        if (active_units.contains(target.acquisition_unit_id)) {
            // Host deployment intent belongs to the stable Acquisition
            // instance, not to an unrelated display/layout/project edit.
            // Capture children remain revision-scoped and are reconciled by
            // the service; only the binding is carried forward.
            target.working_revision = working_revision;
            ++binding;
            continue;
        }
        removed.push_back(binding->second);
        binding = bindings_.erase(binding);
    }
    return removed;
}

void AcquisitionHostBindingStore::require_active_target_unlocked(
    const AcquisitionCaptureTarget& target) const {
    const auto scope = project_scopes_.find(target.project_id);
    if (scope == project_scopes_.end()) {
        return;
    }
    if (target.working_revision !=
        scope->second.working_revision) {
        throw StaleAcquisitionCaptureTarget(
            target.working_revision,
            scope->second.working_revision);
    }
    if (std::find(
            scope->second.acquisition_unit_ids.begin(),
            scope->second.acquisition_unit_ids.end(),
            target.acquisition_unit_id) ==
        scope->second.acquisition_unit_ids.end()) {
        throw InactiveAcquisitionCaptureTarget();
    }
}

std::size_t
AcquisitionHostBindingStore::size() const {
    std::lock_guard lock(mutex_);
    return bindings_.size();
}

std::vector<std::string> build_capture_ingest_command(
    const CaptureIngestCommandOptions& options) {
    const bool session_target = !options.session_id.empty();
    const bool module_target = !options.module_id.empty();
    const bool project_target_requested =
        !options.project_id.empty() ||
        !options.acquisition_unit_id.empty() ||
        options.working_revision.has_value();
    const bool project_target =
        !options.project_id.empty() &&
        !options.acquisition_unit_id.empty() &&
        options.working_revision.has_value();
    if (project_target_requested && !project_target) {
        throw std::invalid_argument(
            "project capture requires project ID, Acquisition unit ID, "
            "and working revision");
    }
    const auto target_count =
        static_cast<unsigned>(session_target) +
        static_cast<unsigned>(module_target) +
        static_cast<unsigned>(project_target);
    if (target_count != 1) {
        throw std::invalid_argument(
            "exactly one capture target is required");
    }
    if (options.ingest_executable.empty() ||
        options.ffmpeg_executable.empty() ||
        options.engine_url.empty() ||
        options.source.empty() ||
        options.sample_rate_hz == 0 ||
        options.channel_count == 0) {
        throw std::invalid_argument(
            "capture ingest command options are incomplete");
    }
    if (options.source_kind == CaptureSourceKind::HttpUrl &&
        !is_http_capture_url(options.source)) {
        throw std::invalid_argument(
            "capture URL must start with http:// or https://");
    }

    std::vector<std::string> args = {
        options.ingest_executable,
    };
    if (options.source_kind == CaptureSourceKind::DirectShowDevice) {
        args.push_back("--ffmpeg-input-option");
        args.push_back("-f");
        args.push_back("--ffmpeg-input-option");
        args.push_back("dshow");
        // DirectShow otherwise uses the device's default audio buffer, which
        // is commonly a multiple of 500 ms. FFmpeg then releases many PCM
        // chunks at once and the live FFT/display visibly freezes between
        // bursts. Thirty milliseconds remained stable on the supported
        // Windows capture path while removing that operator-visible stall.
        args.push_back("--ffmpeg-input-option");
        args.push_back("-audio_buffer_size");
        args.push_back("--ffmpeg-input-option");
        args.push_back("30");
        args.push_back("--source");
        args.push_back("audio=" + options.source);
    }
    else {
        args.push_back("--source");
        args.push_back(options.source);
    }

    if (project_target) {
        args.push_back("--project-id");
        args.push_back(options.project_id);
        args.push_back("--acquisition-unit-id");
        args.push_back(options.acquisition_unit_id);
        args.push_back("--working-revision");
        args.push_back(std::to_string(*options.working_revision));
    }
    else {
        args.push_back(module_target ? "--module" : "--session");
        // The service registry uses a namespaced key such as "module:input".
        // The compatibility ingest API itself requires the raw graph/session
        // identifier.
        args.push_back(
            module_target ? options.module_id : options.session_id);
    }
    args.push_back("--engine");
    args.push_back(options.engine_url);
    args.push_back("--sample-rate");
    args.push_back(std::to_string(options.sample_rate_hz));
    args.push_back("--channels");
    args.push_back(std::to_string(options.channel_count));
    args.push_back("--chunk-frames");
    args.push_back(std::to_string(
        std::max<std::size_t>(
            1,
            options.sample_rate_hz /
                (options.source_kind ==
                     CaptureSourceKind::DirectShowDevice
                     ? 100
                     : 20))));
    args.push_back("--ffmpeg");
    args.push_back(options.ffmpeg_executable);
    args.push_back("--restart");

    if (session_target) {
        args.push_back("--preview-bins");
        args.push_back("0");
        args.push_back("--click-waveforms");
        args.push_back("--resume-from-engine");
    }
    if (options.pass_api_key_environment) {
        args.push_back("--api-key-env");
        args.push_back("PAMGUARD_CAPTURE_API_KEY");
    }
    return args;
}

} // namespace pamguard::service
