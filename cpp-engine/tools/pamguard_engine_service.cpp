#include <cstdint>
#include <cstring>
#include <atomic>
#include <array>
#include <any>
#include <cctype>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <httplib.h>
#include <json.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "pamguard/core/SessionManager.h"
#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/ModuleGraphJson.h"
#include "pamguard/core/ModuleRuntime.h"
#include "pamguard/core/OperatorNodes.h"
#include "pamguard/core/FftDetectorNodes.h"
#include "pamguard/core/DetectorNodes.h"
#include "pamguard/core/MatchedTemplateSettings.h"
#include "pamguard/project/BuiltInControlledUnits.h"
#include "pamguard/project/ControlledUnitJson.h"
#include "pamguard/project/ProjectAuthority.h"
#include "pamguard/project/ProjectAuthorityJson.h"
#include "pamguard/project/ProjectIdentity.h"
#include "pamguard/project/ProjectStore.h"
#include "pamguard/project/SoundRecorderControlledUnit.h"
#include "pamguard/io/WavReader.h"
#include "pamguard/detectors/CtSpectrumTemplates.h"
#include "pamguard/dsp/WindowFunction.h"
#include "pamguard/service/CaptureService.h"
#include "pamguard/service/TrackedClickEvents.h"

using json = nlohmann::json;

namespace {

constexpr std::size_t kMaxServiceChannelCount = 1024;
constexpr int kResultSchemaVersion = 32;

struct ResultJsonOptions {
    bool include_spectrogram = false;
    /** True when the session runs online echo detection, adding the echo flag. */
    bool echo_detection_running = false;
    bool include_spectrogram_complex = false;
    bool include_click_waveforms = false;
    bool include_click_spectra = false;
    std::size_t spectrogram_max_bins = 0;
    std::size_t spectrogram_bin_stride = 1;
    std::uint32_t sample_rate_hz = 0;
    std::size_t fft_length = 0;
    double speed_of_sound_mps = 0.0;
};

struct SessionRuntimeStats {
    std::int64_t created_unix_ms = 0;
    std::int64_t last_receive_unix_ms = 0;
    std::uint64_t chunks_received = 0;
    std::uint64_t frames_received = 0;
    std::uint64_t bytes_received = 0;
    std::uint64_t last_start_sample = 0;
    bool has_expected_start_sample = false;
    std::uint64_t expected_start_sample = 0;
    std::uint64_t sample_discontinuities = 0;
    std::int64_t last_sample_delta = 0;
    std::string last_sample_continuity = "none";
    std::int64_t last_time_ms = 0;
    std::uint64_t spectrogram_frames = 0;
    std::uint64_t clicks = 0;
    std::uint64_t click_features = 0;
    std::uint64_t click_classifications = 0;
    std::uint64_t click_trains = 0;
    std::uint64_t click_train_localisations = 0;
    std::uint64_t click_train_bearings = 0;
    std::uint64_t click_localisations = 0;
    std::uint64_t click_bearings = 0;
    std::uint64_t whistle_peaks = 0;
    std::uint64_t whistle_regions = 0;
    std::uint64_t process_calls = 0;
    double total_process_ms = 0.0;
    double last_process_ms = 0.0;
};

struct ArchiveQueryOptions {
    std::size_t limit = 100;
    bool has_start_sample_from = false;
    bool has_start_sample_to = false;
    bool has_overlap_start_sample = false;
    bool has_overlap_end_sample = false;
    bool has_cursor = false;
    std::uint64_t start_sample_from = 0;
    std::uint64_t start_sample_to = 0;
    std::uint64_t overlap_start_sample = 0;
    std::uint64_t overlap_end_sample = 0;
    std::uint64_t cursor = 0;
    std::string source_id_filter;
    std::string owner_id_filter;
    std::string tenant_id_filter;
};

struct ArchiveDetectionReadResult {
    json events = json::array();
    bool has_next_cursor = false;
    std::uint64_t next_cursor = 0;
    bool used_index = false;
};

struct ArchiveDetectionIndexEntry {
    std::uint64_t offset = 0;
    std::string type;
    std::uint64_t start_sample = 0;
    bool has_end_sample = false;
    std::uint64_t end_sample = 0;
};

std::int64_t current_unix_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

SessionRuntimeStats make_runtime_stats() {
    SessionRuntimeStats stats;
    stats.created_unix_ms = current_unix_ms();
    return stats;
}

std::int64_t non_negative_elapsed_ms(std::int64_t now_unix_ms, std::int64_t then_unix_ms) {
    if (then_unix_ms <= 0 || now_unix_ms <= then_unix_ms) {
        return 0;
    }
    return now_unix_ms - then_unix_ms;
}

std::uint64_t total_detector_outputs(const SessionRuntimeStats& stats) {
    return stats.spectrogram_frames
        + stats.clicks
        + stats.click_features
        + stats.click_classifications
        + stats.click_trains
        + stats.click_train_localisations
        + stats.click_train_bearings
        + stats.click_localisations
        + stats.click_bearings
        + stats.whistle_peaks
        + stats.whistle_regions;
}

json session_operational_status_to_json(const SessionRuntimeStats& stats, std::int64_t now_unix_ms) {
    const bool has_received_audio = stats.last_receive_unix_ms > 0;
    const double mean_process_ms = stats.process_calls == 0
        ? 0.0
        : stats.total_process_ms / static_cast<double>(stats.process_calls);
    return {
        {"activityState", has_received_audio ? "audio-received" : "awaiting-audio"},
        {"hasReceivedAudio", has_received_audio},
        {"ageMs", non_negative_elapsed_ms(now_unix_ms, stats.created_unix_ms)},
        {"idleMs", has_received_audio ? json(non_negative_elapsed_ms(now_unix_ms, stats.last_receive_unix_ms)) : json(nullptr)},
        {"sampleTimelineOk", stats.sample_discontinuities == 0},
        {"sampleDiscontinuities", stats.sample_discontinuities},
        {"lastSampleContinuity", stats.last_sample_continuity},
        {"lastSampleDelta", stats.last_sample_delta},
        {"nextExpectedStartSample", stats.expected_start_sample},
        {"chunksReceived", stats.chunks_received},
        {"framesReceived", stats.frames_received},
        {"totalDetectorOutputs", total_detector_outputs(stats)},
        {"processCalls", stats.process_calls},
        {"meanProcessMs", mean_process_ms},
        {"lastProcessMs", stats.last_process_ms},
    };
}

double read_float_le(const unsigned char* bytes) {
    std::uint32_t raw = 0;
    raw |= static_cast<std::uint32_t>(bytes[0]);
    raw |= static_cast<std::uint32_t>(bytes[1]) << 8;
    raw |= static_cast<std::uint32_t>(bytes[2]) << 16;
    raw |= static_cast<std::uint32_t>(bytes[3]) << 24;
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(raw));
    std::memcpy(&value, &raw, sizeof(value));
    return static_cast<double>(value);
}

std::uint32_t channel_bitmap(std::size_t channel_count) {
    if (channel_count == 0 || channel_count > 32) {
        throw std::invalid_argument("channelCount must be in the range 1..32");
    }
    if (channel_count == 32) {
        return 0xFFFFFFFFu;
    }
    return (1u << channel_count) - 1u;
}

bool is_power_of_two_size(std::size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

std::size_t bitmap_bit_count(std::uint32_t bitmap) {
    std::size_t count = 0;
    for (std::size_t bit = 0; bit < 32; ++bit) {
        if ((bitmap & (1u << bit)) != 0u) {
            ++count;
        }
    }
    return count;
}

std::vector<std::size_t> channels_from_bitmap(std::uint32_t bitmap, std::size_t channel_count) {
    std::vector<std::size_t> channels;
    const auto limit = std::min<std::size_t>(channel_count, 32);
    for (std::size_t channel = 0; channel < limit; ++channel) {
        if ((bitmap & (1u << channel)) != 0u) {
            channels.push_back(channel);
        }
    }
    return channels;
}

json click_localisation_readiness_to_json(const pamguard::core::AnalysisConfig& config) {
    const bool enabled = config.detector.click_detector_enabled && config.detector.click_localisation_enabled;
    const auto click_channels = channels_from_bitmap(config.detector.click.channel_bitmap, config.channel_count);
    std::vector<std::size_t> hydrophone_channels;
    hydrophone_channels.reserve(config.array.hydrophones.size());
    for (const auto& hydrophone : config.array.hydrophones) {
        hydrophone_channels.push_back(hydrophone.channel);
    }
    std::sort(hydrophone_channels.begin(), hydrophone_channels.end());

    std::vector<std::size_t> matched_channels;
    std::vector<std::size_t> missing_channels;
    for (const auto channel : click_channels) {
        if (std::binary_search(hydrophone_channels.begin(), hydrophone_channels.end(), channel)) {
            matched_channels.push_back(channel);
        }
        else {
            missing_channels.push_back(channel);
        }
    }

    std::string mode = "disabled";
    if (enabled) {
        if (click_channels.size() < 2) {
            mode = "invalid-click-channel-count";
        }
        else if (missing_channels.empty() && matched_channels.size() >= 2) {
            mode = "geometry-constrained";
        }
        else if (matched_channels.size() >= 2) {
            mode = "partial-geometry";
        }
        else {
            mode = "delay-only-unconstrained";
        }
    }

    const bool geometry_complete = enabled && click_channels.size() >= 2 && missing_channels.empty() && matched_channels.size() >= 2;
    return {
        {"enabled", enabled},
        {"mode", mode},
        {"geometryComplete", geometry_complete},
        {"bearingEnabled", enabled && matched_channels.size() >= 2},
        {"delayLimitMode", geometry_complete ? "geometry-constrained" : "unconstrained"},
        {"clickChannels", click_channels},
        {"hydrophoneChannels", hydrophone_channels},
        {"matchedClickHydrophoneChannels", matched_channels},
        {"missingClickHydrophoneChannels", missing_channels},
    };
}

std::int64_t saturated_sample_delta(std::uint64_t start_sample, std::uint64_t expected_start_sample) {
    constexpr auto max_delta = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (start_sample >= expected_start_sample) {
        const auto delta = start_sample - expected_start_sample;
        return delta > max_delta ? std::numeric_limits<std::int64_t>::max() : static_cast<std::int64_t>(delta);
    }
    const auto delta = expected_start_sample - start_sample;
    return delta > max_delta ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(delta);
}

void validate_base_config(const pamguard::core::AnalysisConfig& config) {
    if (config.session_id.empty()) {
        throw std::invalid_argument("sessionId must not be empty");
    }
    for (const unsigned char ch : config.session_id) {
        if (!(std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.')) {
            throw std::invalid_argument("sessionId may only contain letters, numbers, '-', '_', and '.'");
        }
    }
    if (config.sample_rate_hz == 0) {
        throw std::invalid_argument("sampleRateHz must be positive");
    }
    if (config.channel_count == 0 || config.channel_count > kMaxServiceChannelCount) {
        throw std::invalid_argument("channelCount must be in the range 1..1024");
    }
}

void validate_channel_list(const std::vector<std::size_t>& channels, std::size_t channel_count, std::string_view field_name) {
    if (channels.empty()) {
        throw std::invalid_argument(std::string(field_name) + " must contain at least one channel");
    }
    std::vector<bool> seen(channel_count, false);
    for (const auto channel : channels) {
        if (channel >= channel_count) {
            throw std::invalid_argument(std::string(field_name) + " contains channel outside channelCount");
        }
        if (seen[channel]) {
            throw std::invalid_argument(std::string(field_name) + " must not contain duplicate channels");
        }
        seen[channel] = true;
    }
}

void validate_click_bitmap(std::uint32_t bitmap, std::size_t channel_count, std::string_view field_name) {
    const auto allowed = channel_bitmap(channel_count);
    if (bitmap == 0) {
        throw std::invalid_argument(std::string(field_name) + " must include at least one channel");
    }
    if ((bitmap & ~allowed) != 0u) {
        throw std::invalid_argument(std::string(field_name) + " contains channel bits outside channelCount");
    }
}

void validate_percentage(double value, std::string_view field_name) {
    if (!std::isfinite(value) || value < 0.0 || value > 100.0) {
        throw std::invalid_argument(std::string(field_name) + " must be in the range 0..100");
    }
}

void validate_finite(double value, std::string_view field_name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(field_name) + " must be finite");
    }
}

void validate_ordered_range(const pamguard::detectors::FrequencyRange& range, std::string_view field_name) {
    validate_finite(range.low_hz, std::string(field_name) + ".lowHz");
    validate_finite(range.high_hz, std::string(field_name) + ".highHz");
    if (range.high_hz < range.low_hz) {
        throw std::invalid_argument(std::string(field_name) + " high value must be greater than or equal to low value");
    }
}

void validate_nonnegative_range(const pamguard::detectors::FrequencyRange& range, std::string_view field_name) {
    validate_ordered_range(range, field_name);
    if (range.low_hz < 0.0) {
        throw std::invalid_argument(std::string(field_name) + " must not be negative");
    }
}

bool parse_bool_param(const httplib::Request& req, const char* name, bool default_value = false) {
    if (!req.has_param(name)) {
        return default_value;
    }
    const auto value = req.get_param_value(name);
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

std::uint64_t parse_uint64_param(const httplib::Request& req, const char* name, std::uint64_t default_value = 0) {
    if (!req.has_param(name)) {
        return default_value;
    }
    const auto value = req.get_param_value(name);
    if (!value.empty() && value.front() == '-') {
        throw std::invalid_argument(std::string(name) + " must be non-negative");
    }
    return static_cast<std::uint64_t>(std::stoull(value));
}

std::vector<double> spectrogram_magnitude_squared(const pamguard::dsp::ComplexSpectrum& bins) {
    if (bins.size() < 2) {
        return {};
    }
    const auto fft_length = (bins.size() - 1) * 2;
    std::vector<double> magsq(fft_length / 2, 0.0);
    magsq[0] = bins[0].real() * bins[0].real() + bins[fft_length / 2].real() * bins[fft_length / 2].real();
    for (std::size_t i = 1; i < magsq.size(); ++i) {
        magsq[i] = std::norm(bins[i]);
    }
    return magsq;
}

std::vector<double> sampled_bins(const std::vector<double>& bins, std::size_t stride, std::size_t max_bins) {
    stride = std::max<std::size_t>(1, stride);
    std::vector<double> sampled;
    const auto limit = max_bins == 0 ? bins.size() : std::min(max_bins, bins.size());
    sampled.reserve((limit + stride - 1) / stride);
    for (std::size_t i = 0; i < bins.size() && sampled.size() < limit; i += stride) {
        sampled.push_back(bins[i]);
    }
    return sampled;
}

std::size_t max_sessions_from_environment() {
    const char* raw = std::getenv("PAMGUARD_MAX_SESSIONS");
    if (raw == nullptr || std::string(raw).empty()) {
        return 0;
    }
    return static_cast<std::size_t>(std::stoull(raw));
}

std::size_t max_pcm_body_bytes_from_environment() {
    const char* raw = std::getenv("PAMGUARD_MAX_PCM_BODY_BYTES");
    if (raw == nullptr || std::string(raw).empty()) {
        return 0;
    }
    return static_cast<std::size_t>(std::stoull(raw));
}

std::size_t max_archive_query_records_from_environment() {
    const char* raw = std::getenv("PAMGUARD_MAX_ARCHIVE_QUERY_RECORDS");
    if (raw == nullptr || std::string(raw).empty()) {
        return 0;
    }
    return static_cast<std::size_t>(std::stoull(raw));
}

std::size_t http_threads_from_environment() {
    const char* raw = std::getenv("PAMGUARD_HTTP_THREADS");
    if (raw == nullptr || std::string(raw).empty()) {
        return 0;
    }
    return static_cast<std::size_t>(std::stoull(raw));
}

bool bool_from_environment(const char* name, bool default_value = false) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || std::string(raw).empty()) {
        return default_value;
    }
    std::string value(raw);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }
    return default_value;
}

std::filesystem::path session_config_dir_from_environment() {
    const char* raw = std::getenv("PAMGUARD_SESSION_CONFIG_DIR");
    if (raw == nullptr || std::string(raw).empty()) {
        return {};
    }
    return std::filesystem::path(raw);
}

std::filesystem::path result_archive_dir_from_environment() {
    const char* raw = std::getenv("PAMGUARD_RESULT_ARCHIVE_DIR");
    if (raw == nullptr || std::string(raw).empty()) {
        return {};
    }
    return std::filesystem::path(raw);
}

std::filesystem::path ingest_status_file_from_environment() {
    const char* raw = std::getenv("PAMGUARD_INGEST_STATUS_FILE");
    if (raw == nullptr || std::string(raw).empty()) {
        return {};
    }
    return std::filesystem::path(raw);
}

std::filesystem::path audit_log_file_from_environment() {
    const char* raw = std::getenv("PAMGUARD_AUDIT_LOG_FILE");
    if (raw == nullptr || std::string(raw).empty()) {
        return {};
    }
    return std::filesystem::path(raw);
}

std::filesystem::path web_ui_file_from_environment() {
    const char* raw = std::getenv("PAMGUARD_WEB_UI_FILE");
    if (raw == nullptr || std::string(raw).empty()) {
        return {};
    }
    return std::filesystem::path(raw);
}

bool path_is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    auto root_part = root.begin();
    auto candidate_part = candidate.begin();
    for (; root_part != root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == candidate.end() ||
            *root_part != *candidate_part) {
            return false;
        }
    }
    return true;
}

std::filesystem::path canonical_directory(
    const std::filesystem::path& path,
    const char* setting_name) {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error) {
        throw std::invalid_argument(
            std::string(setting_name) +
            " must name an existing directory");
    }
    return canonical;
}

std::filesystem::path web_asset_root_from_environment(
    const std::filesystem::path& web_ui_file) {
    const char* configured = std::getenv("PAMGUARD_WEB_ASSET_DIR");
    if (configured != nullptr && !std::string(configured).empty()) {
        return canonical_directory(
            std::filesystem::path(configured),
            "PAMGUARD_WEB_ASSET_DIR");
    }

    if (web_ui_file.empty()) {
        return {};
    }

    std::error_code error;
    const auto canonical_ui =
        std::filesystem::canonical(web_ui_file, error);
    if (error ||
        !std::filesystem::is_regular_file(canonical_ui, error) ||
        error) {
        // Preserve the existing / and /index.html behaviour for a missing or
        // invalid PAMGUARD_WEB_UI_FILE. The HTML handler reports its normal
        // read error; an unvalidated parent is never used as an asset root.
        return {};
    }

    const auto web_root = canonical_ui.parent_path();
    const auto candidate = web_root / "assets";
    if (!std::filesystem::exists(candidate, error)) {
        return {};
    }
    if (error) {
        throw std::invalid_argument(
            "could not inspect the web UI assets directory");
    }

    const auto asset_root =
        canonical_directory(candidate, "web UI assets directory");
    if (asset_root == web_root ||
        !path_is_within(web_root, asset_root)) {
        throw std::invalid_argument(
            "web UI assets directory escapes the web UI directory");
    }
    return asset_root;
}

std::filesystem::path openapi_file_from_environment() {
    const char* raw = std::getenv("PAMGUARD_OPENAPI_FILE");
    if (raw == nullptr || std::string(raw).empty()) {
        return {};
    }
    return std::filesystem::path(raw);
}

std::filesystem::path module_graph_file_from_environment() {
    const char* raw = std::getenv("PAMGUARD_MODULE_GRAPH_FILE");
    if (raw == nullptr || std::string(raw).empty()) {
        return {};
    }
    return std::filesystem::path(raw);
}

std::filesystem::path workspace_file_from_environment() {
    const char* raw = std::getenv("PAMGUARD_WORKSPACE_FILE");
    if (raw == nullptr || std::string(raw).empty()) {
        return {};
    }
    return std::filesystem::path(raw);
}

std::filesystem::path project_dir_from_environment() {
    const char* raw = std::getenv("PAMGUARD_PROJECT_DIR");
    if (raw == nullptr || std::string(raw).empty()) {
        return std::filesystem::path("pamguard-projects");
    }
    return std::filesystem::path(raw);
}

struct SoundRecorderDeploymentContext {
    std::optional<std::filesystem::path> root;
    std::string readiness_error;

    [[nodiscard]] bool ready() const noexcept {
        return root.has_value();
    }
};

struct ActiveProjectNavigationTrack {
    std::string project_id;
    std::uint64_t working_revision = 0;
    std::deque<pamguard::service::TrackedClickNavigationSample>
        samples;
};

SoundRecorderDeploymentContext
sound_recorder_deployment_from_environment() {
    const char* raw = std::getenv("PAMGUARD_RECORDING_ROOT");
    if (raw == nullptr || std::string(raw).empty()) {
        return {
            std::nullopt,
            "PAMGUARD_RECORDING_ROOT is not configured",
        };
    }
    try {
        const auto candidate =
            std::filesystem::canonical(std::filesystem::path(raw));
        if (!std::filesystem::is_directory(candidate)) {
            return {
                std::nullopt,
                "PAMGUARD_RECORDING_ROOT is not an existing directory",
            };
        }
        const auto probe =
            candidate /
            (".pamguard-write-probe-" +
             pamguard::project::generate_uuid_v4());
        {
            std::ofstream output(
                probe,
                std::ios::binary | std::ios::out |
                    std::ios::trunc);
            if (!output) {
                return {
                    std::nullopt,
                    "PAMGUARD_RECORDING_ROOT is not writable",
                };
            }
            output.put('\0');
            output.flush();
            if (!output) {
                output.close();
                std::error_code ignored;
                std::filesystem::remove(probe, ignored);
                return {
                    std::nullopt,
                    "PAMGUARD_RECORDING_ROOT is not writable",
                };
            }
        }
        std::error_code remove_error;
        const bool removed =
            std::filesystem::remove(probe, remove_error);
        if (remove_error || !removed) {
            return {
                std::nullopt,
                "PAMGUARD_RECORDING_ROOT write probe could not be removed",
            };
        }
        return {candidate, {}};
    }
    catch (const std::exception&) {
        return {
            std::nullopt,
            "PAMGUARD_RECORDING_ROOT could not be validated",
        };
    }
}

bool uuid_path_component(std::string_view value) {
    if (value.size() != 36) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 ||
            index == 18 || index == 23) {
            if (value[index] != '-') {
                return false;
            }
            continue;
        }
        if (!std::isxdigit(
                static_cast<unsigned char>(value[index]))) {
            return false;
        }
    }
    return true;
}

std::string active_project_id_from_environment() {
    const char* raw = std::getenv("PAMGUARD_ACTIVE_PROJECT_ID");
    if (raw == nullptr) {
        return {};
    }
    return std::string(raw);
}

void persist_json_file(
    const std::filesystem::path& path,
    const json& document,
    const char* description) {
    if (path.empty()) {
        return;
    }
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    static std::atomic<std::uint64_t> next_temp_id{1};
    auto temporary = path;
    temporary += ".tmp." +
        std::to_string(next_temp_id.fetch_add(1));
    try {
        {
            std::ofstream output(
                temporary,
                std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error(
                    std::string("could not write ") + description);
            }
            output << document.dump(2);
            output.flush();
            if (!output) {
                throw std::runtime_error(
                    std::string("could not finish writing ") +
                    description);
            }
        }
#ifdef _WIN32
        if (!MoveFileExW(
                temporary.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error(
                std::string("could not atomically replace ") +
                description + " (Windows error " +
                std::to_string(GetLastError()) + ")");
        }
#else
        std::filesystem::rename(temporary, path);
#endif
    }
    catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

bool valid_workspace_id(const std::string& id) {
    if (id.empty() || id.size() > 128) {
        return false;
    }
    return std::all_of(
        id.begin(),
        id.end(),
        [](unsigned char character) {
            return std::isalnum(character) ||
                character == '_' ||
                character == '-' ||
                character == '.';
        });
}

void validate_workspace_layout(const json& layout) {
    if (!layout.is_object() ||
        layout.value("schemaVersion", 0) != 1 ||
        !layout.contains("displays") ||
        !layout.at("displays").is_array()) {
        throw std::invalid_argument(
            "Workspace must be a schemaVersion 1 object with a displays array");
    }
    if (layout.at("displays").size() > 128) {
        throw std::invalid_argument(
            "Workspace cannot contain more than 128 displays");
    }
    if (layout.contains("name") &&
        (!layout.at("name").is_string() ||
         layout.at("name").get_ref<const std::string&>().size() > 256)) {
        throw std::invalid_argument(
            "Workspace name must be a string of at most 256 characters");
    }
    if (layout.contains("arrangement") &&
        (!layout.at("arrangement").is_string() ||
         (layout.at("arrangement") != "grid" &&
          layout.at("arrangement") != "tabs"))) {
        throw std::invalid_argument(
            "Workspace arrangement must be grid or tabs");
    }
    if (layout.contains("synchronizedTime") &&
        !layout.at("synchronizedTime").is_boolean()) {
        throw std::invalid_argument(
            "Workspace synchronizedTime must be boolean");
    }
    const std::unordered_set<std::string> display_types = {
        "spectrogram",
        "events",
        "waveform",
        "level",
        "timeplot",
        "status",
        "datamap",
    };
    std::unordered_set<std::string> display_ids;
    for (const auto& display : layout.at("displays")) {
        if (!display.is_object() ||
            !display.contains("id") ||
            !display.at("id").is_string() ||
            !display.contains("type") ||
            !display.at("type").is_string()) {
            throw std::invalid_argument(
                "Every workspace display requires string id and type");
        }
        const auto& id =
            display.at("id").get_ref<const std::string&>();
        const auto& type =
            display.at("type").get_ref<const std::string&>();
        if (id.empty() || id.size() > 128 ||
            !display_ids.insert(id).second) {
            throw std::invalid_argument(
                "Workspace display IDs must be unique and 1 to 128 characters");
        }
        if (!display_types.contains(type)) {
            throw std::invalid_argument(
                "Workspace contains an unknown display type: " +
                type);
        }
        if (display.contains("name") &&
            (!display.at("name").is_string() ||
             display.at("name")
                     .get_ref<const std::string&>()
                     .size() > 256)) {
            throw std::invalid_argument(
                "Workspace display names must be strings of at most 256 characters");
        }
        if (display.contains("sourceBlockId") &&
            (!display.at("sourceBlockId").is_string() ||
             display.at("sourceBlockId")
                     .get_ref<const std::string&>()
                     .size() > 512)) {
            throw std::invalid_argument(
                "Workspace display sourceBlockId is invalid");
        }
    }
    if (layout.contains("audio") &&
        !layout.at("audio").is_object()) {
        throw std::invalid_argument(
            "Workspace audio settings must be an object");
    }
}

void persist_module_graph(
    const std::filesystem::path& graph_file,
    const pamguard::core::ModuleGraphDocument& document) {
    persist_json_file(
        graph_file,
        json::parse(
            pamguard::core::module_graph_to_json(document, false)),
        "module graph file");
}

json graph_issues_to_json(const std::vector<pamguard::core::GraphIssue>& issues) {
    auto result = json::array();
    for (const auto& issue : issues) {
        result.push_back({
            {"code", issue.code},
            {"message", issue.message},
            {"moduleId", issue.module_id.empty() ? json(nullptr) : json(issue.module_id)},
            {"connectionId", issue.connection_id.empty() ? json(nullptr) : json(issue.connection_id)},
        });
    }
    return result;
}

json module_type_to_json(const pamguard::core::ModuleTypeDescriptor& type) {
    auto ports = json::array();
    auto dependencies = json::array();
    json settings_schema;
    json default_settings;
    try {
        settings_schema = json::parse(type.settings_schema_json);
    }
    catch (const std::exception& error) {
        throw std::runtime_error(
            "Module type '" + type.id +
            "' has an invalid settings schema: " + error.what());
    }
    try {
        default_settings = json::parse(type.default_settings_json);
    }
    catch (const std::exception& error) {
        throw std::runtime_error(
            "Module type '" + type.id +
            "' has invalid default settings: " + error.what());
    }
    for (const auto& port : type.ports) {
        ports.push_back({
            {"id", port.id},
            {"name", port.name},
            {"direction", port.direction == pamguard::core::PortDirection::Input ? "input" : "output"},
            {"dataType", port.data_type},
            {"required", port.required},
            {"acceptsMultiple", port.accepts_multiple},
            {"capabilities", port.capabilities},
        });
        if (port.direction == pamguard::core::PortDirection::Input &&
            port.required) {
            dependencies.push_back({
                {"portId", port.id},
                {"dataType", port.data_type},
                {"capabilities", port.capabilities},
            });
        }
    }
    return {
        {"id", type.id},
        {"name", type.name},
        {"category", type.category},
        {"description", type.description},
        {"minimumInstances", type.minimum_instances},
        {"maximumInstances", type.maximum_instances
            ? json(*type.maximum_instances)
            : json(nullptr)},
        {"ports", std::move(ports)},
        {"dependencies", std::move(dependencies)},
        {"runModes", type.run_modes},
        {"providedDisplayTypes", type.provided_display_types},
        {"implementationStatus", type.implementation_status},
        {"parityStatus", type.parity_status},
        {"settingsSchema", std::move(settings_schema)},
        {"defaultSettings", std::move(default_settings)},
    };
}

json data_block_to_json(const pamguard::core::DataBlockDescriptor& block) {
    return {
        {"id", block.id},
        {"name", block.name},
        {"producerModuleId", block.producer_module_id},
        {"producerPortId", block.producer_port_id},
        {"dataType", block.data_type},
        {"schemaVersion", block.schema_version},
        {"sampleRateHz", block.sample_rate_hz},
        {"channelBitmap", block.channel_bitmap},
        {"sequenceBitmap", block.sequence_bitmap},
        {"minimumFrequencyHz", block.minimum_frequency_hz
            ? json(*block.minimum_frequency_hz)
            : json(nullptr)},
        {"maximumFrequencyHz", block.maximum_frequency_hz
            ? json(*block.maximum_frequency_hz)
            : json(nullptr)},
        {"fftLength", block.fft_length
            ? json(*block.fft_length)
            : json(nullptr)},
        {"fftHop", block.fft_hop
            ? json(*block.fft_hop)
            : json(nullptr)},
        {"calibrationDbOffsetByChannel",
         block.calibration_db_offset_by_channel},
        {"voltsPeakToPeak", block.volts_peak_to_peak
            ? json(*block.volts_peak_to_peak)
            : json(nullptr)},
        {"capabilities", block.capabilities},
        {"historyCapacity", block.history_capacity},
        {"clockDomainId", block.clock_domain_id},
        {"retentionPolicy", block.retention_policy},
        {"persistenceProviders", block.persistence_providers},
        {"exportProviders", block.export_providers},
    };
}

json data_block_stats_to_json(const pamguard::core::DataBlockStats& stats) {
    return {
        {"published", stats.published},
        {"delivered", stats.delivered},
        {"dropped", stats.dropped},
        {"observerErrors", stats.observer_errors},
        {"subscriberCount", stats.subscriber_count},
        {"historySize", stats.history_size},
        {"queuedUnits", stats.queued_units},
        {"maximumQueuedUnits", stats.maximum_queued_units},
    };
}

const char* module_state_name(pamguard::core::ModuleState state) {
    switch (state) {
    case pamguard::core::ModuleState::Created:
        return "created";
    case pamguard::core::ModuleState::Prepared:
        return "prepared";
    case pamguard::core::ModuleState::Running:
        return "running";
    case pamguard::core::ModuleState::Stopped:
        return "stopped";
    case pamguard::core::ModuleState::Error:
        return "error";
    }
    return "unknown";
}

json data_unit_to_json(const pamguard::core::DataUnit& unit) {
    json result = {
        {"typeId", unit.metadata.type_id},
        {"schemaVersion", unit.metadata.schema_version},
        {"sourceBlockId", unit.metadata.source_block_id},
        {"uid", unit.metadata.uid},
        {"sequence", unit.metadata.sequence},
        {"timeMs", unit.metadata.time_unix_ms},
        {"startSample", unit.metadata.start_sample},
        {"durationSamples", unit.metadata.duration_samples},
        {"channelBitmap", unit.metadata.channel_bitmap},
        {"sequenceBitmap", unit.metadata.sequence_bitmap},
        {"clockDomainId", unit.metadata.clock_domain_id},
        {"discontinuity", unit.metadata.discontinuity},
    };
    if (const auto* audio = std::any_cast<pamguard::core::AudioChunk>(&unit.payload)) {
        result["payload"] = {
            {"sampleRateHz", audio->sample_rate_hz},
            {"channelCount", audio->channel_count},
            {"interleavedPcm", audio->interleaved_pcm},
        };
    }
    else if (const auto* frame =
                 std::any_cast<pamguard::dsp::SpectrogramFrame>(&unit.payload)) {
        json magnitude_squared = json::array();
        for (const auto& bin : frame->bins) {
            magnitude_squared.push_back(std::norm(bin));
        }
        result["payload"] = {
            {"channel", frame->channel},
            {"fftSlice", frame->fft_slice},
            {"magnitudeSquared", std::move(magnitude_squared)},
        };
    }
    else if (const auto* click =
                 std::any_cast<pamguard::detectors::ClickDetectionResult>(
                     &unit.payload)) {
        auto matched_template_annotations = json::array();
        for (const auto& annotation :
             click->matched_template_annotations) {
            auto best_results = json::array();
            for (const auto& item :
                 annotation.best_results) {
                best_results.push_back({
                    {"threshold", item.threshold},
                    {
                        "matchCorrelation",
                        item.match_correlation,
                    },
                    {
                        "rejectCorrelation",
                        item.reject_correlation,
                    },
                });
            }
            matched_template_annotations.push_back({
                {
                    "classifierInstanceId",
                    annotation.classifier_instance_id,
                },
                {"clickType", annotation.click_type},
                {"classified", annotation.classified},
                {"bestResults", std::move(best_results)},
            });
        }
        result["payload"] = {
            {"channelBitmap", click->channel_bitmap},
            {"triggerBitmap", click->trigger_bitmap},
            {"startSample", click->start_sample},
            {"durationSamples", click->duration_samples},
            {"timeMs", click->time_unix_ms},
            {"signalExcessDb", click->signal_excess_db},
            {"channels", click->channels},
            {"waveform", click->waveform},
            {"clickType", click->click_type},
            {"classifiersPassed", click->classifiers_passed},
            {"delaysInSamples", click->delays_in_samples},
            {
                "orientation",
                click->orientation_declared
                    ? json({
                          {
                              "headingDegrees",
                              click->
                                  orientation_heading_degrees,
                          },
                          {
                              "pitchDegrees",
                              click->
                                  orientation_pitch_degrees,
                          },
                          {
                              "rollDegrees",
                              click->
                                  orientation_roll_degrees,
                          },
                      })
                    : json(nullptr),
            },
            {
                "navigationOriginMetres",
                click->navigation_origin_declared
                    ? json({
                          click->
                              navigation_origin_east_metres,
                          click->
                              navigation_origin_north_metres,
                          click->
                              navigation_origin_height_metres,
                      })
                    : json(nullptr),
            },
            {
                "navigationReferenceId",
                click->navigation_origin_declared
                    ? json(click->navigation_reference_id)
                    : json(nullptr),
            },
            {
                "earthBearingAmbiguitiesRadians",
                click->
                    earth_bearing_ambiguities_radians,
            },
            {
                "matchedTemplateAnnotations",
                std::move(matched_template_annotations),
            },
            {"echo", click->echo},
        };
        result["payload"]["bearingRadians"] =
            click->bearing_radians.has_value()
            ? json(*click->bearing_radians)
            : json(nullptr);
    }
    else if (const auto* noise =
                 std::any_cast<pamguard::detectors::ClickNoiseSampleResult>(
                     &unit.payload)) {
        result["payload"] = {
            {"channelBitmap", noise->channel_bitmap},
            {"startSample", noise->start_sample},
            {"durationSamples", noise->duration_samples},
            {"timeMs", noise->time_unix_ms},
            {"channels", noise->channels},
            {"waveform", noise->waveform},
        };
    }
    else if (const auto* background = std::any_cast<
                 pamguard::detectors::ClickTriggerBackgroundResult>(
                 &unit.payload)) {
        result["payload"] = {
            {"channelBitmap", background->channel_bitmap},
            {"timeMs", background->time_unix_ms},
            {"channels", background->channels},
            {"values", background->values},
        };
    }
    else if (const auto* trigger = std::any_cast<
                 pamguard::detectors::ClickTriggerFunctionResult>(
                 &unit.payload)) {
        result["payload"] = {
            {"channelBitmap", trigger->channel_bitmap},
            {"startSample", trigger->start_sample},
            {"timeMs", trigger->time_unix_ms},
            {"channels", trigger->channels},
            {"signalExcessDb", trigger->signal_excess_db},
            {"longFilterValues", trigger->long_filter_values},
        };
    }
    else if (const auto* feature =
                 std::any_cast<pamguard::detectors::ClickFeatureResult>(
                     &unit.payload)) {
        result["payload"] = {
            {"clickIndex", feature->click_index},
            {"clickStartSample", feature->click_start_sample},
            {"fftLength", feature->fft_length},
            {"clickLengthSeconds", feature->click_length_seconds},
            {"peakFrequencyHz", feature->peak_frequency_hz},
            {"peakWidthHz", feature->peak_width_hz},
            {"meanFrequencyHz", feature->mean_frequency_hz},
            {"bandEnergyDb", feature->band_energy_db},
            {"totalPowerSpectrum", feature->total_power_spectrum},
        };
    }
    else if (const auto* classification =
                 std::any_cast<
                     pamguard::detectors::ClickClassificationResult>(
                     &unit.payload)) {
        result["payload"] = {
            {"clickIndex", classification->click_index},
            {"clickStartSample", classification->click_start_sample},
            {"clickType", classification->click_type},
            {"discard", classification->discard},
            {"classifiersPassed", classification->classifiers_passed},
        };
    }
    else if (const auto* matched =
                 std::any_cast<
                     pamguard::core::MatchedTemplateClassificationResult>(
                     &unit.payload)) {
        auto results = json::array();
        for (const auto& item :
             matched->classification.best_results) {
            results.push_back({
                {"threshold", item.threshold},
                {"matchCorrelation", item.match_corr},
                {"rejectCorrelation", item.reject_corr},
            });
        }
        result["payload"] = {
            {"clickStartSample", matched->click_start_sample},
            {
                "classifierInstanceId",
                matched->classifier_instance_id,
            },
            {"clickType", matched->click_type},
            {"classified", matched->classification.classified},
            {"bestResults", std::move(results)},
        };
    }
    else if (const auto* period =
                 std::any_cast<pamguard::detectors::FftNoisePeriod>(
                     &unit.payload)) {
        json bands = json::array();
        for (const auto& band : period->bands) {
            bands.push_back({
                {"mean", band.mean},
                {"median", band.median},
                {"low95", band.low_95},
                {"high95", band.high_95},
                {"minimum", band.minimum},
                {"maximum", band.maximum},
            });
        }
        result["payload"] = {
            {"channel", period->channel},
            {"endSample", period->end_sample},
            {"timeMs", period->time_unix_ms},
            {"nMeasurements", period->n_measurements},
            {"bands", std::move(bands)},
        };
    }
    else if (const auto* measurement =
                 std::any_cast<pamguard::core::NoiseBandMeasurement>(
                     &unit.payload)) {
        auto bands = json::array();
        for (const auto& band : measurement->bands) {
            bands.push_back({
                {"centreHz", band.centre_hz},
                {"lowEdgeHz", band.lo_edge_hz},
                {"highEdgeHz", band.hi_edge_hz},
            });
        }
        result["payload"] = {
            {"channel", measurement->channel},
            {"bands", std::move(bands)},
            {"rmsDb", measurement->rms_db},
            {"peakDb", measurement->peak_db},
            {"endSample", measurement->end_sample},
            {"timeMs", measurement->time_unix_ms},
        };
    }
    else if (const auto* ltsa =
                 std::any_cast<pamguard::core::LtsaChannelInterval>(
                     &unit.payload)) {
        result["payload"] = {
            {"channel", ltsa->channel},
            {"startTimeMs", ltsa->interval.start_time_ms},
            {"endTimeMs", ltsa->interval.end_time_ms},
            {"nFft", ltsa->interval.n_fft},
            {"startSample", ltsa->interval.start_sample},
            {"durationSamples", ltsa->interval.duration_samples},
            {"magnitude", ltsa->interval.magnitude},
        };
    }
    else if (const auto* sample =
                 std::any_cast<pamguard::detectors::IshmaelDetSample>(
                     &unit.payload)) {
        result["payload"] = {
            {"detectionValue", sample->det_value},
            {"noiseFloor", sample->noise_floor},
            {"rawValue", sample->raw_value},
            {"hasNoiseFloor", sample->has_noise_floor},
        };
    }
    else if (const auto* detection =
                 std::any_cast<pamguard::detectors::IshmaelDetection>(
                     &unit.payload)) {
        result["payload"] = {
            {"channel", detection->channel},
            {"startSample", detection->start_sample},
            {"durationSamples", detection->duration_samples},
            {"peakTimeSample", detection->peak_time_sample},
            {"peakHeight", detection->peak_height},
            {"startTimeMs", detection->start_time_ms},
            {"lowFrequencyHz", detection->low_freq_hz},
            {"highFrequencyHz", detection->high_freq_hz},
        };
    }
    else if (const auto* peak =
                 std::any_cast<pamguard::detectors::WhistlePeak>(
                     &unit.payload)) {
        result["payload"] = {
            {"channel", peak->channel},
            {"startSample", peak->start_sample},
            {"timeMs", peak->time_ms},
            {"sliceNumber", peak->slice_number},
            {"minimumFrequencyBin", peak->min_freq},
            {"peakFrequencyBin", peak->peak_freq},
            {"maximumFrequencyBin", peak->max_freq},
            {"maximumAmplitude", peak->max_amp},
            {"signal", peak->signal},
            {"noise", peak->noise},
            {"ok", peak->ok},
        };
    }
    else if (const auto* contour =
                 std::any_cast<
                     pamguard::detectors::ConnectedRegionResult>(
                     &unit.payload)) {
        result["payload"] = {
            {"channel", contour->channel},
            {"regionNumber", contour->region_number},
            {"firstSlice", contour->first_slice},
            {"startSample", contour->start_sample},
            {"lastStartSample", contour->last_start_sample},
            {"timeMs", contour->time_ms},
            {"durationSamples", contour->duration_samples},
            {"durationSeconds", contour->duration_seconds},
            {"timeSpanSamples", contour->time_span_samples},
            {"timeSpanSeconds", contour->time_span_seconds},
            {"totalPixels", contour->total_pixels},
            {"minimumFrequencyBin", contour->min_frequency_bin},
            {"maximumFrequencyBin", contour->max_frequency_bin},
            {"meanPeakBin", contour->mean_peak_bin},
            {"peakFrequencyBins", contour->peak_freqs_bins},
            {"timeBins", contour->times_bins},
        };
    }
    else if (const auto* localisation =
                 std::any_cast<pamguard::core::ClickLocalisationResult>(
                     &unit.payload)) {
        auto delays = json::array();
        for (const auto& delay : localisation->delays) {
            delays.push_back({
                {"pairIndex", delay.pair_index},
                {"channelA", delay.channel_a},
                {"channelB", delay.channel_b},
                {"audioChannelA", delay.audio_channel_a},
                {"audioChannelB", delay.audio_channel_b},
                {"geometryConstrained", delay.geometry_constrained},
                {"maxDelaySamples", delay.max_delay_samples},
                {"hydrophoneDistanceM", delay.hydrophone_distance_m},
                {"delaySamples", delay.delay.delay_samples},
                {"delayScore", delay.delay.delay_score},
            });
        }
        result["payload"] = {
            {"clickIndex", localisation->click_index},
            {"clickStartSample", localisation->click_start_sample},
            {"delays", std::move(delays)},
            {"arrayShape",
             static_cast<int>(localisation->array_shape)},
            {"bearingLocaliser",
             static_cast<int>(localisation->bearing_localiser)},
            {"lsqBearingValid", localisation->lsq_bearing.valid},
            {"gridBearingValid", localisation->grid_bearing.valid},
        };
    }
    else if (const auto* bearing =
                 std::any_cast<pamguard::core::ClickBearingResult>(
                     &unit.payload)) {
        result["payload"] = {
            {"clickIndex", bearing->click_index},
            {"clickStartSample", bearing->click_start_sample},
            {"valid", bearing->bearing.valid},
            {"unitX", bearing->bearing.unit_x},
            {"unitY", bearing->bearing.unit_y},
            {"unitZ", bearing->bearing.unit_z},
            {"azimuthDegrees", bearing->bearing.azimuth_degrees},
            {"elevationDegrees", bearing->bearing.elevation_degrees},
            {"residualRmsSeconds",
             bearing->bearing.residual_rms_seconds},
            {"usedPairs", bearing->bearing.used_pairs},
        };
    }
    else if (const auto* train =
                 std::any_cast<
                     pamguard::core::GraphMhtClickTrainResult>(
                     &unit.payload)) {
        result["payload"] = {
            {"trainId", train->train_id},
            {"channelBitmap", train->channel_bitmap},
            {"chi2", train->chi2},
            {"clickCount", train->click_count},
            {"firstStartSample", train->first_start_sample},
            {"lastStartSample", train->last_start_sample},
            {"clickStartSamples", train->click_start_samples},
            {"clickTimeMs", train->click_time_ms},
            {"classified", train->classified},
            {"junkTrain", train->junk_train},
            {"speciesId", train->species_id},
            {"classifierSpeciesIds",
             train->classifier_species_ids},
            {"templateCorrelation", train->template_correlation},
        };
    }
    else if (const auto* classification =
                 std::any_cast<
                     pamguard::core::
                         GraphClickTrainClassificationResult>(
                     &unit.payload)) {
        result["payload"] = {
            {"trainId", classification->train_id},
            {"junkTrain", classification->junk_train},
            {"speciesId", classification->species_id},
            {"classifierSpeciesIds",
             classification->classifier_species_ids},
            {"templateCorrelation",
             classification->template_correlation},
        };
    }
    else if (const auto* train =
                 std::any_cast<pamguard::detectors::ClickTrainSummary>(
                     &unit.payload)) {
        result["payload"] = {
            {"trainId", train->train_id},
            {"channelBitmap", train->channel_bitmap},
            {"firstStartSample", train->first_start_sample},
            {"lastStartSample", train->last_start_sample},
            {"firstTimeMs", train->first_time_ms},
            {"lastTimeMs", train->last_time_ms},
            {"clickStartSamples", train->click_start_samples},
            {"clickTimeMs", train->click_time_ms},
            {"clickCount", train->click_count},
            {"durationSamples", train->duration_samples},
            {"durationSeconds", train->duration_seconds},
            {"timeSpanSeconds", train->time_span_seconds},
            {"lastIciSeconds", train->last_ici_seconds},
            {"minIciSeconds", train->min_ici_seconds},
            {"maxIciSeconds", train->max_ici_seconds},
            {"meanIciSeconds", train->mean_ici_seconds},
            {"medianIciSeconds", train->median_ici_seconds},
            {"stdIciSeconds", train->std_ici_seconds},
            {"iciCv", train->ici_cv},
            {"clickRateHz", train->click_rate_hz},
            {"completed", train->completed},
        };
    }
    else if (const auto* level = std::any_cast<
                 pamguard::core::GraphLevelMeasurement>(
                     &unit.payload)) {
        result["payload"] = {
            {"rmsDbfs", level->rms_dbfs},
            {"peakDbfs", level->peak_dbfs},
            {"measuredFrames", level->measured_frames},
        };
    }
    else if (const auto* recording = std::any_cast<
                 pamguard::core::GraphRecordingEvent>(
                     &unit.payload)) {
        result["payload"] = {
            {"path", recording->path},
            {"state", recording->state},
            {"startSample", recording->start_sample},
            {"frameCount", recording->frame_count},
            {"sampleRateHz", recording->sample_rate_hz},
            {"channelCount", recording->channel_count},
        };
    }
    else if (const auto* alarm = std::any_cast<
                 pamguard::core::GraphAlarmState>(
                     &unit.payload)) {
        result["payload"] = {
            {"active", alarm->active},
            {"eventCount", alarm->event_count},
            {"threshold", alarm->threshold},
            {"windowSeconds", alarm->window_seconds},
            {"message", alarm->message},
        };
    }
    else if (const auto* event = std::any_cast<
                 pamguard::core::GraphOperatorEvent>(
                     &unit.payload)) {
        result["payload"] = {
            {"category", event->category},
            {"label", event->label},
            {"notes", event->notes},
            {"value", event->value},
        };
    }
    else if (const auto* storage = std::any_cast<
                 pamguard::core::GraphStorageHealth>(
                     &unit.payload)) {
        result["payload"] = {
            {"path", storage->path},
            {"available", storage->available},
            {"capacityBytes", storage->capacity_bytes},
            {"freeBytes", storage->free_bytes},
            {"availableBytes", storage->available_bytes},
            {"availablePercent", storage->available_percent},
            {"status", storage->status},
        };
    }
    else if (const auto* clip = std::any_cast<
                 pamguard::core::GraphAudioClip>(
                     &unit.payload)) {
        result["payload"] = {
            {"triggerUid", clip->trigger_uid},
            {"triggerTimeMs", clip->trigger_time_unix_ms},
            {"triggerStartSample", clip->trigger_start_sample},
            {"clipStartSample", clip->clip_start_sample},
            {"sampleRateHz", clip->sample_rate_hz},
            {"channelCount", clip->channel_count},
            {"incomplete", clip->incomplete},
            {"interleavedPcm", clip->interleaved_pcm},
        };
    }
    else {
        result["payload"] = nullptr;
    }
    return result;
}

std::size_t channel_count_from_bitmap(std::uint32_t bitmap) {
    std::size_t count = 0;
    for (std::size_t channel = 0; channel < 32; ++channel) {
        if ((bitmap & (std::uint32_t{1} << channel)) != 0) {
            count = channel + 1;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Live sound-card capture management (opt-in via PAMGUARD_CAPTURE_ENABLED).
//
// The service never runs shell commands: device listing and capture both use
// CreateProcess with explicitly quoted argument strings, capture device names
// must exactly match an enumerated device, and the spawned ingest bridge is
// held in a Windows Job Object so stopping a capture kills the whole ffmpeg
// process tree. Windows/DirectShow only; other platforms report 501.
// ---------------------------------------------------------------------------

std::string ffmpeg_path_from_environment() {
    const char* raw = std::getenv("PAMGUARD_FFMPEG_PATH");
    if (raw == nullptr || std::string(raw).empty()) {
        return "ffmpeg";
    }
    return raw;
}

std::string ingest_exe_path(const char* argv0) {
    const char* raw = std::getenv("PAMGUARD_INGEST_EXE");
    if (raw != nullptr && !std::string(raw).empty()) {
        return raw;
    }
    // Default: next to the service executable.
    std::filesystem::path self(argv0 == nullptr ? "" : argv0);
    auto candidate = self.parent_path() / "ffmpeg_stream_ingest.exe";
    return candidate.string();
}

#ifdef _WIN32

/** Standard Windows argument quoting (backslash-doubling before quotes). */
std::string quote_windows_arg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) {
        return arg;
    }
    std::string quoted = "\"";
    std::size_t backslashes = 0;
    for (const char c : arg) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, '\\');
        backslashes = 0;
        quoted.push_back(c);
    }
    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

std::string join_windows_command(const std::vector<std::string>& args) {
    std::string command;
    for (const auto& arg : args) {
        if (!command.empty()) {
            command.push_back(' ');
        }
        command += quote_windows_arg(arg);
    }
    return command;
}

/** Runs a command and captures stdout+stderr; returns false when it could
 * not be started at all (a non-zero exit still returns true with output). */
bool run_command_capture(const std::vector<std::string>& args, std::string& output) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
        return false;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION process{};
    std::string command = join_windows_command(args);
    const bool started = CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != 0;
    CloseHandle(write_pipe);
    if (!started) {
        CloseHandle(read_pipe);
        return false;
    }
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        output.append(buffer, read);
    }
    CloseHandle(read_pipe);
    WaitForSingleObject(process.hProcess, 15000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

#endif // _WIN32

/** ffmpeg dshow device enumeration: lines like [dshow @ ...] "Name" (audio). */
std::vector<std::pair<std::string, std::string>> parse_dshow_devices(const std::string& listing) {
    std::vector<std::pair<std::string, std::string>> devices;
    std::istringstream stream(listing);
    std::string line;
    while (std::getline(stream, line)) {
        const auto first_quote = line.find('"');
        if (first_quote == std::string::npos) {
            continue;
        }
        const auto second_quote = line.find('"', first_quote + 1);
        if (second_quote == std::string::npos) {
            continue;
        }
        const auto suffix = line.substr(second_quote + 1);
        std::string type;
        if (suffix.find("(audio)") != std::string::npos) {
            type = "audio";
        }
        else if (suffix.find("(video)") != std::string::npos) {
            type = "video";
        }
        else {
            continue;
        }
        // "Alternative name" lines also carry quotes; skip them.
        if (line.find("Alternative name") != std::string::npos) {
            continue;
        }
        devices.emplace_back(line.substr(first_quote + 1, second_quote - first_quote - 1), type);
    }
    return devices;
}

struct CaptureProcess {
    std::string session_id;
    std::string module_id;
    std::string project_id;
    std::string acquisition_unit_id;
    std::string device;
    pamguard::service::CaptureSourceKind source_kind =
        pamguard::service::CaptureSourceKind::HttpUrl;
    std::size_t sample_rate_hz = 0;
    std::size_t channel_count = 0;
    std::optional<std::uint64_t> graph_revision;
    std::optional<std::uint64_t> working_revision;
    std::optional<std::uint64_t> binding_revision;
#ifdef _WIN32
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    DWORD pid = 0;
#else
    pid_t pid = -1;
#endif
};

struct RequiredProjectCapture {
    pamguard::service::AcquisitionCaptureTarget target;
    bool child_failed = false;
};

struct CaptureState {
    std::mutex mutex;
    std::unordered_map<std::string, CaptureProcess> running;
    /**
     * A project-owned capture becomes readiness-critical only after an
     * operator has successfully started it. Explicit capture/runtime stop and
     * project/revision transitions remove the requirement. Unexpected child
     * exit leaves the requirement latched so a later status/readiness request
     * cannot accidentally turn the failure back into "healthy" merely by
     * reaping the process handle.
     */
    std::unordered_map<std::string, RequiredProjectCapture>
        required_project_captures;
};

#ifdef _WIN32

bool capture_process_running(const CaptureProcess& capture) {
    return capture.process != nullptr && WaitForSingleObject(capture.process, 0) == WAIT_TIMEOUT;
}

void close_capture_process(CaptureProcess& capture) {
    // Closing the kill-on-close job object takes the whole ffmpeg tree down.
    if (capture.job != nullptr) {
        CloseHandle(capture.job);
        capture.job = nullptr;
    }
    if (capture.process != nullptr) {
        WaitForSingleObject(capture.process, 5000);
        CloseHandle(capture.process);
        capture.process = nullptr;
    }
}

bool start_capture_process(CaptureProcess& capture, const std::vector<std::string>& args,
                           std::string& error) {
    HANDLE job = CreateJobObjectA(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::string command = join_windows_command(args);
    const bool started = CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                                        &startup, &process) != 0;
    if (!started) {
        if (job != nullptr) {
            CloseHandle(job);
        }
        error = "could not start the ingest bridge (" + args.front() + ")";
        return false;
    }
    if (job != nullptr) {
        AssignProcessToJobObject(job, process.hProcess);
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    capture.process = process.hProcess;
    capture.job = job;
    capture.pid = process.dwProcessId;
    return true;
}

#else

bool capture_process_running(const CaptureProcess& capture) {
    if (capture.pid <= 0) {
        return false;
    }
    int status = 0;
    const auto result = waitpid(capture.pid, &status, WNOHANG);
    if (result == 0) {
        return true;
    }
    return false;
}

void close_capture_process(CaptureProcess& capture) {
    if (capture.pid <= 0) {
        return;
    }
    const auto pid = capture.pid;
    // The child establishes itself as a process-group leader before exec.
    // Addressing the negative PID therefore quiesces the ingest bridge and
    // any FFmpeg descendant without involving a command shell.
    (void)kill(-pid, SIGTERM);
    for (int attempt = 0; attempt < 50; ++attempt) {
        int status = 0;
        const auto result = waitpid(pid, &status, WNOHANG);
        if (result == pid ||
            (result < 0 && errno == ECHILD)) {
            capture.pid = -1;
            return;
        }
        usleep(100000);
    }
    (void)kill(-pid, SIGKILL);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 &&
           errno == EINTR) {
    }
    capture.pid = -1;
}

bool start_capture_process(
    CaptureProcess& capture,
    const std::vector<std::string>& args,
    std::string& error) {
    if (args.empty()) {
        error = "capture ingest command is empty";
        return false;
    }
    int exec_status[2]{-1, -1};
    if (pipe(exec_status) != 0) {
        error = "could not create the ingest bridge status pipe";
        return false;
    }
    const auto flags = fcntl(exec_status[1], F_GETFD);
    if (flags < 0 ||
        fcntl(exec_status[1], F_SETFD, flags | FD_CLOEXEC) < 0) {
        close(exec_status[0]);
        close(exec_status[1]);
        error = "could not secure the ingest bridge status pipe";
        return false;
    }
    const auto pid = fork();
    if (pid < 0) {
        close(exec_status[0]);
        close(exec_status[1]);
        error = "could not fork the ingest bridge";
        return false;
    }
    if (pid == 0) {
        close(exec_status[0]);
        (void)setpgid(0, 0);
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv.front(), argv.data());
        const auto child_errno = errno;
        (void)write(
            exec_status[1],
            &child_errno,
            sizeof(child_errno));
        _exit(127);
    }
    close(exec_status[1]);
    (void)setpgid(pid, pid);
    int child_errno = 0;
    std::size_t received = 0;
    while (received < sizeof(child_errno)) {
        const auto count = read(
            exec_status[0],
            reinterpret_cast<char*>(&child_errno) + received,
            sizeof(child_errno) - received);
        if (count > 0) {
            received += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    close(exec_status[0]);
    if (received != 0) {
        int status = 0;
        (void)waitpid(pid, &status, 0);
        error =
            "could not start the ingest bridge (" +
            args.front() + "): errno " +
            std::to_string(child_errno);
        return false;
    }
    capture.pid = pid;
    return true;
}

#endif // _WIN32

std::size_t quiesce_module_captures(CaptureState& state) {
    // Callers must already hold the module graph lifecycle mutex. Capture
    // routes take that mutex before this state mutex as well, establishing one
    // lock order across capture and graph/runtime transitions.
    std::size_t stopped = 0;
    std::lock_guard<std::mutex> lock(state.mutex);
    for (auto capture = state.running.begin();
         capture != state.running.end();) {
        if (capture->second.module_id.empty()) {
            ++capture;
            continue;
        }
        close_capture_process(capture->second);
        capture = state.running.erase(capture);
        ++stopped;
    }
    state.required_project_captures.clear();
    return stopped;
}

const char* capture_source_kind_name(
    pamguard::service::CaptureSourceKind source_kind) {
    return source_kind ==
            pamguard::service::CaptureSourceKind::DirectShowDevice
        ? "dshow"
        : "url";
}

std::string cors_origin_from_environment() {
    const char* raw = std::getenv("PAMGUARD_CORS_ORIGIN");
    if (raw == nullptr || std::string(raw).empty()) {
        return "*";
    }
    return raw;
}

std::string trim_secret(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    if (start > 0) {
        value.erase(0, start);
    }
    return value;
}

std::string read_secret_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not read API key file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return trim_secret(buffer.str());
}

std::string api_key_from_environment() {
    const char* raw = std::getenv("PAMGUARD_API_KEY");
    if (raw != nullptr && !std::string(raw).empty()) {
        return raw;
    }
    const char* file = std::getenv("PAMGUARD_API_KEY_FILE");
    if (file != nullptr && !std::string(file).empty()) {
        return read_secret_file(std::filesystem::path(file));
    }
    return {};
}

std::string safe_session_filename(const std::string& session_id) {
    std::string safe;
    safe.reserve(session_id.size());
    for (const unsigned char ch : session_id) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
            safe.push_back(static_cast<char>(ch));
        }
        else {
            safe.push_back('_');
        }
    }
    return safe.empty() ? "session" : safe;
}

std::filesystem::path session_config_path(const std::filesystem::path& config_dir, const std::string& session_id) {
    return config_dir / (safe_session_filename(session_id) + ".json");
}

void persist_session_config(const std::filesystem::path& config_dir, const std::string& session_id, const json& original_body) {
    if (config_dir.empty()) {
        return;
    }
    std::filesystem::create_directories(config_dir);
    std::ofstream output(session_config_path(config_dir, session_id));
    if (!output) {
        throw std::runtime_error("could not write session config file");
    }
    output << original_body.dump(2);
}

void remove_persisted_session_config(const std::filesystem::path& config_dir, const std::string& session_id) {
    if (config_dir.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(session_config_path(config_dir, session_id), error);
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not read file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string read_binary_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not read file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.eof() && input.fail()) {
        throw std::runtime_error(
            "could not finish reading file: " + path.string());
    }
    return buffer.str();
}

enum class WebAssetStatus {
    Ok,
    Forbidden,
    NotFound,
    Unsupported,
};

struct WebAssetResolution {
    WebAssetStatus status = WebAssetStatus::NotFound;
    std::filesystem::path path;
    std::string_view content_type;
};

std::string_view web_asset_content_type(
    const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

    static const std::unordered_map<std::string, std::string_view> types = {
        {".css", "text/css; charset=utf-8"},
        {".js", "application/javascript; charset=utf-8"},
        {".mjs", "application/javascript; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".map", "application/json; charset=utf-8"},
        {".svg", "image/svg+xml"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".webp", "image/webp"},
        {".ico", "image/x-icon"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".wasm", "application/wasm"},
    };
    const auto found = types.find(extension);
    return found == types.end() ? std::string_view() : found->second;
}

WebAssetResolution resolve_web_asset(
    const std::filesystem::path& asset_root,
    std::string_view requested_path) {
    if (requested_path.empty()) {
        return {WebAssetStatus::NotFound, {}, {}};
    }

    // cpp-httplib URL-decodes the request path before routing. Reject every
    // filesystem-significant form that could become a drive, alternate data
    // stream, parent segment, or second decoding pass on Windows.
    for (const unsigned char ch : requested_path) {
        if (ch < 0x20u || ch == 0x7fu ||
            ch == '\\' || ch == ':' || ch == '%') {
            return {WebAssetStatus::Forbidden, {}, {}};
        }
    }
    if (requested_path.front() == '/') {
        return {WebAssetStatus::Forbidden, {}, {}};
    }

    std::filesystem::path relative;
    std::size_t start = 0;
    while (start <= requested_path.size()) {
        const auto separator = requested_path.find('/', start);
        const auto end = separator == std::string_view::npos
            ? requested_path.size()
            : separator;
        const auto segment = requested_path.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return {WebAssetStatus::Forbidden, {}, {}};
        }
        relative /= std::string(segment);
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    if (relative.empty() || relative.is_absolute() ||
        relative.has_root_name() || relative.has_root_directory()) {
        return {WebAssetStatus::Forbidden, {}, {}};
    }

    const auto content_type = web_asset_content_type(relative);
    if (content_type.empty()) {
        return {WebAssetStatus::Unsupported, {}, {}};
    }

    std::error_code error;
    const auto resolved =
        std::filesystem::canonical(asset_root / relative, error);
    if (error) {
        return {WebAssetStatus::NotFound, {}, {}};
    }
    if (resolved == asset_root ||
        !path_is_within(asset_root, resolved)) {
        return {WebAssetStatus::Forbidden, {}, {}};
    }
    if (!std::filesystem::is_regular_file(resolved, error) || error) {
        return {WebAssetStatus::NotFound, {}, {}};
    }
    return {WebAssetStatus::Ok, resolved, content_type};
}

void append_result_archive(const std::filesystem::path& archive_dir, const std::string& session_id, const json& result_body) {
    if (archive_dir.empty()) {
        return;
    }
    std::filesystem::create_directories(archive_dir);
    const auto path = archive_dir / (safe_session_filename(session_id) + ".ndjson");
    std::ofstream output(path, std::ios::app);
    if (!output) {
        throw std::runtime_error("could not append result archive file");
    }
    output << result_body.dump() << '\n';
}

bool archive_record_matches(const json& record, const ArchiveQueryOptions& options) {
    if (!options.has_start_sample_from && !options.has_start_sample_to) {
        return true;
    }
    if (!record.contains("startSample")) {
        return false;
    }
    const auto start_sample = record.at("startSample").get<std::uint64_t>();
    if (options.has_start_sample_from && start_sample < options.start_sample_from) {
        return false;
    }
    if (options.has_start_sample_to && start_sample > options.start_sample_to) {
        return false;
    }
    return true;
}

json read_result_archive(const std::filesystem::path& archive_dir, const std::string& session_id, const ArchiveQueryOptions& options) {
    json records = json::array();
    if (archive_dir.empty()) {
        return records;
    }
    const auto path = archive_dir / (safe_session_filename(session_id) + ".ndjson");
    std::ifstream input(path);
    if (!input) {
        return records;
    }
    std::string line;
    if (options.limit == 0) {
        while (std::getline(input, line)) {
            if (!line.empty()) {
                auto record = json::parse(line);
                if (archive_record_matches(record, options)) {
                    records.push_back(std::move(record));
                }
            }
        }
        return records;
    }

    std::vector<json> recent_records;
    recent_records.reserve(options.limit);
    std::size_t next_slot = 0;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        auto record = json::parse(line);
        if (!archive_record_matches(record, options)) {
            continue;
        }
        if (recent_records.size() < options.limit) {
            recent_records.push_back(std::move(record));
        }
        else {
            recent_records[next_slot] = std::move(record);
            next_slot = (next_slot + 1) % options.limit;
        }
    }
    if (recent_records.size() < options.limit) {
        for (auto& record : recent_records) {
            records.push_back(std::move(record));
        }
    }
    else {
        for (std::size_t offset = 0; offset < recent_records.size(); ++offset) {
            records.push_back(std::move(recent_records[(next_slot + offset) % recent_records.size()]));
        }
    }
    return records;
}

bool json_uint64_field(const json& object, const char* field_name, std::uint64_t& value) {
    if (!object.is_object() || !object.contains(field_name)) {
        return false;
    }
    const auto& field = object.at(field_name);
    if (field.is_number_unsigned()) {
        value = field.get<std::uint64_t>();
        return true;
    }
    if (field.is_number_integer()) {
        const auto signed_value = field.get<std::int64_t>();
        if (signed_value < 0) {
            return false;
        }
        value = static_cast<std::uint64_t>(signed_value);
        return true;
    }
    return false;
}

bool detection_sample_matches(std::uint64_t start_sample, const ArchiveQueryOptions& options) {
    if (options.has_start_sample_from && start_sample < options.start_sample_from) {
        return false;
    }
    if (options.has_start_sample_to && start_sample > options.start_sample_to) {
        return false;
    }
    return true;
}

bool detection_interval_matches(std::uint64_t start_sample, bool has_end_sample, std::uint64_t end_sample, const ArchiveQueryOptions& options) {
    if (!detection_sample_matches(start_sample, options)) {
        return false;
    }
    const auto effective_end_sample = has_end_sample ? end_sample : start_sample;
    if (options.has_overlap_start_sample && effective_end_sample < options.overlap_start_sample) {
        return false;
    }
    if (options.has_overlap_end_sample && start_sample > options.overlap_end_sample) {
        return false;
    }
    return true;
}

bool detection_type_matches(const std::string& type, const std::string& type_filter) {
    return type_filter.empty() || type == type_filter;
}

std::string optional_json_string(const json& object, const char* field_name) {
    if (!object.is_object() || !object.contains(field_name) || !object.at(field_name).is_string()) {
        return {};
    }
    return object.at(field_name).get<std::string>();
}

bool archive_query_has_metadata_filters(const ArchiveQueryOptions& options) {
    return !options.source_id_filter.empty() || !options.owner_id_filter.empty() || !options.tenant_id_filter.empty();
}

bool detection_metadata_matches(const json& event, const ArchiveQueryOptions& options) {
    if (!options.source_id_filter.empty() && optional_json_string(event, "sourceId") != options.source_id_filter) {
        return false;
    }
    if (!options.owner_id_filter.empty() && optional_json_string(event, "ownerId") != options.owner_id_filter) {
        return false;
    }
    if (!options.tenant_id_filter.empty() && optional_json_string(event, "tenantId") != options.tenant_id_filter) {
        return false;
    }
    return true;
}

void attach_channel_group(json& event, const json& payload) {
    std::uint64_t value = 0;
    if (json_uint64_field(payload, "channelBitmap", value)) {
        event["channelGroup"] = "bitmap:" + std::to_string(value);
    }
    else if (json_uint64_field(payload, "triggerBitmap", value)) {
        event["channelGroup"] = "triggerBitmap:" + std::to_string(value);
    }
    else if (json_uint64_field(payload, "channel", value)) {
        event["channelGroup"] = "channel:" + std::to_string(value);
    }
}

void append_detection_event(
    json& events,
    const std::string& type,
    const std::string& session_id,
    std::uint64_t start_sample,
    bool has_end_sample,
    std::uint64_t end_sample,
    const json& payload,
    const ArchiveQueryOptions& options,
    const std::string& type_filter,
    bool has_record_start_sample,
    std::uint64_t record_start_sample,
    const json* related_train_ids_by_sample) {
    if (!detection_type_matches(type, type_filter) || !detection_interval_matches(start_sample, has_end_sample, end_sample, options)) {
        return;
    }

    json event = {
        {"type", type},
        {"sessionId", session_id},
        {"startSample", start_sample},
        {"payload", payload},
    };
    if (has_end_sample) {
        event["endSample"] = end_sample;
    }
    if (has_record_start_sample) {
        event["recordStartSample"] = record_start_sample;
    }
    if (related_train_ids_by_sample != nullptr) {
        const auto key = std::to_string(start_sample);
        if (related_train_ids_by_sample->contains(key)) {
            event["relatedTrainIds"] = related_train_ids_by_sample->at(key);
        }
    }
    attach_channel_group(event, payload);
    events.push_back(std::move(event));
}

void append_sampled_items(
    json& events,
    const json& record,
    const char* collection_name,
    const char* type,
    const char* start_sample_field,
    const char* duration_sample_field,
    const std::string& session_id,
    const ArchiveQueryOptions& options,
    const std::string& type_filter,
    bool has_record_start_sample,
    std::uint64_t record_start_sample,
    const json* related_train_ids_by_sample) {
    if (!record.contains(collection_name) || !record.at(collection_name).is_array()) {
        return;
    }
    for (const auto& item : record.at(collection_name)) {
        std::uint64_t start_sample = 0;
        if (!json_uint64_field(item, start_sample_field, start_sample)) {
            continue;
        }
        std::uint64_t end_sample = 0;
        bool has_end_sample = false;
        if (duration_sample_field != nullptr) {
            std::uint64_t duration_samples = 0;
            if (json_uint64_field(item, duration_sample_field, duration_samples)) {
                end_sample = duration_samples > std::numeric_limits<std::uint64_t>::max() - start_sample
                    ? std::numeric_limits<std::uint64_t>::max()
                    : start_sample + duration_samples;
                has_end_sample = true;
            }
        }
        append_detection_event(
            events,
            type,
            session_id,
            start_sample,
            has_end_sample,
            end_sample,
            item,
            options,
            type_filter,
            has_record_start_sample,
            record_start_sample,
            related_train_ids_by_sample);
    }
}

void append_ranged_items(
    json& events,
    const json& record,
    const char* collection_name,
    const char* type,
    const std::string& session_id,
    const ArchiveQueryOptions& options,
    const std::string& type_filter,
    bool has_record_start_sample,
    std::uint64_t record_start_sample) {
    if (!record.contains(collection_name) || !record.at(collection_name).is_array()) {
        return;
    }
    for (const auto& item : record.at(collection_name)) {
        std::uint64_t first_start_sample = 0;
        if (!json_uint64_field(item, "firstStartSample", first_start_sample)) {
            continue;
        }
        std::uint64_t last_start_sample = 0;
        const bool has_last_start_sample = json_uint64_field(item, "lastStartSample", last_start_sample);
        append_detection_event(
            events,
            type,
            session_id,
            first_start_sample,
            has_last_start_sample,
            has_last_start_sample ? last_start_sample : 0,
            item,
            options,
            type_filter,
            has_record_start_sample,
            record_start_sample,
            nullptr);
    }
}

json click_train_ids_by_sample(const json& record) {
    json by_sample = json::object();
    if (!record.contains("clickTrains") || !record.at("clickTrains").is_array()) {
        return by_sample;
    }
    for (const auto& train : record.at("clickTrains")) {
        std::uint64_t train_id = 0;
        if (!json_uint64_field(train, "trainId", train_id) || !train.contains("clickStartSamples") || !train.at("clickStartSamples").is_array()) {
            continue;
        }
        for (const auto& sample_value : train.at("clickStartSamples")) {
            std::uint64_t sample = 0;
            if (sample_value.is_number_unsigned()) {
                sample = sample_value.get<std::uint64_t>();
            }
            else if (sample_value.is_number_integer()) {
                const auto signed_sample = sample_value.get<std::int64_t>();
                if (signed_sample < 0) {
                    continue;
                }
                sample = static_cast<std::uint64_t>(signed_sample);
            }
            else {
                continue;
            }
            const auto key = std::to_string(sample);
            if (!by_sample.contains(key)) {
                by_sample[key] = json::array();
            }
            by_sample[key].push_back(train_id);
        }
    }
    return by_sample;
}

json detection_events_from_archive_record(const json& record, const ArchiveQueryOptions& options, const std::string& type_filter) {
    json events = json::array();
    const auto session_id = record.value("sessionId", std::string());
    std::uint64_t record_start_sample = 0;
    const bool has_record_start_sample = json_uint64_field(record, "startSample", record_start_sample);
    const auto train_ids_by_sample = click_train_ids_by_sample(record);

    append_sampled_items(events, record, "clicks", "click", "startSample", "durationSamples", session_id, options, type_filter, has_record_start_sample, record_start_sample, &train_ids_by_sample);
    append_sampled_items(events, record, "clickFeatures", "click-feature", "clickStartSample", nullptr, session_id, options, type_filter, has_record_start_sample, record_start_sample, &train_ids_by_sample);
    append_sampled_items(events, record, "clickClassifications", "click-classification", "clickStartSample", nullptr, session_id, options, type_filter, has_record_start_sample, record_start_sample, &train_ids_by_sample);
    append_sampled_items(events, record, "clickLocalisations", "click-localisation", "clickStartSample", nullptr, session_id, options, type_filter, has_record_start_sample, record_start_sample, &train_ids_by_sample);
    append_sampled_items(events, record, "clickBearings", "click-bearing", "clickStartSample", nullptr, session_id, options, type_filter, has_record_start_sample, record_start_sample, &train_ids_by_sample);
    append_ranged_items(events, record, "clickTrains", "click-track", session_id, options, type_filter, has_record_start_sample, record_start_sample);
    append_ranged_items(events, record, "mhtClickTrains", "mht-click-track", session_id, options, type_filter, has_record_start_sample, record_start_sample);
    append_ranged_items(events, record, "clickTrainLocalisations", "click-track-localisation", session_id, options, type_filter, has_record_start_sample, record_start_sample);
    append_ranged_items(events, record, "clickTrainBearings", "click-track-bearing", session_id, options, type_filter, has_record_start_sample, record_start_sample);
    append_sampled_items(events, record, "whistlePeaks", "whistle-peak", "startSample", nullptr, session_id, options, type_filter, has_record_start_sample, record_start_sample, nullptr);
    append_sampled_items(events, record, "whistleRegions", "whistle-contour", "startSample", "durationSamples", session_id, options, type_filter, has_record_start_sample, record_start_sample, nullptr);

    const auto source_id = optional_json_string(record, "sourceId");
    const auto owner_id = optional_json_string(record, "ownerId");
    const auto tenant_id = optional_json_string(record, "tenantId");
    for (auto& event : events) {
        event["sourceId"] = source_id.empty() ? json(nullptr) : json(source_id);
        event["ownerId"] = owner_id.empty() ? json(nullptr) : json(owner_id);
        event["tenantId"] = tenant_id.empty() ? json(nullptr) : json(tenant_id);
    }

    if (archive_query_has_metadata_filters(options)) {
        json filtered = json::array();
        for (auto& event : events) {
            if (detection_metadata_matches(event, options)) {
                filtered.push_back(std::move(event));
            }
        }
        return filtered;
    }

    return events;
}

ArchiveDetectionReadResult read_archive_detection_events_from_raw(
    const std::filesystem::path& archive_dir,
    const std::string& session_id,
    const ArchiveQueryOptions& options,
    const std::string& type_filter) {
    ArchiveDetectionReadResult result;
    if (archive_dir.empty()) {
        return result;
    }
    const auto path = archive_dir / (safe_session_filename(session_id) + ".ndjson");
    std::ifstream input(path);
    if (!input) {
        return result;
    }

    if (options.has_cursor) {
        std::uint64_t matching_cursor = 0;
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty()) {
                continue;
            }
            const auto record = json::parse(line);
            auto projected_events = detection_events_from_archive_record(record, options, type_filter);
            for (auto& event : projected_events) {
                if (matching_cursor >= options.cursor) {
                    if (options.limit == 0 || result.events.size() < options.limit) {
                        result.events.push_back(std::move(event));
                    }
                    else {
                        result.has_next_cursor = true;
                        result.next_cursor = matching_cursor;
                        return result;
                    }
                }
                ++matching_cursor;
            }
        }
        return result;
    }

    std::vector<json> recent_events;
    if (options.limit > 0) {
        recent_events.reserve(options.limit);
    }
    std::size_t next_slot = 0;

    auto capture_event = [&](json event) {
        if (options.limit == 0) {
            result.events.push_back(std::move(event));
            return;
        }
        if (recent_events.size() < options.limit) {
            recent_events.push_back(std::move(event));
        }
        else {
            recent_events[next_slot] = std::move(event);
            next_slot = (next_slot + 1) % options.limit;
        }
    };

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto record = json::parse(line);
        auto projected_events = detection_events_from_archive_record(record, options, type_filter);
        for (auto& event : projected_events) {
            capture_event(std::move(event));
        }
    }

    if (options.limit == 0) {
        return result;
    }
    if (recent_events.size() < options.limit) {
        for (auto& event : recent_events) {
            result.events.push_back(std::move(event));
        }
    }
    else {
        for (std::size_t offset = 0; offset < recent_events.size(); ++offset) {
            result.events.push_back(std::move(recent_events[(next_slot + offset) % recent_events.size()]));
        }
    }
    return result;
}

bool archived_detection_event_matches(const json& event, const ArchiveQueryOptions& options, const std::string& type_filter) {
    if (!event.is_object() || !event.contains("type") || !event.at("type").is_string()) {
        return false;
    }
    if (!detection_type_matches(event.at("type").get<std::string>(), type_filter)) {
        return false;
    }
    std::uint64_t start_sample = 0;
    if (!json_uint64_field(event, "startSample", start_sample)) {
        return false;
    }
    std::uint64_t end_sample = 0;
    const bool has_end_sample = json_uint64_field(event, "endSample", end_sample);
    return detection_interval_matches(start_sample, has_end_sample, end_sample, options) && detection_metadata_matches(event, options);
}

std::filesystem::path archive_detection_event_file_path(const std::filesystem::path& archive_dir, const std::string& session_id) {
    return archive_dir / (safe_session_filename(session_id) + ".events.ndjson");
}

std::filesystem::path archive_detection_index_file_path(const std::filesystem::path& archive_dir, const std::string& session_id) {
    return archive_dir / (safe_session_filename(session_id) + ".events.index.ndjson");
}

bool detection_index_entry_from_json(const json& value, ArchiveDetectionIndexEntry& entry) {
    if (!value.is_object() || !value.contains("type") || !value.at("type").is_string()) {
        return false;
    }
    std::uint64_t offset = 0;
    std::uint64_t start_sample = 0;
    if (!json_uint64_field(value, "offset", offset) || !json_uint64_field(value, "startSample", start_sample)) {
        return false;
    }
    entry.offset = offset;
    entry.type = value.at("type").get<std::string>();
    entry.start_sample = start_sample;
    entry.has_end_sample = json_uint64_field(value, "endSample", entry.end_sample);
    return true;
}

bool archived_detection_index_entry_matches(
    const ArchiveDetectionIndexEntry& entry,
    const ArchiveQueryOptions& options,
    const std::string& type_filter) {
    if (!detection_type_matches(entry.type, type_filter)) {
        return false;
    }
    return detection_interval_matches(entry.start_sample, entry.has_end_sample, entry.end_sample, options);
}

json read_detection_event_at_offset(std::ifstream& event_input, std::uint64_t offset) {
    const auto max_stream_offset = static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max());
    if (offset > max_stream_offset) {
        throw std::runtime_error("archive detection event offset exceeds stream range");
    }
    event_input.clear();
    event_input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!event_input) {
        throw std::runtime_error("failed to seek archive detection event file");
    }
    std::string line;
    if (!std::getline(event_input, line) || line.empty()) {
        throw std::runtime_error("failed to read indexed archive detection event");
    }
    return json::parse(line);
}

ArchiveDetectionReadResult read_archive_detection_event_index_file(
    std::ifstream& event_input,
    std::ifstream& index_input,
    const ArchiveQueryOptions& options,
    const std::string& type_filter) {
    ArchiveDetectionReadResult result;
    result.used_index = true;

    if (options.has_cursor) {
        std::uint64_t matching_cursor = 0;
        std::string line;
        while (std::getline(index_input, line)) {
            if (line.empty()) {
                continue;
            }
            ArchiveDetectionIndexEntry entry;
            if (!detection_index_entry_from_json(json::parse(line), entry)) {
                continue;
            }
            if (!archived_detection_index_entry_matches(entry, options, type_filter)) {
                continue;
            }
            if (matching_cursor >= options.cursor) {
                if (options.limit == 0 || result.events.size() < options.limit) {
                    auto event = read_detection_event_at_offset(event_input, entry.offset);
                    if (archived_detection_event_matches(event, options, type_filter)) {
                        result.events.push_back(std::move(event));
                    }
                }
                else {
                    result.has_next_cursor = true;
                    result.next_cursor = matching_cursor;
                    return result;
                }
            }
            ++matching_cursor;
        }
        return result;
    }

    std::vector<std::uint64_t> recent_offsets;
    if (options.limit > 0) {
        recent_offsets.reserve(options.limit);
    }
    std::size_t next_slot = 0;

    auto capture_offset = [&](std::uint64_t offset) {
        if (options.limit == 0) {
            auto event = read_detection_event_at_offset(event_input, offset);
            if (archived_detection_event_matches(event, options, type_filter)) {
                result.events.push_back(std::move(event));
            }
            return;
        }
        if (recent_offsets.size() < options.limit) {
            recent_offsets.push_back(offset);
        }
        else {
            recent_offsets[next_slot] = offset;
            next_slot = (next_slot + 1) % options.limit;
        }
    };

    std::string line;
    while (std::getline(index_input, line)) {
        if (line.empty()) {
            continue;
        }
        ArchiveDetectionIndexEntry entry;
        if (!detection_index_entry_from_json(json::parse(line), entry)) {
            continue;
        }
        if (archived_detection_index_entry_matches(entry, options, type_filter)) {
            capture_offset(entry.offset);
        }
    }

    if (options.limit == 0) {
        return result;
    }

    auto append_offset = [&](std::uint64_t offset) {
        auto event = read_detection_event_at_offset(event_input, offset);
        if (archived_detection_event_matches(event, options, type_filter)) {
            result.events.push_back(std::move(event));
        }
    };

    if (recent_offsets.size() < options.limit) {
        for (const auto offset : recent_offsets) {
            append_offset(offset);
        }
    }
    else {
        for (std::size_t offset_index = 0; offset_index < recent_offsets.size(); ++offset_index) {
            append_offset(recent_offsets[(next_slot + offset_index) % recent_offsets.size()]);
        }
    }
    return result;
}

ArchiveDetectionReadResult read_archive_detection_event_file(std::ifstream& input, const ArchiveQueryOptions& options, const std::string& type_filter) {
    ArchiveDetectionReadResult result;
    if (options.has_cursor) {
        std::uint64_t matching_cursor = 0;
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty()) {
                continue;
            }
            auto event = json::parse(line);
            if (!archived_detection_event_matches(event, options, type_filter)) {
                continue;
            }
            if (matching_cursor >= options.cursor) {
                if (options.limit == 0 || result.events.size() < options.limit) {
                    result.events.push_back(std::move(event));
                }
                else {
                    result.has_next_cursor = true;
                    result.next_cursor = matching_cursor;
                    return result;
                }
            }
            ++matching_cursor;
        }
        return result;
    }

    std::vector<json> recent_events;
    if (options.limit > 0) {
        recent_events.reserve(options.limit);
    }
    std::size_t next_slot = 0;

    auto capture_event = [&](json event) {
        if (options.limit == 0) {
            result.events.push_back(std::move(event));
            return;
        }
        if (recent_events.size() < options.limit) {
            recent_events.push_back(std::move(event));
        }
        else {
            recent_events[next_slot] = std::move(event);
            next_slot = (next_slot + 1) % options.limit;
        }
    };

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        auto event = json::parse(line);
        if (archived_detection_event_matches(event, options, type_filter)) {
            capture_event(std::move(event));
        }
    }

    if (options.limit == 0) {
        return result;
    }
    if (recent_events.size() < options.limit) {
        for (auto& event : recent_events) {
            result.events.push_back(std::move(event));
        }
    }
    else {
        for (std::size_t offset = 0; offset < recent_events.size(); ++offset) {
            result.events.push_back(std::move(recent_events[(next_slot + offset) % recent_events.size()]));
        }
    }
    return result;
}

ArchiveDetectionReadResult read_archive_detection_events(
    const std::filesystem::path& archive_dir,
    const std::string& session_id,
    const ArchiveQueryOptions& options,
    const std::string& type_filter) {
    if (archive_dir.empty()) {
        return {};
    }
    const auto event_path = archive_detection_event_file_path(archive_dir, session_id);
    const auto index_path = archive_detection_index_file_path(archive_dir, session_id);
    std::ifstream event_input(event_path, std::ios::binary);
    std::ifstream index_input(index_path);
    if (event_input && index_input && !archive_query_has_metadata_filters(options)) {
        return read_archive_detection_event_index_file(event_input, index_input, options, type_filter);
    }
    if (event_input) {
        return read_archive_detection_event_file(event_input, options, type_filter);
    }

    return read_archive_detection_events_from_raw(archive_dir, session_id, options, type_filter);
}

json make_archive_detection_summary(const std::string& session_id, const std::string& source, bool indexed_available) {
    return {
        {"sessionId", session_id},
        {"source", source},
        {"indexedAvailable", indexed_available},
        {"totalCount", 0},
        {"types", json::object()},
        {"minStartSample", nullptr},
        {"maxStartSample", nullptr},
    };
}

void capture_archive_detection_summary_event(json& summary, const json& event) {
    std::uint64_t start_sample = 0;
    if (!json_uint64_field(event, "startSample", start_sample)) {
        return;
    }
    summary["totalCount"] = summary.at("totalCount").get<std::uint64_t>() + 1;
    if (summary.at("minStartSample").is_null() || start_sample < summary.at("minStartSample").get<std::uint64_t>()) {
        summary["minStartSample"] = start_sample;
    }
    if (summary.at("maxStartSample").is_null() || start_sample > summary.at("maxStartSample").get<std::uint64_t>()) {
        summary["maxStartSample"] = start_sample;
    }
    const auto type = optional_json_string(event, "type");
    if (!type.empty()) {
        auto& type_counts = summary["types"];
        const auto previous = type_counts.contains(type) ? type_counts.at(type).get<std::uint64_t>() : 0;
        type_counts[type] = previous + 1;
    }
}

json summarize_archive_detection_event_file(
    std::ifstream& input,
    const std::string& session_id,
    const ArchiveQueryOptions& options,
    const std::string& type_filter,
    bool indexed_available) {
    auto summary = make_archive_detection_summary(session_id, "event-sidecar", indexed_available);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto event = json::parse(line);
        if (archived_detection_event_matches(event, options, type_filter)) {
            capture_archive_detection_summary_event(summary, event);
        }
    }
    return summary;
}

json summarize_archive_detection_events_from_raw(
    const std::filesystem::path& archive_dir,
    const std::string& session_id,
    const ArchiveQueryOptions& options,
    const std::string& type_filter) {
    auto summary = make_archive_detection_summary(session_id, "result-archive", false);
    if (archive_dir.empty()) {
        return summary;
    }
    const auto path = archive_dir / (safe_session_filename(session_id) + ".ndjson");
    std::ifstream input(path);
    if (!input) {
        return summary;
    }
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto record = json::parse(line);
        auto projected_events = detection_events_from_archive_record(record, options, type_filter);
        for (const auto& event : projected_events) {
            capture_archive_detection_summary_event(summary, event);
        }
    }
    return summary;
}

json summarize_archive_detection_events(
    const std::filesystem::path& archive_dir,
    const std::string& session_id,
    const ArchiveQueryOptions& options,
    const std::string& type_filter) {
    if (archive_dir.empty()) {
        return make_archive_detection_summary(session_id, "none", false);
    }
    const auto event_path = archive_detection_event_file_path(archive_dir, session_id);
    const auto index_path = archive_detection_index_file_path(archive_dir, session_id);
    const bool indexed_available = std::filesystem::exists(index_path);
    std::ifstream event_input(event_path);
    if (event_input) {
        return summarize_archive_detection_event_file(event_input, session_id, options, type_filter, indexed_available);
    }
    return summarize_archive_detection_events_from_raw(archive_dir, session_id, options, type_filter);
}

std::string csv_escape(const std::string& value) {
    bool quote = false;
    for (const char ch : value) {
        if (ch == '"' || ch == ',' || ch == '\n' || ch == '\r') {
            quote = true;
            break;
        }
    }
    if (!quote) {
        return value;
    }
    std::string out = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            out += "\"\"";
        }
        else {
            out.push_back(ch);
        }
    }
    out.push_back('"');
    return out;
}

std::string json_cell(const json& object, const char* field_name) {
    if (!object.is_object() || !object.contains(field_name) || object.at(field_name).is_null()) {
        return {};
    }
    const auto& field = object.at(field_name);
    if (field.is_string()) {
        return field.get<std::string>();
    }
    return field.dump();
}

std::string related_train_ids_cell(const json& event) {
    if (!event.contains("relatedTrainIds") || !event.at("relatedTrainIds").is_array()) {
        return {};
    }
    std::ostringstream out;
    bool first = true;
    for (const auto& id : event.at("relatedTrainIds")) {
        if (!first) {
            out << ';';
        }
        first = false;
        out << (id.is_string() ? id.get<std::string>() : id.dump());
    }
    return out.str();
}

std::string detection_events_to_csv(const json& events) {
    std::ostringstream out;
    out << "type,sessionId,sourceId,ownerId,tenantId,startSample,endSample,recordStartSample,channelGroup,relatedTrainIds,payload\n";
    if (!events.is_array()) {
        return out.str();
    }
    for (const auto& event : events) {
        out << csv_escape(json_cell(event, "type")) << ','
            << csv_escape(json_cell(event, "sessionId")) << ','
            << csv_escape(json_cell(event, "sourceId")) << ','
            << csv_escape(json_cell(event, "ownerId")) << ','
            << csv_escape(json_cell(event, "tenantId")) << ','
            << csv_escape(json_cell(event, "startSample")) << ','
            << csv_escape(json_cell(event, "endSample")) << ','
            << csv_escape(json_cell(event, "recordStartSample")) << ','
            << csv_escape(json_cell(event, "channelGroup")) << ','
            << csv_escape(related_train_ids_cell(event)) << ','
            << csv_escape(json_cell(event, "payload")) << '\n';
    }
    return out.str();
}

void append_detection_event_archive(const std::filesystem::path& archive_dir, const std::string& session_id, const json& result_body) {
    if (archive_dir.empty()) {
        return;
    }
    ArchiveQueryOptions projection_options;
    const auto events = detection_events_from_archive_record(result_body, projection_options, std::string());
    if (events.empty()) {
        return;
    }
    std::filesystem::create_directories(archive_dir);
    const auto path = archive_detection_event_file_path(archive_dir, session_id);
    const auto index_path = archive_detection_index_file_path(archive_dir, session_id);
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        throw std::runtime_error("failed to open archive detection event file");
    }
    std::ofstream index_output(index_path, std::ios::app);
    if (!index_output) {
        throw std::runtime_error("failed to open archive detection index file");
    }
    for (const auto& event : events) {
        output.seekp(0, std::ios::end);
        const auto raw_position = output.tellp();
        if (raw_position == std::ofstream::pos_type(-1)) {
            throw std::runtime_error("failed to read archive detection event offset");
        }
        const auto raw_offset = static_cast<std::streamoff>(raw_position);
        if (raw_offset < 0) {
            throw std::runtime_error("archive detection event offset was negative");
        }
        const auto event_offset = static_cast<std::uint64_t>(raw_offset);
        output << event.dump() << '\n';
        std::uint64_t start_sample = 0;
        if (!event.contains("type") || !event.at("type").is_string() || !json_uint64_field(event, "startSample", start_sample)) {
            continue;
        }
        json index_entry = {
            {"schemaVersion", 1},
            {"offset", event_offset},
            {"type", event.at("type").get<std::string>()},
            {"startSample", start_sample},
        };
        std::uint64_t end_sample = 0;
        if (json_uint64_field(event, "endSample", end_sample)) {
            index_entry["endSample"] = end_sample;
        }
        if (event.contains("channelGroup") && event.at("channelGroup").is_string()) {
            index_entry["channelGroup"] = event.at("channelGroup");
        }
        if (event.contains("sourceId") && event.at("sourceId").is_string()) {
            index_entry["sourceId"] = event.at("sourceId");
        }
        if (event.contains("ownerId") && event.at("ownerId").is_string()) {
            index_entry["ownerId"] = event.at("ownerId");
        }
        if (event.contains("tenantId") && event.at("tenantId").is_string()) {
            index_entry["tenantId"] = event.at("tenantId");
        }
        index_output << index_entry.dump() << '\n';
    }
}

pamguard::detectors::FrequencyRange parse_frequency_range(const json& value) {
    pamguard::detectors::FrequencyRange range;
    if (value.is_array() && value.size() >= 2) {
        range.low_hz = value.at(0).get<double>();
        range.high_hz = value.at(1).get<double>();
    }
    else if (value.is_object()) {
        range.low_hz = value.value("lowHz", 0.0);
        range.high_hz = value.value("highHz", 0.0);
    }
    else {
        throw std::invalid_argument("frequency range must be [lowHz, highHz] or {lowHz, highHz}");
    }
    validate_ordered_range(range, "range");
    return range;
}

std::string normalized_token(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
        return ch == '-' || ch == '_' || std::isspace(ch) != 0;
    }), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

json click_train_ids_by_sample(const std::vector<pamguard::detectors::ClickTrainSummary>& trains) {
    json by_sample = json::object();
    for (const auto& train : trains) {
        for (const auto start_sample : train.click_start_samples) {
            const auto key = std::to_string(start_sample);
            if (!by_sample.contains(key)) {
                by_sample[key] = json::array();
            }
            by_sample[key].push_back(train.train_id);
        }
    }
    return by_sample;
}

void attach_related_train_ids(json& item, std::int64_t start_sample, const json& train_ids_by_sample) {
    const auto key = std::to_string(start_sample);
    if (train_ids_by_sample.contains(key)) {
        item["relatedTrainIds"] = train_ids_by_sample.at(key);
    }
}

pamguard::dsp::WindowType parse_window_type(const json& value) {
    if (value.is_number_integer()) {
        const auto raw = value.get<int>();
        if (raw >= 0 && raw <= 5) {
            return static_cast<pamguard::dsp::WindowType>(raw);
        }
        throw std::invalid_argument("FFT window type integer must be in the range 0..5");
    }
    if (!value.is_string()) {
        throw std::invalid_argument("FFT window type must be a string or integer");
    }

    const auto name = normalized_token(value.get<std::string>());
    if (name == "rectangular" || name == "rectangle" || name == "none") {
        return pamguard::dsp::WindowType::Rectangular;
    }
    if (name == "hamming") {
        return pamguard::dsp::WindowType::Hamming;
    }
    if (name == "hann" || name == "hanning") {
        return pamguard::dsp::WindowType::Hann;
    }
    if (name == "bartlett" || name == "triangular" || name == "bartletttriangular") {
        return pamguard::dsp::WindowType::Bartlett;
    }
    if (name == "blackman") {
        return pamguard::dsp::WindowType::Blackman;
    }
    if (name == "blackmanharris") {
        return pamguard::dsp::WindowType::BlackmanHarris;
    }
    throw std::invalid_argument("unknown FFT window type: " + value.get<std::string>());
}

pamguard::detectors::BasicClickTypeConfig parse_basic_click_type(const json& value) {
    pamguard::detectors::BasicClickTypeConfig type;
    type.species_code = value.value("speciesCode", type.species_code);
    type.discard = value.value("discard", type.discard);
    type.which_selections = value.value("whichSelections", type.which_selections);
    if (value.contains("band1FreqHz")) {
        type.band1_freq_hz = parse_frequency_range(value.at("band1FreqHz"));
    }
    if (value.contains("band2FreqHz")) {
        type.band2_freq_hz = parse_frequency_range(value.at("band2FreqHz"));
    }
    if (value.contains("band1EnergyDb")) {
        type.band1_energy_db = parse_frequency_range(value.at("band1EnergyDb"));
    }
    if (value.contains("band2EnergyDb")) {
        type.band2_energy_db = parse_frequency_range(value.at("band2EnergyDb"));
    }
    type.band_energy_difference_db = value.value("bandEnergyDifferenceDb", type.band_energy_difference_db);
    if (value.contains("peakFrequencySearchHz")) {
        type.peak_frequency_search_hz = parse_frequency_range(value.at("peakFrequencySearchHz"));
    }
    if (value.contains("peakFrequencyRangeHz")) {
        type.peak_frequency_range_hz = parse_frequency_range(value.at("peakFrequencyRangeHz"));
    }
    if (value.contains("peakWidthHz")) {
        type.peak_width_hz = parse_frequency_range(value.at("peakWidthHz"));
    }
    type.width_energy_fraction = value.value("widthEnergyFraction", type.width_energy_fraction);
    if (value.contains("meanSumRangeHz")) {
        type.mean_sum_range_hz = parse_frequency_range(value.at("meanSumRangeHz"));
    }
    if (value.contains("meanSelectionRangeHz")) {
        type.mean_selection_range_hz = parse_frequency_range(value.at("meanSelectionRangeHz"));
    }
    if (value.contains("clickLengthMs")) {
        type.click_length_ms = parse_frequency_range(value.at("clickLengthMs"));
    }
    type.length_energy_fraction = value.value("lengthEnergyFraction", type.length_energy_fraction);
    return type;
}

pamguard::detectors::BasicClickStandardType parse_basic_click_standard(const std::string& value) {
    if (value == "beakedWhale" || value == "beaked_whale" || value == "Beaked Whale") {
        return pamguard::detectors::BasicClickStandardType::BeakedWhale;
    }
    if (value == "porpoise" || value == "Porpoise") {
        return pamguard::detectors::BasicClickStandardType::Porpoise;
    }
    throw std::invalid_argument("unknown basic click standard type: " + value);
}

pamguard::detectors::BasicClickTypeConfig parse_basic_click_standard_type(const json& value) {
    if (value.is_string()) {
        const auto standard = parse_basic_click_standard(value.get<std::string>());
        const int species_code = standard == pamguard::detectors::BasicClickStandardType::BeakedWhale ? 1 : 2;
        return pamguard::detectors::standard_basic_click_type(species_code, standard);
    }
    if (!value.is_object()) {
        throw std::invalid_argument("standard click classifier type must be a string or object");
    }

    const auto standard = parse_basic_click_standard(value.at("standard").get<std::string>());
    const int default_code = standard == pamguard::detectors::BasicClickStandardType::BeakedWhale ? 1 : 2;
    auto type = pamguard::detectors::standard_basic_click_type(value.value("speciesCode", default_code), standard);
    type.discard = value.value("discard", type.discard);
    if (value.contains("whichSelections")) {
        type.which_selections = value.at("whichSelections").get<std::uint32_t>();
    }
    return type;
}

pamguard::detectors::SweepRange parse_sweep_range(const json& value) {
    const auto range = parse_frequency_range(value);
    return {range.low_hz, range.high_hz};
}

pamguard::detectors::SweepChannelChoice parse_sweep_channel_choice(const json& value) {
    if (value.is_number_integer()) {
        const int choice = value.get<int>();
        if (choice >= 0 && choice <= 2) {
            return static_cast<pamguard::detectors::SweepChannelChoice>(choice);
        }
    }
    if (value.is_string()) {
        const auto choice = normalized_token(value.get<std::string>());
        if (choice == "requireall" || choice == "all") {
            return pamguard::detectors::SweepChannelChoice::RequireAll;
        }
        if (choice == "requireone" || choice == "one") {
            return pamguard::detectors::SweepChannelChoice::RequireOne;
        }
        if (choice == "usemeans" || choice == "means" || choice == "mean") {
            return pamguard::detectors::SweepChannelChoice::UseMeans;
        }
    }
    throw std::invalid_argument("sweep channelChoice must be requireAll, requireOne, useMeans, or 0..2");
}

pamguard::detectors::SweepFftFilterBand parse_sweep_filter_band(const json& value) {
    const auto band = normalized_token(value.get<std::string>());
    if (band == "highpass") return pamguard::detectors::SweepFftFilterBand::HighPass;
    if (band == "lowpass") return pamguard::detectors::SweepFftFilterBand::LowPass;
    if (band == "bandpass") return pamguard::detectors::SweepFftFilterBand::BandPass;
    if (band == "bandstop") return pamguard::detectors::SweepFftFilterBand::BandStop;
    throw std::invalid_argument("sweep fftFilter.band must be highPass, lowPass, bandPass, or bandStop");
}

pamguard::detectors::SweepClickTypeConfig parse_sweep_click_type(const json& value) {
    pamguard::detectors::SweepClickTypeConfig type;
    type.name = value.value("name", type.name);
    type.species_code = value.value("speciesCode", type.species_code);
    type.discard = value.value("discard", type.discard);
    type.enabled = value.value("enabled", type.enabled);
    if (value.contains("channelChoice")) {
        type.channel_choice = parse_sweep_channel_choice(value.at("channelChoice"));
    }
    type.restrict_length = value.value("restrictLength", type.restrict_length);
    type.restricted_bins = value.value("restrictedBins", type.restricted_bins);
    if (value.contains("restrictedBinType")) {
        const auto& bin_type = value.at("restrictedBinType");
        if (bin_type.is_number_integer()) {
            type.restricted_bin_type = bin_type.get<int>() == 1
                ? pamguard::detectors::SweepRestrictedBinType::ClickStart
                : pamguard::detectors::SweepRestrictedBinType::ClickCenter;
        }
        else {
            const auto name = normalized_token(bin_type.get<std::string>());
            type.restricted_bin_type = name == "clickstart" || name == "start"
                ? pamguard::detectors::SweepRestrictedBinType::ClickStart
                : pamguard::detectors::SweepRestrictedBinType::ClickCenter;
        }
    }
    type.enable_length = value.value("enableLength", type.enable_length);
    type.length_smoothing = value.value("lengthSmoothing", type.length_smoothing);
    type.length_db = value.value("lengthDb", type.length_db);
    if (value.contains("lengthMs")) type.length_ms = parse_sweep_range(value.at("lengthMs"));

    type.enable_energy_bands = value.value("enableEnergyBands", type.enable_energy_bands);
    if (value.contains("testEnergyBandHz")) {
        type.test_energy_band_hz = parse_sweep_range(value.at("testEnergyBandHz"));
    }
    if (value.contains("controlEnergyBand0Hz")) {
        type.control_energy_band_0_hz = parse_sweep_range(value.at("controlEnergyBand0Hz"));
    }
    if (value.contains("controlEnergyBand1Hz")) {
        type.control_energy_band_1_hz = parse_sweep_range(value.at("controlEnergyBand1Hz"));
    }
    type.energy_threshold_0_db = value.value("energyThreshold0Db", type.energy_threshold_0_db);
    type.energy_threshold_1_db = value.value("energyThreshold1Db", type.energy_threshold_1_db);

    type.test_amplitude = value.value("testAmplitude", type.test_amplitude);
    if (value.contains("amplitudeRangeDb")) {
        type.amplitude_range_db = parse_sweep_range(value.at("amplitudeRangeDb"));
    }

    type.enable_fft_filter = value.value("enableFftFilter", type.enable_fft_filter);
    if (value.contains("fftFilter")) {
        const auto& filter = value.at("fftFilter");
        if (filter.contains("band")) type.fft_filter.band = parse_sweep_filter_band(filter.at("band"));
        type.fft_filter.low_pass_freq_hz =
            filter.value("lowPassFreqHz", type.fft_filter.low_pass_freq_hz);
        type.fft_filter.high_pass_freq_hz =
            filter.value("highPassFreqHz", type.fft_filter.high_pass_freq_hz);
    }

    type.enable_peak = value.value("enablePeak", type.enable_peak);
    type.enable_width = value.value("enableWidth", type.enable_width);
    type.enable_mean = value.value("enableMean", type.enable_mean);
    if (value.contains("peakSearchRangeHz")) {
        type.peak_search_range_hz = parse_sweep_range(value.at("peakSearchRangeHz"));
    }
    if (value.contains("peakRangeHz")) {
        type.peak_range_hz = parse_sweep_range(value.at("peakRangeHz"));
    }
    if (value.contains("peakWidthRangeHz")) {
        type.peak_width_range_hz = parse_sweep_range(value.at("peakWidthRangeHz"));
    }
    if (value.contains("meanRangeHz")) {
        type.mean_range_hz = parse_sweep_range(value.at("meanRangeHz"));
    }
    type.peak_smoothing = value.value("peakSmoothing", type.peak_smoothing);
    type.peak_width_threshold_db =
        value.value("peakWidthThresholdDb", type.peak_width_threshold_db);

    type.enable_zero_crossings =
        value.value("enableZeroCrossings", type.enable_zero_crossings);
    if (value.contains("zeroCrossingCount")) {
        type.zero_crossing_count = parse_sweep_range(value.at("zeroCrossingCount"));
    }
    type.enable_sweep = value.value("enableSweep", type.enable_sweep);
    if (value.contains("zeroCrossingSweepKhzPerMs")) {
        type.zero_crossing_sweep_khz_per_ms =
            parse_sweep_range(value.at("zeroCrossingSweepKhzPerMs"));
    }

    type.enable_min_cross_correlation =
        value.value("enableMinCrossCorrelation", type.enable_min_cross_correlation);
    type.enable_peak_cross_correlation =
        value.value("enablePeakCrossCorrelation", type.enable_peak_cross_correlation);
    type.min_correlation = value.value("minCorrelation", type.min_correlation);
    type.correlation_factor = value.value("correlationFactor", type.correlation_factor);

    type.enable_bearing_limits =
        value.value("enableBearingLimits", type.enable_bearing_limits);
    type.exclude_bearing_limits =
        value.value("excludeBearingLimits", type.exclude_bearing_limits);
    if (value.contains("bearingLimitsRadians")) {
        type.bearing_limits_radians = parse_sweep_range(value.at("bearingLimitsRadians"));
    }
    return type;
}

pamguard::detectors::SweepClickTypeConfig parse_sweep_click_standard_type(const json& value) {
    if (value.is_string()) {
        const auto standard = parse_basic_click_standard(value.get<std::string>());
        const int code = standard == pamguard::detectors::BasicClickStandardType::BeakedWhale ? 1 : 2;
        return pamguard::detectors::standard_sweep_click_type(code, standard);
    }
    if (!value.is_object()) {
        throw std::invalid_argument("standard sweep classifier type must be a string or object");
    }
    const auto standard = parse_basic_click_standard(value.at("standard").get<std::string>());
    const int default_code =
        standard == pamguard::detectors::BasicClickStandardType::BeakedWhale ? 1 : 2;
    auto type = pamguard::detectors::standard_sweep_click_type(
        value.value("speciesCode", default_code), standard);
    type.name = value.value("name", type.name);
    type.discard = value.value("discard", type.discard);
    type.enabled = value.value("enabled", type.enabled);
    return type;
}

void validate_analysis_config(const pamguard::core::AnalysisConfig& config) {
    validate_base_config(config);

    if (!is_power_of_two_size(config.detector.fft.fft_length)) {
        throw std::invalid_argument("fft.length must be a non-zero power of two");
    }
    if (config.detector.fft.fft_hop == 0) {
        throw std::invalid_argument("fft.hop must be positive");
    }
    if (config.detector.fft.fft_hop > config.detector.fft.fft_length) {
        throw std::invalid_argument("fft.hop must be less than or equal to fft.length");
    }
    validate_channel_list(config.detector.fft.channels, config.channel_count, "fft.channels");

    if (config.array.speed_of_sound_mps <= 0.0 || !std::isfinite(config.array.speed_of_sound_mps)) {
        throw std::invalid_argument("array.speedOfSoundMps must be positive and finite");
    }
    {
        std::vector<bool> hydrophone_channels(config.channel_count, false);
        for (const auto& hydrophone : config.array.hydrophones) {
            if (hydrophone.channel >= config.channel_count) {
                throw std::invalid_argument("array.hydrophones contains channel outside channelCount");
            }
            if (hydrophone_channels[hydrophone.channel]) {
                throw std::invalid_argument("array.hydrophones must not contain duplicate channels");
            }
            hydrophone_channels[hydrophone.channel] = true;
            validate_finite(hydrophone.x_m, "array.hydrophones.xM");
            validate_finite(hydrophone.y_m, "array.hydrophones.yM");
            validate_finite(hydrophone.z_m, "array.hydrophones.zM");
            validate_finite(hydrophone.sensitivity_db, "array.hydrophones.sensitivityDb");
        }
    }

    if (config.detector.click_detector_enabled) {
        validate_click_bitmap(config.detector.click.channel_bitmap, config.channel_count, "click.channelBitmap");
        validate_click_bitmap(config.detector.click.trigger_bitmap, config.channel_count, "click.triggerBitmap");
        const auto triggerable_channels = config.detector.click.channel_bitmap & config.detector.click.trigger_bitmap;
        if (config.detector.click.min_trigger_channels == 0 ||
            config.detector.click.min_trigger_channels > bitmap_bit_count(triggerable_channels)) {
            throw std::invalid_argument("click.minTriggerChannels must be between 1 and the number of triggerable detector channels");
        }
        if (!std::isfinite(config.detector.click.short_filter) ||
            !std::isfinite(config.detector.click.long_filter) ||
            !std::isfinite(config.detector.click.long_filter_2) ||
            config.detector.click.short_filter < 0.0 || config.detector.click.short_filter > 1.0 ||
            config.detector.click.long_filter < 0.0 || config.detector.click.long_filter > 1.0 ||
            config.detector.click.long_filter_2 < 0.0 || config.detector.click.long_filter_2 > 1.0) {
            throw std::invalid_argument(
                "click.shortFilter, click.longFilter, and click.longFilter2 must be in the range 0..1");
        }
        if (config.detector.click.max_length == 0) {
            throw std::invalid_argument("click.maxLength must be positive");
        }
        if (config.detector.click.sample_noise &&
            (!(config.detector.click.noise_sample_interval_seconds > 0.0) ||
             !std::isfinite(config.detector.click.noise_sample_interval_seconds))) {
            throw std::invalid_argument(
                "click.noise.waveformIntervalSeconds must be positive and finite");
        }
        if (config.detector.click.store_background &&
            config.detector.click.background_interval_milliseconds <= 0) {
            throw std::invalid_argument(
                "click.noise.backgroundIntervalMilliseconds must be positive");
        }
        if (config.detector.click_features_enabled) {
            if (!is_power_of_two_size(config.detector.click_features.fft_length)) {
                throw std::invalid_argument("click.features.fftLength must be a non-zero power of two");
            }
            validate_percentage(config.detector.click_features.length_energy_fraction, "click.features.lengthEnergyFraction");
            validate_percentage(config.detector.click_features.width_energy_fraction, "click.features.widthEnergyFraction");
            for (const auto& band : config.detector.click_features.energy_bands_hz) {
                validate_nonnegative_range(band, "click.features.energyBandsHz");
            }
            validate_nonnegative_range(config.detector.click_features.peak_frequency_search_hz, "click.features.peakFrequencySearchHz");
            validate_nonnegative_range(config.detector.click_features.mean_frequency_range_hz, "click.features.meanFrequencyRangeHz");
        }
        if (config.detector.click_train_tracker_enabled) {
            if (!std::isfinite(config.detector.click_train.min_ici_seconds) ||
                !std::isfinite(config.detector.click_train.max_ici_seconds) ||
                config.detector.click_train.min_ici_seconds < 0.0 ||
                config.detector.click_train.max_ici_seconds <= 0.0 ||
                config.detector.click_train.min_ici_seconds >
                    config.detector.click_train.max_ici_seconds ||
                config.detector.click_train.min_clicks == 0) {
                throw std::invalid_argument(
                    "click.train ICI range must be ordered and non-negative, "
                    "and click.train.minClicks must be positive");
            }
        }
    }

    if (config.detector.whistle_peak_detector_enabled) {
        validate_finite(config.detector.whistle_peak.detection_threshold_db, "whistle.detectionThresholdDb");
        validate_finite(config.detector.whistle_peak.peak_time_constant_0, "whistle.peakTimeConstant0");
        validate_finite(config.detector.whistle_peak.peak_time_constant_1, "whistle.peakTimeConstant1");
        validate_percentage(config.detector.whistle_peak.max_percent_over_threshold, "whistle.maxPercentOverThreshold");
        if (config.detector.whistle_peak.min_peak_width == 0 ||
            config.detector.whistle_peak.max_peak_width < config.detector.whistle_peak.min_peak_width) {
            throw std::invalid_argument("whistle peak width limits must be positive and ordered");
        }
        const auto half_bins = config.detector.fft.fft_length / 2;
        if (config.detector.whistle_peak.search_bin0 >= half_bins) {
            throw std::invalid_argument("whistle.searchBin0 must be inside the FFT half spectrum");
        }
        if (config.detector.whistle_peak.search_bin1 != 0 &&
            (config.detector.whistle_peak.search_bin1 < config.detector.whistle_peak.search_bin0 ||
             config.detector.whistle_peak.search_bin1 >= half_bins)) {
            throw std::invalid_argument("whistle.searchBin1 must be zero for auto or inside the FFT half spectrum after searchBin0");
        }
    }

    if (config.detector.whistle_region_detector_enabled) {
        if (config.detector.whistle_region.min_pixels == 0 || config.detector.whistle_region.min_length == 0) {
            throw std::invalid_argument("whistle minPixels and minLength must be positive");
        }
        if (!std::isfinite(config.detector.whistle_region.min_frequency_hz) ||
            !std::isfinite(config.detector.whistle_region.max_frequency_hz)) {
            throw std::invalid_argument("whistle frequency limits must be finite");
        }
        if (!(config.detector.whistle_region.background_interval_seconds > 0.0) ||
            !std::isfinite(
                config.detector.whistle_region.background_interval_seconds)) {
            throw std::invalid_argument(
                "whistle.backgroundIntervalSeconds must be positive and finite");
        }
        if (config.detector.whistle_region.connect_type != 4 && config.detector.whistle_region.connect_type != 8) {
            throw std::invalid_argument("whistle.connectType must be 4 or 8");
        }
        if (config.detector.whistle_region.fragmentation_method < 0 || config.detector.whistle_region.fragmentation_method > 3) {
            throw std::invalid_argument("whistle.fragmentationMethod must be in the range 0..3");
        }
    }
}

/**
 * Live result feed (WP7 "multiple subscribers can watch one shared session"):
 * a per-session ring of the most recent result bodies, each stamped with a
 * monotonically increasing sequence number. Any number of viewers poll
 * GET /sessions/{id}/results?sinceSeq=K and receive everything newer than K —
 * the engine session itself stays shared, one detector state per source, and
 * viewers cost a ring lookup, not a session.
 */
struct SessionResultFeed {
    std::uint64_t next_sequence = 1;
    std::deque<std::pair<std::uint64_t, json>> recent;
};

std::size_t result_feed_depth_from_environment() {
    const char* raw = std::getenv("PAMGUARD_RESULT_FEED_DEPTH");
    if (raw == nullptr) {
        return 16;
    }
    return static_cast<std::size_t>(std::stoul(raw));
}

std::filesystem::path audio_archive_dir_from_environment() {
    const char* raw = std::getenv("PAMGUARD_AUDIO_ARCHIVE_DIR");
    return raw == nullptr ? std::filesystem::path() : std::filesystem::path(raw);
}

/**
 * Audio archive (WP3): the exact f32le bytes each session analysed, append-
 * only, with an NDJSON index of {startSample, frames, timeMs, byteOffset,
 * byteLength} per chunk. Gaps and overlaps stay visible as startSample
 * discontinuities in the index, and replay feeds the same bytes through the
 * same chunk boundaries — the strongest form of the determinism acceptance.
 */
std::filesystem::path audio_archive_data_path(const std::filesystem::path& dir, const std::string& session_id) {
    return dir / (safe_session_filename(session_id) + ".f32le");
}

std::filesystem::path audio_archive_index_path(const std::filesystem::path& dir, const std::string& session_id) {
    return dir / (safe_session_filename(session_id) + ".audio.ndjson");
}

void append_audio_archive(const std::filesystem::path& dir, const std::string& session_id,
                          const std::string& pcm_bytes, std::uint64_t start_sample, std::uint64_t frames,
                          std::int64_t time_ms, std::uint32_t sample_rate_hz, std::size_t channel_count) {
    std::filesystem::create_directories(dir);
    const auto data_path = audio_archive_data_path(dir, session_id);
    const auto index_path = audio_archive_index_path(dir, session_id);
    std::ofstream data(data_path, std::ios::binary | std::ios::app);
    if (!data) {
        throw std::runtime_error("failed to open audio archive data file");
    }
    data.seekp(0, std::ios::end);
    const std::uint64_t offset = static_cast<std::uint64_t>(data.tellp());
    data.write(pcm_bytes.data(), static_cast<std::streamsize>(pcm_bytes.size()));
    data.flush();
    std::ofstream index(index_path, std::ios::app);
    if (!index) {
        throw std::runtime_error("failed to open audio archive index file");
    }
    const json record = {
        {"startSample", start_sample},
        {"frames", frames},
        {"timeMs", time_ms},
        {"byteOffset", offset},
        {"byteLength", pcm_bytes.size()},
        {"sampleRateHz", sample_rate_hz},
        {"channelCount", channel_count},
    };
    index << record.dump() << "\n";
}

struct AudioIndexRecord {
    std::uint64_t start_sample = 0;
    std::uint64_t frames = 0;
    std::int64_t time_ms = 0;
    std::uint64_t byte_offset = 0;
    std::uint64_t byte_length = 0;
    std::uint32_t sample_rate_hz = 0;
    std::size_t channel_count = 0;
};

std::vector<AudioIndexRecord> read_audio_archive_index(const std::filesystem::path& dir,
                                                       const std::string& session_id) {
    std::vector<AudioIndexRecord> records;
    std::ifstream index(audio_archive_index_path(dir, session_id));
    if (!index) {
        return records;
    }
    std::string line;
    while (std::getline(index, line)) {
        if (line.empty()) {
            continue;
        }
        const auto record = json::parse(line);
        AudioIndexRecord entry;
        entry.start_sample = record.value("startSample", 0ULL);
        entry.frames = record.value("frames", 0ULL);
        entry.time_ms = record.value("timeMs", static_cast<std::int64_t>(0));
        entry.byte_offset = record.value("byteOffset", 0ULL);
        entry.byte_length = record.value("byteLength", 0ULL);
        entry.sample_rate_hz = record.value("sampleRateHz", 0U);
        entry.channel_count = record.value("channelCount", static_cast<std::size_t>(0));
        records.push_back(entry);
    }
    return records;
}

/**
 * Offline batch jobs (WP7): a queued WAV analysis run through the same
 * session machinery, results archived under the job id like any session's,
 * so the existing archive/query/export endpoints serve job output.
 *
 * Enabled only when PAMGUARD_JOB_AUDIO_DIR is set; job WAV paths resolve
 * strictly inside that directory, so the HTTP surface cannot read arbitrary
 * files.
 */
struct OfflineJob {
    std::string job_id;
    std::string wav_file;
    /** When set, replay the archived audio of this session instead of a WAV. */
    std::string audio_session;
    json session_body;
    std::string state = "queued"; // queued | running | completed | failed | cancelled
    std::string error;
    std::uint64_t total_frames = 0;
    std::uint64_t processed_frames = 0;
    std::uint64_t chunks = 0;
    std::uint64_t clicks = 0;
    std::uint64_t click_trains = 0;
    std::uint64_t whistle_regions = 0;
    std::int64_t created_unix_ms = 0;
    std::int64_t started_unix_ms = 0;
    std::int64_t finished_unix_ms = 0;
    bool cancel_requested = false;
};

struct JobQueueState {
    std::mutex mutex;
    std::condition_variable cv;
    std::map<std::string, OfflineJob> jobs;
    std::deque<std::string> pending;
    bool shutting_down = false;
};

std::filesystem::path job_audio_dir_from_environment() {
    const char* raw = std::getenv("PAMGUARD_JOB_AUDIO_DIR");
    return raw == nullptr ? std::filesystem::path() : std::filesystem::path(raw);
}

std::size_t job_workers_from_environment() {
    const char* raw = std::getenv("PAMGUARD_JOB_WORKERS");
    if (raw == nullptr) {
        return 1;
    }
    const auto value = std::stoul(raw);
    return value == 0 ? 1 : static_cast<std::size_t>(value);
}

std::int64_t now_unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/**
 * Resolve a job WAV path strictly inside the configured audio root. Rejects
 * absolute paths and anything whose canonical form escapes the root, which
 * closes the path-traversal door the endpoint would otherwise open.
 */
std::filesystem::path resolve_job_wav(const std::filesystem::path& audio_root, const std::string& wav_file) {
    const std::filesystem::path relative(wav_file);
    if (relative.is_absolute()) {
        throw std::invalid_argument("wavFile must be relative to the job audio directory");
    }
    const auto root = std::filesystem::weakly_canonical(audio_root);
    const auto candidate = std::filesystem::weakly_canonical(audio_root / relative);
    const auto root_text = root.generic_string();
    const auto candidate_text = candidate.generic_string();
    if (candidate_text.size() < root_text.size() || candidate_text.compare(0, root_text.size(), root_text) != 0) {
        throw std::invalid_argument("wavFile escapes the job audio directory");
    }
    return candidate;
}

json job_to_json(const OfflineJob& job) {
    json body = {
        {"jobId", job.job_id},
        {"wavFile", job.wav_file},
        {"audioSession", job.audio_session},
        {"state", job.state},
        {"totalFrames", job.total_frames},
        {"processedFrames", job.processed_frames},
        {"chunks", job.chunks},
        {"clicks", job.clicks},
        {"clickTrains", job.click_trains},
        {"whistleRegions", job.whistle_regions},
        {"createdUnixMs", job.created_unix_ms},
        {"sessionId", std::string("job-") + job.job_id},
    };
    if (job.started_unix_ms != 0) {
        body["startedUnixMs"] = job.started_unix_ms;
    }
    if (job.finished_unix_ms != 0) {
        body["finishedUnixMs"] = job.finished_unix_ms;
    }
    if (!job.error.empty()) {
        body["error"] = job.error;
    }
    return body;
}

pamguard::dsp::IirFilterParams parse_iir_filter(const json& filter) {
    pamguard::dsp::IirFilterParams params;
    const auto type = filter.value("type", std::string("none"));
    if (type == "none") {
        params.type = pamguard::dsp::IirFilterType::None;
        return params;
    }
    if (type == "butterworth") {
        params.type = pamguard::dsp::IirFilterType::Butterworth;
    }
    else if (type == "chebyshev") {
        params.type = pamguard::dsp::IirFilterType::Chebyshev;
    }
    else if (type == "firwindow") {
        params.type = pamguard::dsp::IirFilterType::FirWindow;
    }
    else if (type == "firarbitrary") {
        params.type = pamguard::dsp::IirFilterType::FirArbitrary;
    }
    else if (type == "fft") {
        params.type = pamguard::dsp::IirFilterType::Fft;
    }
    else {
        throw std::invalid_argument(
            "filter type must be none, butterworth, chebyshev, firwindow, firarbitrary, or fft");
    }
    const auto band = filter.value("band", std::string("highpass"));
    if (band == "highpass") {
        params.band = pamguard::dsp::IirFilterBand::HighPass;
    }
    else if (band == "lowpass") {
        params.band = pamguard::dsp::IirFilterBand::LowPass;
    }
    else if (band == "bandpass") {
        params.band = pamguard::dsp::IirFilterBand::BandPass;
    }
    else if (band == "bandstop") {
        params.band = pamguard::dsp::IirFilterBand::BandStop;
    }
    else {
        throw std::invalid_argument("filter band must be highpass, lowpass, bandpass, or bandstop");
    }
    params.order = filter.value("order", 4);
    params.high_pass_freq_hz = filter.value("highPassFreq", 0.0F);
    params.low_pass_freq_hz = filter.value("lowPassFreq", 0.0F);
    params.pass_band_ripple_db = filter.value("passBandRipple", 2.0);
    params.stop_band_ripple_db = filter.value("stopBandRipple", 2.0);
    params.cheby_gamma = filter.value("chebyGamma", 3.0);
    if (filter.contains("arbitraryFrequenciesHz")) {
        params.arbitrary_frequencies_hz =
            filter.at("arbitraryFrequenciesHz").get<std::vector<double>>();
    }
    if (filter.contains("arbitraryGainsDb")) {
        params.arbitrary_gains_db =
            filter.at("arbitraryGainsDb").get<std::vector<double>>();
    }
    if (params.type != pamguard::dsp::IirFilterType::Fft &&
        (params.order <= 0 || params.order > 32)) {
        throw std::invalid_argument("filter order must be between 1 and 32");
    }
    if ((params.type == pamguard::dsp::IirFilterType::FirWindow ||
         params.type == pamguard::dsp::IirFilterType::FirArbitrary) &&
        params.order > 16) {
        throw std::invalid_argument(
            "FIR filter order exponent must be between 1 and 16");
    }
    if (!std::isfinite(params.high_pass_freq_hz) || !std::isfinite(params.low_pass_freq_hz) ||
        params.high_pass_freq_hz < 0.0F || params.low_pass_freq_hz < 0.0F) {
        throw std::invalid_argument("filter frequencies must be non-negative and finite");
    }
    if (!(params.cheby_gamma > 0.0) || !std::isfinite(params.cheby_gamma)) {
        throw std::invalid_argument("filter chebyGamma must be positive and finite");
    }
    if (params.type == pamguard::dsp::IirFilterType::FirArbitrary) {
        if (params.arbitrary_frequencies_hz.size() < 2 ||
            params.arbitrary_frequencies_hz.size() !=
                params.arbitrary_gains_db.size()) {
            throw std::invalid_argument(
                "arbitrary FIR filter needs equal arbitraryFrequenciesHz and arbitraryGainsDb arrays with at least two points");
        }
        for (std::size_t i = 0; i < params.arbitrary_frequencies_hz.size(); ++i) {
            if (!std::isfinite(params.arbitrary_frequencies_hz[i]) ||
                !std::isfinite(params.arbitrary_gains_db[i]) ||
                params.arbitrary_frequencies_hz[i] < 0.0 ||
                (i > 0 && params.arbitrary_frequencies_hz[i] <
                              params.arbitrary_frequencies_hz[i - 1])) {
                throw std::invalid_argument(
                    "arbitrary FIR control points must be finite, non-negative, and frequency ordered");
            }
        }
    }
    return params;
}

pamguard::localisation::DelayMeasurementConfig parse_delay_measurement(
    const json& value,
    pamguard::localisation::DelayMeasurementConfig params = {}) {
    params.filter_bearings = value.value("filterBearings", params.filter_bearings);
    params.envelope_bearings =
        value.value("envelopeBearings", params.envelope_bearings);
    params.use_leading_edge =
        value.value("useLeadingEdge", params.use_leading_edge);
    params.up_sample = value.value("upSample", params.up_sample);
    params.use_restricted_bins =
        value.value("useRestrictedBins", params.use_restricted_bins);
    params.restricted_bins =
        value.value("restrictedBins", params.restricted_bins);
    if (value.contains("filter")) {
        const auto& filter = value.at("filter");
        const auto band = filter.value("band", std::string("highpass"));
        if (band == "highpass") {
            params.filter_band = pamguard::localisation::DelayFilterBand::HighPass;
        }
        else if (band == "lowpass") {
            params.filter_band = pamguard::localisation::DelayFilterBand::LowPass;
        }
        else if (band == "bandpass") {
            params.filter_band = pamguard::localisation::DelayFilterBand::BandPass;
        }
        else if (band == "bandstop") {
            params.filter_band = pamguard::localisation::DelayFilterBand::BandStop;
        }
        else {
            throw std::invalid_argument(
                "click.delayMeasurement.filter.band must be highpass, lowpass, bandpass, or bandstop");
        }
        params.filter_high_pass_hz =
            filter.value("highPassFreq", params.filter_high_pass_hz);
        params.filter_low_pass_hz =
            filter.value("lowPassFreq", params.filter_low_pass_hz);
    }
    if (params.up_sample < 1 || params.up_sample > 32) {
        throw std::invalid_argument(
            "click.delayMeasurement.upSample must be between 1 and 32");
    }
    if (params.use_restricted_bins && params.restricted_bins < 10) {
        throw std::invalid_argument(
            "click.delayMeasurement.restrictedBins must be at least 10 when enabled");
    }
    if (!std::isfinite(params.filter_high_pass_hz) ||
        !std::isfinite(params.filter_low_pass_hz) ||
        params.filter_high_pass_hz < 0.0 ||
        params.filter_low_pass_hz < 0.0) {
        throw std::invalid_argument(
            "click.delayMeasurement filter frequencies must be non-negative and finite");
    }
    // The Java dialog only permits leading-edge correlation with the envelope.
    params.use_leading_edge =
        params.use_leading_edge && params.envelope_bearings;
    return params;
}

pamguard::core::AnalysisConfig parse_config(const json& body) {
    pamguard::core::AnalysisConfig config;
    config.session_id = body.at("sessionId").get<std::string>();
    config.source_id = body.value("sourceId", config.session_id);
    config.owner_id = body.value("ownerId", std::string());
    config.tenant_id = body.value("tenantId", std::string());
    config.sample_rate_hz = body.at("sampleRateHz").get<std::uint32_t>();
    config.channel_count = body.at("channelCount").get<std::size_t>();
    validate_base_config(config);

    const auto array = body.value("array", json::object());
    config.array.id = array.value("id", config.array.id);
    config.array.speed_of_sound_mps = array.value("speedOfSoundMps", config.array.speed_of_sound_mps);
    config.array.speed_of_sound_error_mps = array.value("speedOfSoundErrorMps", config.array.speed_of_sound_error_mps);
    config.array.timing_error_seconds = array.value("timingErrorSeconds", config.array.timing_error_seconds);
    config.array.spacing_error_m = array.value("spacingErrorM", config.array.spacing_error_m);
    config.array.wobble_radians = array.value("wobbleRadians", config.array.wobble_radians);
    if (config.array.speed_of_sound_error_mps < 0.0 || !std::isfinite(config.array.speed_of_sound_error_mps) ||
        config.array.timing_error_seconds < 0.0 || !std::isfinite(config.array.timing_error_seconds) ||
        config.array.spacing_error_m < 0.0 || !std::isfinite(config.array.spacing_error_m) ||
        config.array.wobble_radians < 0.0 || !std::isfinite(config.array.wobble_radians)) {
        throw std::invalid_argument("array.speedOfSoundErrorMps, timingErrorSeconds, spacingErrorM, and wobbleRadians must be non-negative and finite");
    }
    {
        const auto noise_band = body.value("noiseBand", json::object());
        auto& noise_config = config.detector.noise_band;
        noise_config.enabled = noise_band.value("enabled", false);
        if (noise_config.enabled) {
            const auto band = noise_band.value("bandType", std::string("thirdOctave"));
            if (band == "octave") noise_config.band_type = pamguard::detectors::NoiseBandType::Octave;
            else if (band == "thirdOctave") noise_config.band_type = pamguard::detectors::NoiseBandType::ThirdOctave;
            else if (band == "decidecade") noise_config.band_type = pamguard::detectors::NoiseBandType::Decidecade;
            else if (band == "decade") noise_config.band_type = pamguard::detectors::NoiseBandType::Decade;
            else if (band == "tenthOctave") noise_config.band_type = pamguard::detectors::NoiseBandType::TenthOctave;
            else if (band == "twelfthOctave") noise_config.band_type = pamguard::detectors::NoiseBandType::TwelfthOctave;
            else throw std::invalid_argument("noiseBand.bandType must be octave, thirdOctave, decidecade, decade, tenthOctave, or twelfthOctave");
            noise_config.min_frequency_hz = noise_band.value("minFrequencyHz", noise_config.min_frequency_hz);
            noise_config.max_frequency_hz = noise_band.value("maxFrequencyHz", noise_config.max_frequency_hz);
            noise_config.reference_frequency_hz = noise_band.value("referenceFrequencyHz", noise_config.reference_frequency_hz);
            noise_config.iir_order = noise_band.value("iirOrder", noise_config.iir_order);
            noise_config.output_interval_seconds = noise_band.value("outputIntervalSeconds", noise_config.output_interval_seconds);
            if (!(noise_config.min_frequency_hz > 0.0) || noise_config.iir_order <= 0 ||
                !(noise_config.output_interval_seconds > 0.0)) {
                throw std::invalid_argument("noiseBand needs positive minFrequencyHz, iirOrder, and outputIntervalSeconds");
            }
        }
        const auto fft_noise = body.value("fftNoise", json::object());
        auto& fft_noise_config = config.detector.fft_noise;
        fft_noise_config.enabled = fft_noise.value("enabled", false);
        if (fft_noise_config.enabled) {
            const auto bitmap = fft_noise.value("channelBitmap", 1u);
            for (std::size_t channel = 0;
                 channel < std::min<std::size_t>(config.channel_count, 32);
                 ++channel) {
                if ((bitmap & (1u << channel)) != 0) {
                    fft_noise_config.channels.push_back(channel);
                }
            }
            fft_noise_config.measurement_interval_seconds =
                fft_noise.value("measurementIntervalSeconds", 60);
            fft_noise_config.n_measures =
                fft_noise.value("nMeasures", 100);
            fft_noise_config.use_all =
                fft_noise.value("useAll", true);
            if (fft_noise.contains("bands")) {
                if (!fft_noise.at("bands").is_array()) {
                    throw std::invalid_argument(
                        "fftNoise.bands must be an array");
                }
                for (const auto& item : fft_noise.at("bands")) {
                    pamguard::detectors::FftNoiseBand band;
                    band.name = item.value("name", std::string("User"));
                    band.low_frequency_hz =
                        item.at("lowFrequencyHz").get<double>();
                    band.high_frequency_hz =
                        item.at("highFrequencyHz").get<double>();
                    if (!std::isfinite(band.low_frequency_hz) ||
                        !std::isfinite(band.high_frequency_hz) ||
                        band.low_frequency_hz < 0.0 ||
                        !(band.high_frequency_hz >
                          band.low_frequency_hz) ||
                        band.high_frequency_hz >
                          static_cast<double>(config.sample_rate_hz) / 2.0) {
                        throw std::invalid_argument(
                            "fftNoise bands need 0 <= lowFrequencyHz < highFrequencyHz <= Nyquist");
                    }
                    fft_noise_config.bands.push_back(std::move(band));
                }
            }
            if (fft_noise_config.channels.empty() ||
                fft_noise_config.measurement_interval_seconds <= 0 ||
                fft_noise_config.n_measures <= 0 ||
                (!fft_noise_config.use_all &&
                 fft_noise_config.n_measures < 2) ||
                fft_noise_config.bands.empty()) {
                throw std::invalid_argument(
                    "fftNoise needs selected channels, a positive interval, at least one band, and nMeasures >= 2 when useAll is false");
            }
        }
        const auto ltsa = body.value("ltsa", json::object());
        config.detector.ltsa.enabled = ltsa.value("enabled", false);
        if (config.detector.ltsa.enabled) {
            config.detector.ltsa.interval_seconds = ltsa.value("intervalSeconds", config.detector.ltsa.interval_seconds);
            if (config.detector.ltsa.interval_seconds <= 0) {
                throw std::invalid_argument("ltsa.intervalSeconds must be positive");
            }
        }
        const auto ishmael = body.value("ishmael", json::object());
        auto& ish_config = config.detector.ishmael;
        ish_config.enabled = ishmael.value("enabled", false);
        if (ish_config.enabled) {
            ish_config.f0 = ishmael.value("f0", ish_config.f0);
            ish_config.f1 = ishmael.value("f1", ish_config.f1);
            ish_config.ratio_f0 = ishmael.value("ratioF0", ish_config.ratio_f0);
            ish_config.ratio_f1 = ishmael.value("ratioF1", ish_config.ratio_f1);
            ish_config.use_ratio = ishmael.value("useRatio", ish_config.use_ratio);
            ish_config.use_log = ishmael.value("useLog", ish_config.use_log);
            ish_config.adaptive_threshold = ishmael.value("adaptiveThreshold", ish_config.adaptive_threshold);
            ish_config.long_filter = ishmael.value("longFilter", ish_config.long_filter);
            ish_config.spike_decay = ishmael.value("spikeDecay", ish_config.spike_decay);
            ish_config.output_smoothing = ishmael.value("outputSmoothing", ish_config.output_smoothing);
            ish_config.short_filter = ishmael.value("shortFilter", ish_config.short_filter);
            ish_config.thresh = ishmael.value("thresh", ish_config.thresh);
            ish_config.min_time_s = ishmael.value("minTimeSeconds", ish_config.min_time_s);
            ish_config.max_time_s = ishmael.value("maxTimeSeconds", ish_config.max_time_s);
            ish_config.refractory_time_s = ishmael.value("refractoryTimeSeconds", ish_config.refractory_time_s);
            if (!(ish_config.f1 > ish_config.f0) || ish_config.min_time_s < 0.0 ||
                ish_config.max_time_s < 0.0 || ish_config.refractory_time_s < 0.0) {
                throw std::invalid_argument("ishmael needs f1 > f0 and non-negative times");
            }
        }
        const auto sgram = body.value("sgramCorr", json::object());
        auto& sgram_config = config.detector.sgram_corr;
        sgram_config.enabled = sgram.value("enabled", false);
        if (sgram_config.enabled) {
            const auto segments = sgram.value("segments", json::array());
            for (const auto& seg : segments) {
                if (!seg.is_array() || seg.size() != 4) {
                    throw std::invalid_argument("sgramCorr.segments entries must be [t0, f0, t1, f1]");
                }
                sgram_config.segments.push_back({seg[0].get<double>(), seg[1].get<double>(),
                                                 seg[2].get<double>(), seg[3].get<double>()});
            }
            sgram_config.spread = sgram.value("spread", sgram_config.spread);
            sgram_config.use_log = sgram.value("useLog", sgram_config.use_log);
            sgram_config.thresh = sgram.value("thresh", sgram_config.thresh);
            sgram_config.min_time_s = sgram.value("minTimeSeconds", sgram_config.min_time_s);
            sgram_config.max_time_s = sgram.value("maxTimeSeconds", sgram_config.max_time_s);
            sgram_config.refractory_time_s = sgram.value("refractoryTimeSeconds", sgram_config.refractory_time_s);
            if (sgram_config.segments.empty() || !(sgram_config.spread > 0.0)) {
                throw std::invalid_argument("sgramCorr needs segments and positive spread");
            }
        }
        const auto match_filt = body.value("matchFilt", json::object());
        auto& mf_config = config.detector.match_filt;
        mf_config.enabled = match_filt.value("enabled", false);
        if (mf_config.enabled) {
            mf_config.kernel = match_filt.value("kernel", std::vector<double>{});
            mf_config.channels = match_filt.value("channels", std::vector<std::size_t>{});
            mf_config.thresh = match_filt.value("thresh", mf_config.thresh);
            mf_config.min_time_s = match_filt.value("minTimeSeconds", mf_config.min_time_s);
            mf_config.max_time_s = match_filt.value("maxTimeSeconds", mf_config.max_time_s);
            mf_config.refractory_time_s = match_filt.value("refractoryTimeSeconds", mf_config.refractory_time_s);
            if (mf_config.kernel.empty()) {
                throw std::invalid_argument("matchFilt needs a non-empty kernel waveform");
            }
        }
        auto& mt_config = config.detector.matched_template;
        if (body.contains("matchedTemplate")) {
            const auto& matched = body.at("matchedTemplate");
            if (!matched.is_object() ||
                !matched.contains("enabled") ||
                !matched.at("enabled").is_boolean()) {
                throw std::invalid_argument(
                    "matchedTemplate must be an object with boolean enabled");
            }
            mt_config.enabled = matched.at("enabled").get<bool>();
            if (!mt_config.enabled && matched.size() != 1) {
                throw std::invalid_argument(
                    "disabled matchedTemplate accepts only the enabled field");
            }
        }
        if (mt_config.enabled) {
            auto settings_value = body.at("matchedTemplate");
            settings_value.erase("enabled");
            const auto settings =
                pamguard::core::matched_template_settings_from_json(
                    settings_value.dump(),
                    1);
            config.detector.matched_template_click_type =
                settings.click_type;
            mt_config.normalisation_type =
                settings.normalisation_type;
            mt_config.peak_search = settings.peak_search;
            mt_config.peak_smoothing = settings.peak_smoothing;
            mt_config.length_db = settings.length_db;
            mt_config.restricted_bins = settings.restricted_bins;
            mt_config.channel_classification =
                settings.channel_classification;
            mt_config.classifiers = settings.classifiers;
            // Surface template problems (decimation, empty waveforms) at
            // session creation rather than at first audio.
            pamguard::detectors::MatchedTemplateClassifier probe(
                config.sample_rate_hz != 0 ? static_cast<double>(config.sample_rate_hz) : 0.0, mt_config);
            if (!probe.valid()) {
                throw std::invalid_argument("matchedTemplate: " + probe.invalid_reason());
            }
        }
        const auto acquisition = body.value("acquisition", json::object());
        config.acquisition.volts_peak_to_peak = acquisition.value("voltsPeak2Peak", config.acquisition.volts_peak_to_peak);
        config.acquisition.preamp_gain_db = acquisition.value("preampGainDb", config.acquisition.preamp_gain_db);
        if (!(config.acquisition.volts_peak_to_peak > 0.0) || !std::isfinite(config.acquisition.preamp_gain_db)) {
            throw std::invalid_argument("acquisition.voltsPeak2Peak must be positive and preampGainDb finite");
        }
    }
    if (array.contains("orientation")) {
        const auto& orientation = array.at("orientation");
        config.array.orientation.declared = true;
        config.array.orientation.heading_degrees = orientation.value("headingDegrees", 0.0);
        config.array.orientation.pitch_degrees = orientation.value("pitchDegrees", 0.0);
        config.array.orientation.roll_degrees = orientation.value("rollDegrees", 0.0);
        if (!std::isfinite(config.array.orientation.heading_degrees) ||
            !std::isfinite(config.array.orientation.pitch_degrees) ||
            !std::isfinite(config.array.orientation.roll_degrees)) {
            throw std::invalid_argument("array.orientation headingDegrees, pitchDegrees, and rollDegrees must be finite");
        }
    }
    if (array.contains("streamers")) {
        for (const auto& streamer : array.at("streamers")) {
            pamguard::core::ArrayStreamer item;
            item.id = streamer.at("id").get<int>();
            item.x_m = streamer.value("xM", 0.0);
            item.y_m = streamer.value("yM", 0.0);
            item.z_m = streamer.value("zM", 0.0);
            item.heading_degrees = streamer.value("headingDegrees", 0.0);
            item.pitch_degrees = streamer.value("pitchDegrees", 0.0);
            item.roll_degrees = streamer.value("rollDegrees", 0.0);
            if (!std::isfinite(item.heading_degrees) || !std::isfinite(item.pitch_degrees) ||
                !std::isfinite(item.roll_degrees)) {
                throw std::invalid_argument("array.streamers headingDegrees, pitchDegrees, and rollDegrees must be finite");
            }
            config.array.streamers.push_back(item);
        }
    }
    if (array.contains("hydrophones")) {
        for (const auto& hydrophone : array.at("hydrophones")) {
            pamguard::core::ArrayHydrophone item;
            item.channel = hydrophone.at("channel").get<std::size_t>();
            item.x_m = hydrophone.value("xM", 0.0);
            item.y_m = hydrophone.value("yM", 0.0);
            item.z_m = hydrophone.value("zM", 0.0);
            item.sensitivity_db = hydrophone.value("sensitivityDb", 0.0);
            item.streamer_id = hydrophone.value("streamerId", 0);
            item.x_error_m = hydrophone.value("xErrorM", 0.0);
            item.y_error_m = hydrophone.value("yErrorM", 0.0);
            item.z_error_m = hydrophone.value("zErrorM", 0.0);
            item.preamp_gain_db = hydrophone.value("preampGainDb", 0.0);
            if (!std::isfinite(item.preamp_gain_db)) {
                throw std::invalid_argument("array.hydrophones preampGainDb must be finite");
            }
            if (item.x_error_m < 0.0 || item.y_error_m < 0.0 || item.z_error_m < 0.0 ||
                !std::isfinite(item.x_error_m) || !std::isfinite(item.y_error_m) || !std::isfinite(item.z_error_m)) {
                throw std::invalid_argument("array.hydrophones xErrorM, yErrorM, and zErrorM must be non-negative and finite");
            }
            config.array.hydrophones.push_back(item);
            if (!config.array.streamers.empty()) {
                const auto known = std::any_of(config.array.streamers.begin(), config.array.streamers.end(),
                                               [&](const auto& streamer) { return streamer.id == item.streamer_id; });
                if (!known) {
                    throw std::invalid_argument("array.hydrophones streamerId does not match any declared streamer");
                }
            }
        }
    }

    const auto fft = body.value("fft", json::object());
    config.detector.fft.fft_length = fft.value("length", config.detector.fft.fft_length);
    config.detector.fft.fft_hop = fft.value("hop", config.detector.fft.fft_hop);
    if (fft.contains("windowType")) {
        config.detector.fft.window_type = parse_window_type(fft.at("windowType"));
    }
    if (fft.contains("channels")) {
        config.detector.fft.channels = fft.at("channels").get<std::vector<std::size_t>>();
    }
    else {
        for (std::size_t channel = 0; channel < config.channel_count; ++channel) {
            config.detector.fft.channels.push_back(channel);
        }
    }
    if (config.detector.fft_noise.enabled) {
        for (const auto channel : config.detector.fft_noise.channels) {
            if (std::find(config.detector.fft.channels.begin(),
                          config.detector.fft.channels.end(), channel) ==
                config.detector.fft.channels.end()) {
                throw std::invalid_argument(
                    "fftNoise.channelBitmap must select only FFT source channels");
            }
        }
    }

    const auto click = body.value("click", json::object());
    config.detector.click_detector_enabled = click.value("enabled", false);
    config.detector.click_localisation_enabled = click.value("localisation", false);
    if (click.contains("angleVetoes")) {
        if (!click.at("angleVetoes").is_array()) {
            throw std::invalid_argument("click.angleVetoes must be an array");
        }
        for (const auto& item : click.at("angleVetoes")) {
            pamguard::detectors::ClickAngleVeto veto;
            veto.channels = item.value("channels", 0u);
            veto.start_angle_degrees =
                item.value("startAngleDegrees", 0.0);
            veto.end_angle_degrees =
                item.value("endAngleDegrees", 0.0);
            config.detector.click_angle_vetoes.push_back(veto);
        }
    }
    if (config.detector.click_detector_enabled) {
        config.detector.click.channel_bitmap = click.value("channelBitmap", channel_bitmap(config.channel_count));
        config.detector.click.trigger_bitmap = click.value("triggerBitmap", config.detector.click.channel_bitmap);
        config.detector.click.min_trigger_channels = click.value("minTriggerChannels", config.detector.click.min_trigger_channels);
        config.detector.click.threshold_db = click.value("thresholdDb", config.detector.click.threshold_db);
        config.detector.click.long_filter = click.value("longFilter", config.detector.click.long_filter);
        config.detector.click.long_filter_2 = click.value("longFilter2", config.detector.click.long_filter_2);
        config.detector.click.short_filter = click.value("shortFilter", config.detector.click.short_filter);
        config.detector.click.pre_sample = click.value("preSample", config.detector.click.pre_sample);
        config.detector.click.post_sample = click.value("postSample", config.detector.click.post_sample);
        config.detector.click.min_sep = click.value("minSep", config.detector.click.min_sep);
        config.detector.click.max_length = click.value("maxLength", config.detector.click.max_length);
        config.detector.click.publish_trigger_function =
            click.value("publishTriggerFunction",
                        config.detector.click.publish_trigger_function);
        const auto click_noise = click.value("noise", json::object());
        config.detector.click.sample_noise =
            click_noise.value("sampleWaveforms", config.detector.click.sample_noise);
        config.detector.click.noise_sample_interval_seconds =
            click_noise.value("waveformIntervalSeconds",
                              config.detector.click.noise_sample_interval_seconds);
        config.detector.click.store_background =
            click_noise.value("storeBackground", config.detector.click.store_background);
        config.detector.click.background_interval_milliseconds =
            click_noise.value("backgroundIntervalMilliseconds",
                              config.detector.click.background_interval_milliseconds);
        if (click.contains("delayMeasurement")) {
            const auto& delay = click.at("delayMeasurement");
            config.detector.click_delay_measurement =
                parse_delay_measurement(delay);
            if (delay.contains("typeSettings")) {
                if (!delay.at("typeSettings").is_array()) {
                    throw std::invalid_argument(
                        "click.delayMeasurement.typeSettings must be an array");
                }
                for (const auto& item : delay.at("typeSettings")) {
                    const int click_type = item.at("clickType").get<int>();
                    if (click_type <= 0 || click_type > 255) {
                        throw std::invalid_argument(
                            "click.delayMeasurement.typeSettings clickType must be 1..255");
                    }
                    config.detector.click_delay_measurement_by_type[click_type] =
                        parse_delay_measurement(
                            item, config.detector.click_delay_measurement);
                }
            }
        }

        if (click.contains("preFilter")) {
            config.detector.click.pre_filter = parse_iir_filter(click.at("preFilter"));
        }
        if (click.contains("triggerFilter")) {
            config.detector.click.trigger_filter = parse_iir_filter(click.at("triggerFilter"));
        }

        const auto grouping = click.value("groupingType", std::string("all"));
        if (grouping == "all") {
            config.detector.click_grouping_type =
                pamguard::core::DetectorConfig::ClickGroupingType::All;
        }
        else if (grouping == "singles") {
            config.detector.click_grouping_type =
                pamguard::core::DetectorConfig::ClickGroupingType::Singles;
        }
        else if (grouping == "user") {
            config.detector.click_grouping_type =
                pamguard::core::DetectorConfig::ClickGroupingType::User;
        }
        else {
            throw std::invalid_argument(
                "click.groupingType must be all, singles, or user");
        }
        config.detector.click_channel_groups.assign(config.channel_count, 0);
        if (config.detector.click_grouping_type ==
            pamguard::core::DetectorConfig::ClickGroupingType::Singles) {
            for (std::size_t channel = 0; channel < config.channel_count; ++channel) {
                config.detector.click_channel_groups[channel] =
                    static_cast<int>(channel);
            }
        }
        else if (config.detector.click_grouping_type ==
                 pamguard::core::DetectorConfig::ClickGroupingType::User) {
            if (!click.contains("channelGroups") ||
                !click.at("channelGroups").is_array() ||
                click.at("channelGroups").size() < config.channel_count) {
                throw std::invalid_argument(
                    "click.channelGroups must assign every channel for user grouping");
            }
            for (std::size_t channel = 0; channel < config.channel_count; ++channel) {
                const int group = click.at("channelGroups").at(channel).get<int>();
                if (group < 0 || group >= 32) {
                    throw std::invalid_argument(
                        "click.channelGroups values must be between 0 and 31");
                }
                config.detector.click_channel_groups[channel] = group;
            }
        }
        std::map<int, std::uint32_t> grouped_channels;
        for (std::size_t channel = 0;
             channel < std::min<std::size_t>(config.channel_count, 32);
             ++channel) {
            if ((config.detector.click.channel_bitmap & (1u << channel)) == 0) {
                continue;
            }
            grouped_channels[config.detector.click_channel_groups[channel]] |=
                static_cast<std::uint32_t>(1u << channel);
        }
        config.detector.click_groups.clear();
        for (const auto& [_, bitmap] : grouped_channels) {
            auto group = config.detector.click;
            group.channel_bitmap = bitmap;
            group.trigger_bitmap &= bitmap;
            config.detector.click_groups.push_back(std::move(group));
        }

        const auto echo = click.value("echo", json::object());
        config.detector.click_echo_enabled = echo.value("runOnline", false);
        config.detector.click_echo_discard = echo.value("discardEchoes", false);
        config.detector.click_echo_max_interval_seconds =
            echo.value("maxIntervalSeconds", config.detector.click_echo_max_interval_seconds);
        if (config.detector.click_echo_enabled &&
            (!(config.detector.click_echo_max_interval_seconds >= 0.0) ||
             !std::isfinite(config.detector.click_echo_max_interval_seconds))) {
            throw std::invalid_argument("click.echo.maxIntervalSeconds must be non-negative and finite");
        }

        config.detector.click_features_enabled = click.value("featuresEnabled", true);
        const auto features = click.value("features", json::object());
        config.detector.click_features.fft_length = features.value("fftLength", config.detector.click_features.fft_length);
        config.detector.click_features.length_energy_fraction = features.value("lengthEnergyFraction", config.detector.click_features.length_energy_fraction);
        config.detector.click_features.width_energy_fraction = features.value("widthEnergyFraction", config.detector.click_features.width_energy_fraction);
        if (features.contains("energyBandsHz")) {
            for (const auto& band : features.at("energyBandsHz")) {
                config.detector.click_features.energy_bands_hz.push_back(parse_frequency_range(band));
            }
        }
        if (features.contains("peakFrequencySearchHz")) {
            config.detector.click_features.peak_frequency_search_hz = parse_frequency_range(features.at("peakFrequencySearchHz"));
        }
        if (features.contains("meanFrequencyRangeHz")) {
            config.detector.click_features.mean_frequency_range_hz = parse_frequency_range(features.at("meanFrequencyRangeHz"));
        }
        if (config.detector.click_features.fft_length == 0) {
            config.detector.click_features.fft_length = config.detector.fft.fft_length;
        }

        const auto classifier = click.value("classifier", json::object());
        const auto classifier_type = classifier.value("type", std::string("sweep"));
        if (classifier_type == "basic") {
            config.detector.click_classifier_type =
                pamguard::core::DetectorConfig::ClickClassifierType::Basic;
        }
        else if (classifier_type == "sweep") {
            config.detector.click_classifier_type =
                pamguard::core::DetectorConfig::ClickClassifierType::Sweep;
        }
        else if (classifier_type == "none") {
            config.detector.click_classifier_type =
                pamguard::core::DetectorConfig::ClickClassifierType::None;
        }
        else {
            throw std::invalid_argument("click.classifier.type must be basic, sweep, or none");
        }
        config.detector.click_classify_online = classifier.value("runOnline", false);
        config.detector.click_discard_unclassified =
            classifier.value("discardUnclassifiedClicks", false);
        const auto basic_classifier = classifier.value("basic", json::object());
        config.detector.click_basic_classifier_enabled = basic_classifier.value("enabled", false);
        if (config.detector.click_basic_classifier_enabled && basic_classifier.contains("standardTypes")) {
            for (const auto& type : basic_classifier.at("standardTypes")) {
                config.detector.click_basic_classifier.click_types.push_back(parse_basic_click_standard_type(type));
            }
        }
        if (config.detector.click_basic_classifier_enabled && basic_classifier.contains("types")) {
            for (const auto& type : basic_classifier.at("types")) {
                config.detector.click_basic_classifier.click_types.push_back(parse_basic_click_type(type));
            }
        }
        const auto sweep_classifier = classifier.value("sweep", json::object());
        config.detector.click_sweep_classifier_enabled =
            sweep_classifier.value("enabled", false);
        config.detector.click_sweep_classifier.check_all_classifiers =
            sweep_classifier.value("checkAllClassifiers", false);
        if (sweep_classifier.contains("standardTypes")) {
            for (const auto& type : sweep_classifier.at("standardTypes")) {
                config.detector.click_sweep_classifier.click_types.push_back(
                    parse_sweep_click_standard_type(type));
            }
        }
        if (sweep_classifier.contains("types")) {
            for (const auto& type : sweep_classifier.at("types")) {
                config.detector.click_sweep_classifier.click_types.push_back(
                    parse_sweep_click_type(type));
            }
        }

        const auto click_train = click.value("train", json::object());
        config.detector.click_train_tracker_enabled = click_train.value("enabled", false);
        if (config.detector.click_train_tracker_enabled) {
            config.detector.click_train.min_ici_seconds =
                click_train.value(
                    "minIciSeconds",
                    config.detector.click_train.min_ici_seconds);
            config.detector.click_train.max_ici_seconds = click_train.value("maxIciSeconds", config.detector.click_train.max_ici_seconds);
            config.detector.click_train.min_clicks = click_train.value("minClicks", config.detector.click_train.min_clicks);
            const auto algorithm = click_train.value("algorithm", std::string("ici"));
            if (algorithm == "mht") {
                config.detector.click_train_mht = true;
            }
            else if (algorithm != "ici") {
                throw std::invalid_argument("click.train.algorithm must be \"ici\" or \"mht\"");
            }

            const auto classifier = click_train.value("classifier", json::object());
            config.detector.click_train_classifier_enabled = classifier.value("enabled", false);
            if (config.detector.click_train_classifier_enabled) {
                auto& pre = config.detector.click_train_pre_classifier;
                const auto pre_json = classifier.value("preClassifier", json::object());
                pre.chi2_threshold = pre_json.value("chi2Threshold", pre.chi2_threshold);
                pre.min_clicks = pre_json.value("minClicks", pre.min_clicks);
                pre.min_time_seconds = pre_json.value("minTimeSeconds", pre.min_time_seconds);
                pre.species_flag = pre_json.value("speciesFlag", pre.species_flag);

                const auto idi_json = classifier.value("idi", json::object());
                config.detector.click_train_idi_classifier_enabled = idi_json.value("enabled", false);
                auto& idi = config.detector.click_train_idi_classifier;
                idi.use_median_idi = idi_json.value("useMedianIdi", idi.use_median_idi);
                idi.min_median_idi = idi_json.value("minMedianIdi", idi.min_median_idi);
                idi.max_median_idi = idi_json.value("maxMedianIdi", idi.max_median_idi);
                idi.use_mean_idi = idi_json.value("useMeanIdi", idi.use_mean_idi);
                idi.min_mean_idi = idi_json.value("minMeanIdi", idi.min_mean_idi);
                idi.max_mean_idi = idi_json.value("maxMeanIdi", idi.max_mean_idi);
                idi.use_std_idi = idi_json.value("useStdIdi", idi.use_std_idi);
                idi.min_std_idi = idi_json.value("minStdIdi", idi.min_std_idi);
                idi.max_std_idi = idi_json.value("maxStdIdi", idi.max_std_idi);
                idi.species_flag = idi_json.value("speciesFlag", idi.species_flag);

                const auto bearing_json = classifier.value("bearing", json::object());
                config.detector.click_train_bearing_classifier_enabled = bearing_json.value("enabled", false);
                if (config.detector.click_train_bearing_classifier_enabled) {
                    auto& bearing = config.detector.click_train_bearing_classifier;
                    constexpr double deg = 3.141592653589793238462643383279502884 / 180.0;
                    // Angles are configured in degrees; PAMGuard stores radians.
                    bearing.bearing_lim_min = bearing_json.value("bearingLimMinDegrees", 85.0) * deg;
                    bearing.bearing_lim_max = bearing_json.value("bearingLimMaxDegrees", 95.0) * deg;
                    bearing.use_mean = bearing_json.value("useMean", bearing.use_mean);
                    bearing.min_mean_bearing_derivative = bearing_json.value("minMeanBearingDerivativeDegrees", -0.005) * deg;
                    bearing.max_mean_bearing_derivative = bearing_json.value("maxMeanBearingDerivativeDegrees", 0.005) * deg;
                    bearing.use_median = bearing_json.value("useMedian", bearing.use_median);
                    bearing.min_median_bearing_derivative = bearing_json.value("minMedianBearingDerivativeDegrees", -0.005) * deg;
                    bearing.max_median_bearing_derivative = bearing_json.value("maxMedianBearingDerivativeDegrees", 0.005) * deg;
                    bearing.use_std = bearing_json.value("useStd", bearing.use_std);
                    bearing.min_std_bearing_derivative = bearing_json.value("minStdBearingDerivativeDegrees", 0.0) * deg;
                    bearing.max_std_bearing_derivative = bearing_json.value("maxStdBearingDerivativeDegrees", 1.5) * deg;
                    bearing.species_flag = bearing_json.value("speciesFlag", bearing.species_flag);
                    if (!config.detector.click_localisation_enabled) {
                        throw std::invalid_argument("click.train.classifier.bearing requires click.localisation to be enabled");
                    }
                }

                const auto template_json = classifier.value("template", json::object());
                config.detector.click_train_template_classifier_enabled = template_json.value("enabled", false);
                if (config.detector.click_train_template_classifier_enabled) {
                    auto& tmpl = config.detector.click_train_template_classifier;
                    const auto preset = template_json.value("preset", std::string());
                    if (!preset.empty()) {
                        bool matched = false;
                        for (const auto& candidate : pamguard::detectors::ct_default_spectrum_templates()) {
                            if (candidate.name == preset) {
                                tmpl.template_spectrum = candidate.values;
                                tmpl.template_sample_rate_hz = candidate.sample_rate_hz;
                                matched = true;
                                break;
                            }
                        }
                        if (!matched) {
                            throw std::invalid_argument("click.train.classifier.template.preset is not a known PAMGuard template");
                        }
                    }
                    else if (template_json.contains("spectrum")) {
                        tmpl.template_spectrum = template_json.at("spectrum").get<std::vector<double>>();
                        tmpl.template_sample_rate_hz = template_json.value("sampleRateHz", tmpl.template_sample_rate_hz);
                    }
                    tmpl.correlation_threshold = template_json.value("correlationThreshold", tmpl.correlation_threshold);
                    tmpl.species_flag = template_json.value("speciesFlag", tmpl.species_flag);
                    if (tmpl.template_spectrum.size() < 2 || tmpl.template_sample_rate_hz <= 0.0) {
                        throw std::invalid_argument("click.train.classifier.template needs a preset or a spectrum with a positive sampleRateHz");
                    }
                }
            }

            if (config.detector.click_train_mht && click_train.contains("mht")) {
                const auto& mht = click_train.at("mht");
                auto& chi2 = config.detector.click_train_mht_chi2;
                auto& kernel = config.detector.click_train_mht_kernel;
                chi2.enable_idi = mht.value("enableIdi", chi2.enable_idi);
                chi2.enable_amplitude = mht.value("enableAmplitude", chi2.enable_amplitude);
                chi2.enable_length = mht.value("enableLength", chi2.enable_length);
                chi2.enable_bearing = mht.value("enableBearing", chi2.enable_bearing);
                chi2.enable_peak_frequency = mht.value("enablePeakFrequency", chi2.enable_peak_frequency);
                chi2.enable_time_delay = mht.value("enableTimeDelay", chi2.enable_time_delay);
                chi2.enable_correlation = mht.value("enableCorrelation", chi2.enable_correlation);
                chi2.coast_penalty = mht.value("coastPenalty", chi2.coast_penalty);
                chi2.new_track_penalty = mht.value("newTrackPenalty", chi2.new_track_penalty);
                chi2.new_track_n = mht.value("newTrackN", chi2.new_track_n);
                chi2.max_ici = mht.value("maxIci", chi2.max_ici);
                chi2.low_ici_exponent = mht.value("lowIciExponent", chi2.low_ici_exponent);
                chi2.long_track_exponent = mht.value("longTrackExponent", chi2.long_track_exponent);
                chi2.use_electrical_noise_filter = mht.value("useElectricalNoiseFilter", chi2.use_electrical_noise_filter);
                chi2.electrical_noise_min_chi2 = mht.value("electricalNoiseMinChi2", chi2.electrical_noise_min_chi2);
                chi2.electrical_noise_n_data_units = mht.value("electricalNoiseNDataUnits", chi2.electrical_noise_n_data_units);
                kernel.n_hold = mht.value("nHold", kernel.n_hold);
                kernel.n_pruneback = mht.value("nPruneback", kernel.n_pruneback);
                kernel.n_pruneback_start = mht.value("nPrunebackStart", kernel.n_pruneback_start);
                kernel.max_coast = mht.value("maxCoast", kernel.max_coast);

                if (!(chi2.enable_idi || chi2.enable_amplitude || chi2.enable_length ||
                      chi2.enable_bearing || chi2.enable_peak_frequency ||
                      chi2.enable_time_delay || chi2.enable_correlation)) {
                    throw std::invalid_argument("click.train.mht must enable at least one chi2 variable");
                }
                if (chi2.max_ici <= 0.0 || !std::isfinite(chi2.max_ici) ||
                    chi2.coast_penalty < 0.0 || !std::isfinite(chi2.coast_penalty) ||
                    chi2.new_track_penalty < 0.0 || !std::isfinite(chi2.new_track_penalty)) {
                    throw std::invalid_argument("click.train.mht penalties must be non-negative and maxIci positive");
                }
                if (kernel.n_hold == 0 || kernel.n_pruneback == 0 || kernel.max_coast <= 0) {
                    throw std::invalid_argument("click.train.mht nHold, nPruneback, and maxCoast must be positive");
                }
            }
        }
    }

    const auto whistle = body.value("whistle", json::object());
    config.detector.whistle_peak_detector_enabled = whistle.value("enabled", false);
    config.detector.whistle_region_detector_enabled = whistle.value("regionEnabled", false);
    if (config.detector.whistle_peak_detector_enabled) {
        config.detector.whistle_peak.detection_threshold_db = whistle.value("detectionThresholdDb", config.detector.whistle_peak.detection_threshold_db);
        config.detector.whistle_peak.peak_time_constant_0 = whistle.value("peakTimeConstant0", config.detector.whistle_peak.peak_time_constant_0);
        config.detector.whistle_peak.peak_time_constant_1 = whistle.value("peakTimeConstant1", config.detector.whistle_peak.peak_time_constant_1);
        config.detector.whistle_peak.max_percent_over_threshold = whistle.value("maxPercentOverThreshold", config.detector.whistle_peak.max_percent_over_threshold);
        config.detector.whistle_peak.min_peak_width = whistle.value("minPeakWidth", config.detector.whistle_peak.min_peak_width);
        config.detector.whistle_peak.max_peak_width = whistle.value("maxPeakWidth", config.detector.whistle_peak.max_peak_width);
        config.detector.whistle_peak.search_bin0 = whistle.value("searchBin0", config.detector.whistle_peak.search_bin0);
        config.detector.whistle_peak.search_bin1 = whistle.value("searchBin1", config.detector.whistle_peak.search_bin1);
        config.detector.whistle_peak.warmup_slices = whistle.value("warmupSlices", config.detector.whistle_peak.warmup_slices);
    }
    {
        const auto noise = whistle.value("noise", json::object());
        auto& noise_config = config.detector.whistle_noise;
        noise_config.run_median_filter = noise.value("medianFilter", false);
        noise_config.median_filter_length = noise.value("medianFilterLength", noise_config.median_filter_length);
        noise_config.run_average_subtraction = noise.value("averageSubtraction", false);
        noise_config.average_update_constant = noise.value("updateConstant", noise_config.average_update_constant);
        noise_config.run_kernel_smoothing = noise.value("kernelSmoothing", false);
        noise_config.run_threshold = noise.value("threshold", false);
        noise_config.threshold_db = noise.value("thresholdDb", noise_config.threshold_db);
        noise_config.threshold_final_output = noise.value("finalOutput", noise_config.threshold_final_output);
        if (noise_config.run_median_filter && noise_config.median_filter_length <= 0) {
            throw std::invalid_argument("whistle.noise.medianFilterLength must be positive");
        }
        if (noise_config.run_average_subtraction &&
            (!(noise_config.average_update_constant > 0.0) || noise_config.average_update_constant >= 1.0)) {
            throw std::invalid_argument("whistle.noise.updateConstant must be in (0, 1)");
        }
        if (noise_config.run_threshold &&
            (!std::isfinite(noise_config.threshold_db) || noise_config.threshold_final_output < 0 ||
             noise_config.threshold_final_output > 2)) {
            throw std::invalid_argument("whistle.noise.thresholdDb must be finite and finalOutput 0..2");
        }
    }
    if (config.detector.whistle_region_detector_enabled) {
        config.detector.whistle_region.min_frequency_hz =
            whistle.value("minFrequencyHz", config.detector.whistle_region.min_frequency_hz);
        config.detector.whistle_region.max_frequency_hz =
            whistle.value("maxFrequencyHz", config.detector.whistle_region.max_frequency_hz);
        config.detector.whistle_region.background_interval_seconds =
            whistle.value("backgroundIntervalSeconds",
                          config.detector.whistle_region.background_interval_seconds);
        config.detector.whistle_region.min_pixels = whistle.value("minPixels", config.detector.whistle_region.min_pixels);
        config.detector.whistle_region.min_length = whistle.value("minLength", config.detector.whistle_region.min_length);
        config.detector.whistle_region.connect_type = whistle.value("connectType", config.detector.whistle_region.connect_type);
        config.detector.whistle_region.keep_shape_stubs = whistle.value("keepShapeStubs", config.detector.whistle_region.keep_shape_stubs);
        config.detector.whistle_region.fragmentation_method = whistle.value("fragmentationMethod", config.detector.whistle_region.fragmentation_method);
        config.detector.whistle_region.max_cross_length = whistle.value("maxCrossLength", config.detector.whistle_region.max_cross_length);
        config.detector.whistle_region.reject_first_quarter_second = whistle.value("rejectFirstQuarterSecond", config.detector.whistle_region.reject_first_quarter_second);

        std::uint32_t fft_channel_bitmap = 0;
        for (const auto channel : config.detector.fft.channels) {
            if (channel < 32) {
                fft_channel_bitmap |= std::uint32_t{1} << channel;
            }
        }
        config.detector.whistle_channel_bitmap =
            whistle.value("channelBitmap", fft_channel_bitmap);
        if (config.detector.whistle_channel_bitmap == 0 ||
            (config.detector.whistle_channel_bitmap & ~fft_channel_bitmap) !=
                0) {
            throw std::invalid_argument(
                "whistle.channelBitmap must select one or more FFT source channels");
        }

        const auto grouping =
            whistle.value("groupingType", std::string("all"));
        if (grouping == "all") {
            config.detector.whistle_grouping_type =
                pamguard::core::DetectorConfig::ClickGroupingType::All;
        }
        else if (grouping == "singles") {
            config.detector.whistle_grouping_type =
                pamguard::core::DetectorConfig::ClickGroupingType::Singles;
        }
        else if (grouping == "user") {
            config.detector.whistle_grouping_type =
                pamguard::core::DetectorConfig::ClickGroupingType::User;
        }
        else {
            throw std::invalid_argument(
                "whistle.groupingType must be all, singles, or user");
        }
        config.detector.whistle_channel_groups.assign(
            config.channel_count, 0);
        if (config.detector.whistle_grouping_type ==
            pamguard::core::DetectorConfig::ClickGroupingType::Singles) {
            for (std::size_t channel = 0; channel < config.channel_count;
                 ++channel) {
                config.detector.whistle_channel_groups[channel] =
                    static_cast<int>(channel);
            }
        }
        else if (config.detector.whistle_grouping_type ==
                 pamguard::core::DetectorConfig::ClickGroupingType::User) {
            if (!whistle.contains("channelGroups") ||
                !whistle.at("channelGroups").is_array() ||
                whistle.at("channelGroups").size() < config.channel_count) {
                throw std::invalid_argument(
                    "whistle.channelGroups must assign every channel for user grouping");
            }
            for (std::size_t channel = 0; channel < config.channel_count;
                 ++channel) {
                const int group =
                    whistle.at("channelGroups").at(channel).get<int>();
                if (group < 0 || group >= 32) {
                    throw std::invalid_argument(
                        "whistle.channelGroups values must be between 0 and 31");
                }
                config.detector.whistle_channel_groups[channel] = group;
            }
        }
    }

    validate_analysis_config(config);
    return config;
}

json world_vectors_to_json(const std::vector<pamguard::localisation::WorldVector>& vectors) {
    json out = json::array();
    for (const auto& world : vectors) {
        out.push_back({
            {"x", world.direction[0]},
            {"y", world.direction[1]},
            {"z", world.direction[2]},
            {"cone", world.cone},
        });
    }
    return out;
}

/**
 * PAMGuard MLGridBearingLocaliser2 output. Theta and phi are the reference's
 * own angles in the sub-array's principal axis frame, so they keep those names
 * rather than being presented as compass azimuth and elevation.
 */
json grid_bearing_to_json(const pamguard::core::GridBearingResult& grid) {
    constexpr double kRadiansToDegrees = 180.0 / 3.141592653589793238462643383279502884;
    json item = {
        {"thetaRadians", grid.theta_radians},
        {"thetaDegrees", grid.theta_radians * kRadiansToDegrees},
        {"usedPairs", grid.used_pairs},
        {"hasPhi", grid.has_phi},
    };
    if (std::isfinite(grid.theta_error_radians)) {
        item["thetaErrorRadians"] = grid.theta_error_radians;
    }
    if (!grid.world_vectors.empty()) {
        item["worldVectors"] = world_vectors_to_json(grid.world_vectors);
    }
    if (!grid.earth_world_vectors.empty()) {
        item["earthWorldVectors"] = world_vectors_to_json(grid.earth_world_vectors);
    }
    if (grid.has_phi) {
        item["phiRadians"] = grid.phi_radians;
        item["phiDegrees"] = grid.phi_radians * kRadiansToDegrees;
        if (std::isfinite(grid.phi_error_radians)) {
            item["phiErrorRadians"] = grid.phi_error_radians;
        }
    }
    return item;
}

json result_to_json(const pamguard::core::AnalysisResult& result, const ResultJsonOptions& options = {}) {
    json out;
    out["schemaVersion"] = kResultSchemaVersion;
    out["spectrogramFrames"] = result.spectrogram_frames.size();
    const bool include_frequency_hz = options.sample_rate_hz > 0 && options.fft_length > 0;
    const auto train_ids_by_sample = click_train_ids_by_sample(result.click_trains);
    auto bin_value_to_hz = [&](double bin) {
        return bin * static_cast<double>(options.sample_rate_hz) / static_cast<double>(options.fft_length);
    };
    auto bin_to_hz = [&](std::size_t bin) {
        return bin_value_to_hz(static_cast<double>(bin));
    };

    if (options.include_spectrogram) {
        out["spectrogram"] = json::array();
        for (const auto& frame : result.spectrogram_frames) {
            const auto magnitude_squared = spectrogram_magnitude_squared(frame.bins);
            json item;
            item["channel"] = frame.channel;
            item["startSample"] = frame.start_sample;
            item["timeMs"] = frame.time_unix_ms;
            item["slice"] = frame.fft_slice;
            item["binStride"] = std::max<std::size_t>(1, options.spectrogram_bin_stride);
            item["magnitudeSquared"] = sampled_bins(magnitude_squared, options.spectrogram_bin_stride, options.spectrogram_max_bins);
            if (options.include_spectrogram_complex) {
                item["complexBins"] = json::array();
                const auto limit = options.spectrogram_max_bins == 0 ? frame.bins.size() : std::min(options.spectrogram_max_bins, frame.bins.size());
                const auto stride = std::max<std::size_t>(1, options.spectrogram_bin_stride);
                for (std::size_t i = 0; i < frame.bins.size() && item["complexBins"].size() < limit; i += stride) {
                    item["complexBins"].push_back({{"real", frame.bins[i].real()}, {"imag", frame.bins[i].imag()}});
                }
            }
            out["spectrogram"].push_back(std::move(item));
        }
    }

    out["clicks"] = json::array();
    for (const auto& click : result.clicks) {
        auto matched_template_annotations = json::array();
        for (const auto& annotation :
             click.matched_template_annotations) {
            auto best_results = json::array();
            for (const auto& match : annotation.best_results) {
                json match_result = {
                    {"threshold", match.threshold},
                    {
                        "matchCorrelation",
                        match.match_correlation,
                    },
                };
                if (std::isfinite(
                        match.reject_correlation)) {
                    match_result["rejectCorrelation"] =
                        match.reject_correlation;
                }
                best_results.push_back(
                    std::move(match_result));
            }
            matched_template_annotations.push_back({
                {
                    "classifierInstanceId",
                    annotation.classifier_instance_id,
                },
                {"clickType", annotation.click_type},
                {"classified", annotation.classified},
                {"bestResults", std::move(best_results)},
            });
        }
        json item = {
            {"startSample", click.start_sample},
            {"durationSamples", click.duration_samples},
            {"timeMs", click.time_unix_ms},
            {"triggerBitmap", click.trigger_bitmap},
            {"signalExcessDb", click.signal_excess_db},
            {"clickType", click.click_type},
            {"classifiersPassed", click.classifiers_passed},
            {"delaysInSamples", click.delays_in_samples},
            {
                "matchedTemplateAnnotations",
                std::move(matched_template_annotations),
            },
            {"waveformChannels", click.waveform.size()},
            {"waveformSamples", click.waveform.empty() ? 0 : click.waveform.front().size()},
        };
        item["bearingRadians"] = click.bearing_radians.has_value()
            ? json(*click.bearing_radians)
            : json(nullptr);
        if (options.echo_detection_running) {
            item["echo"] = click.echo;
        }
        if (options.include_click_waveforms) {
            item["channels"] = click.channels;
            item["waveform"] = click.waveform;
        }
        attach_related_train_ids(item, click.start_sample, train_ids_by_sample);
        out["clicks"].push_back(std::move(item));
    }

    out["clickNoiseSamples"] = json::array();
    for (const auto& noise : result.click_noise_samples) {
        json item = {
            {"channelBitmap", noise.channel_bitmap},
            {"startSample", noise.start_sample},
            {"durationSamples", noise.duration_samples},
            {"timeMs", noise.time_unix_ms},
            {"channels", noise.channels},
            {"waveformChannels", noise.waveform.size()},
            {"waveformSamples",
             noise.waveform.empty() ? 0 : noise.waveform.front().size()},
        };
        if (options.include_click_waveforms) {
            item["waveform"] = noise.waveform;
        }
        out["clickNoiseSamples"].push_back(std::move(item));
    }
    out["clickTriggerBackground"] = json::array();
    for (const auto& background : result.click_trigger_background) {
        out["clickTriggerBackground"].push_back({
            {"channelBitmap", background.channel_bitmap},
            {"timeMs", background.time_unix_ms},
            {"channels", background.channels},
            {"values", background.values},
        });
    }
    out["clickTriggerFunction"] = json::array();
    for (const auto& trigger : result.click_trigger_function) {
        out["clickTriggerFunction"].push_back({
            {"channelBitmap", trigger.channel_bitmap},
            {"startSample", trigger.start_sample},
            {"timeMs", trigger.time_unix_ms},
            {"channels", trigger.channels},
            {"signalExcessDb", trigger.signal_excess_db},
            {"longFilterValues", trigger.long_filter_values},
        });
    }

    out["clickLocalisations"] = json::array();
    const bool include_delay_seconds = options.sample_rate_hz > 0;
    const bool include_path_difference_m = include_delay_seconds && options.speed_of_sound_mps > 0.0;
    for (const auto& localisation : result.click_localisations) {
        json loc;
        loc["clickIndex"] = localisation.click_index;
        loc["clickStartSample"] = localisation.click_start_sample;
        loc["delays"] = json::array();
        for (const auto& delay : localisation.delays) {
            json delay_item = {
                {"pairIndex", delay.pair_index},
                {"channelA", delay.channel_a},
                {"channelB", delay.channel_b},
                {"audioChannelA", delay.audio_channel_a},
                {"audioChannelB", delay.audio_channel_b},
                {"delaySamples", delay.delay.delay_samples},
                {"delayScore", delay.delay.delay_score},
            };
            if (include_delay_seconds) {
                const double delay_seconds = delay.delay.delay_samples / static_cast<double>(options.sample_rate_hz);
                delay_item["delaySeconds"] = delay_seconds;
                if (include_path_difference_m) {
                    delay_item["pathDifferenceM"] = delay_seconds * options.speed_of_sound_mps;
                }
            }
            delay_item["geometryConstrained"] = delay.geometry_constrained;
            if (delay.geometry_constrained) {
                delay_item["maxDelaySamples"] = delay.max_delay_samples;
                delay_item["hydrophoneDistanceM"] = delay.hydrophone_distance_m;
                if (include_delay_seconds) {
                    delay_item["maxDelaySeconds"] = delay.max_delay_samples / static_cast<double>(options.sample_rate_hz);
                }
            }
            if (delay.pair_bearing_valid && std::isfinite(delay.pair_bearing_radians)) {
                delay_item["pairBearingRadians"] = delay.pair_bearing_radians;
                delay_item["pairBearingDegrees"] = delay.pair_bearing_radians * 180.0 / 3.141592653589793238462643383279502884;
                if (std::isfinite(delay.pair_bearing_error_radians)) {
                    delay_item["pairBearingErrorRadians"] = delay.pair_bearing_error_radians;
                }
                if (!delay.pair_bearing_world_vectors.empty()) {
                    delay_item["pairBearingWorldVectors"] = world_vectors_to_json(delay.pair_bearing_world_vectors);
                }
                if (!delay.pair_bearing_earth_world_vectors.empty()) {
                    delay_item["pairBearingEarthWorldVectors"] =
                        world_vectors_to_json(delay.pair_bearing_earth_world_vectors);
                }
            }
            loc["delays"].push_back(std::move(delay_item));
        }
        if (localisation.lsq_bearing.valid) {
            json lsq_item = {
                {"azimuthRadians", localisation.lsq_bearing.azimuth_radians},
                {"azimuthDegrees", localisation.lsq_bearing.azimuth_radians * 180.0 / 3.141592653589793238462643383279502884},
                {"elevationRadians", localisation.lsq_bearing.elevation_radians},
                {"elevationDegrees", localisation.lsq_bearing.elevation_radians * 180.0 / 3.141592653589793238462643383279502884},
                {"usedPairs", localisation.lsq_bearing.used_pairs},
            };
            if (std::isfinite(localisation.lsq_bearing.azimuth_error_radians)) {
                lsq_item["azimuthErrorRadians"] = localisation.lsq_bearing.azimuth_error_radians;
            }
            if (std::isfinite(localisation.lsq_bearing.elevation_error_radians)) {
                lsq_item["elevationErrorRadians"] = localisation.lsq_bearing.elevation_error_radians;
            }
            if (!localisation.lsq_bearing.world_vectors.empty()) {
                lsq_item["worldVectors"] = world_vectors_to_json(localisation.lsq_bearing.world_vectors);
            }
            if (!localisation.lsq_bearing.earth_world_vectors.empty()) {
                lsq_item["earthWorldVectors"] = world_vectors_to_json(localisation.lsq_bearing.earth_world_vectors);
            }
            loc["lsqBearing"] = std::move(lsq_item);
        }
        if (localisation.grid_bearing.valid) {
            loc["gridBearing"] = grid_bearing_to_json(localisation.grid_bearing);
        }
        loc["arrayShape"] = std::string(pamguard::localisation::array_shape_name(localisation.array_shape));
        loc["bearingLocaliser"] = std::string(pamguard::localisation::bearing_localiser_name(localisation.bearing_localiser));
        attach_related_train_ids(loc, localisation.click_start_sample, train_ids_by_sample);
        out["clickLocalisations"].push_back(std::move(loc));
    }

    out["clickTrainLocalisations"] = json::array();
    for (const auto& train_localisation : result.click_train_localisations) {
        json item = {
            {"trainId", train_localisation.train_id},
            {"channelBitmap", train_localisation.channel_bitmap},
            {"firstStartSample", train_localisation.first_start_sample},
            {"lastStartSample", train_localisation.last_start_sample},
            {"clickCount", train_localisation.click_count},
            {"localisationCount", train_localisation.localisation_count},
            {"valid", train_localisation.valid},
        };
        item["pairDelays"] = json::array();
        for (const auto& pair : train_localisation.pair_delays) {
            json pair_item = {
                {"pairIndex", pair.pair_index},
                {"channelA", pair.channel_a},
                {"channelB", pair.channel_b},
                {"audioChannelA", pair.audio_channel_a},
                {"audioChannelB", pair.audio_channel_b},
                {"geometryConstrained", pair.geometry_constrained},
                {"delayCount", pair.delay_count},
                {"meanDelaySamples", pair.mean_delay_samples},
                {"meanDelayScore", pair.mean_delay_score},
            };
            if (pair.geometry_constrained) {
                pair_item["maxDelaySamples"] = pair.max_delay_samples;
                pair_item["hydrophoneDistanceM"] = pair.hydrophone_distance_m;
            }
            if (pair.pair_bearing_count > 0) {
                pair_item["pairBearingCount"] = pair.pair_bearing_count;
                pair_item["meanPairBearingRadians"] = pair.mean_pair_bearing_radians;
                pair_item["meanPairBearingDegrees"] = pair.mean_pair_bearing_radians * 180.0 / 3.141592653589793238462643383279502884;
            }
            if (include_delay_seconds) {
                const double mean_delay_seconds = pair.mean_delay_samples / static_cast<double>(options.sample_rate_hz);
                pair_item["meanDelaySeconds"] = mean_delay_seconds;
                if (include_path_difference_m) {
                    pair_item["meanPathDifferenceM"] = mean_delay_seconds * options.speed_of_sound_mps;
                }
                if (pair.geometry_constrained) {
                    pair_item["maxDelaySeconds"] = pair.max_delay_samples / static_cast<double>(options.sample_rate_hz);
                }
            }
            item["pairDelays"].push_back(std::move(pair_item));
        }
        out["clickTrainLocalisations"].push_back(std::move(item));
    }

    out["clickBearings"] = json::array();
    for (const auto& bearing : result.click_bearings) {
        json item = {
            {"clickIndex", bearing.click_index},
            {"clickStartSample", bearing.click_start_sample},
            {"valid", bearing.bearing.valid},
            {"unit", {bearing.bearing.unit_x, bearing.bearing.unit_y, bearing.bearing.unit_z}},
            {"azimuthDegrees", bearing.bearing.azimuth_degrees},
            {"elevationDegrees", bearing.bearing.elevation_degrees},
            {"residualRmsSeconds", bearing.bearing.residual_rms_seconds},
            {"usedPairs", bearing.bearing.used_pairs},
        };
        attach_related_train_ids(item, bearing.click_start_sample, train_ids_by_sample);
        out["clickBearings"].push_back(std::move(item));
    }

    out["clickTrainBearings"] = json::array();
    for (const auto& train_bearing : result.click_train_bearings) {
        out["clickTrainBearings"].push_back({
            {"trainId", train_bearing.train_id},
            {"channelBitmap", train_bearing.channel_bitmap},
            {"firstStartSample", train_bearing.first_start_sample},
            {"lastStartSample", train_bearing.last_start_sample},
            {"clickCount", train_bearing.click_count},
            {"bearingCount", train_bearing.bearing_count},
            {"valid", train_bearing.valid},
            {"unit", {train_bearing.unit_x, train_bearing.unit_y, train_bearing.unit_z}},
            {"azimuthDegrees", train_bearing.azimuth_degrees},
            {"elevationDegrees", train_bearing.elevation_degrees},
            {"meanResidualRmsSeconds", train_bearing.mean_residual_rms_seconds},
        });
    }

    out["clickFeatures"] = json::array();
    for (const auto& feature : result.click_features) {
        json item;
        item["clickIndex"] = feature.click_index;
        item["clickStartSample"] = feature.click_start_sample;
        item["fftLength"] = feature.fft_length;
        item["clickLengthSeconds"] = feature.click_length_seconds;
        item["peakFrequencyHz"] = feature.peak_frequency_hz;
        item["peakWidthHz"] = feature.peak_width_hz;
        item["meanFrequencyHz"] = feature.mean_frequency_hz;
        item["bandEnergyDb"] = feature.band_energy_db;
        item["totalPowerSpectrumBins"] = feature.total_power_spectrum.size();
        if (options.include_click_spectra) {
            item["totalPowerSpectrum"] = feature.total_power_spectrum;
        }
        attach_related_train_ids(item, feature.click_start_sample, train_ids_by_sample);
        item["channels"] = json::array();
        for (const auto& channel : feature.channels) {
            json channel_item = {
                {"channel", channel.channel},
                {"lengthSeconds", channel.length_seconds},
                {"powerSpectrumBins", channel.power_spectrum.size()},
            };
            if (options.include_click_spectra) {
                channel_item["powerSpectrum"] = channel.power_spectrum;
            }
            item["channels"].push_back(std::move(channel_item));
        }
        out["clickFeatures"].push_back(std::move(item));
    }

    out["clickClassifications"] = json::array();
    for (const auto& classification : result.click_classifications) {
        json item = {
            {"clickIndex", classification.click_index},
            {"clickStartSample", classification.click_start_sample},
            {"clickType", classification.click_type},
            {"discard", classification.discard},
        };
        if (!classification.classifiers_passed.empty()) {
            item["classifiersPassed"] = classification.classifiers_passed;
        }
        attach_related_train_ids(item, classification.click_start_sample, train_ids_by_sample);
        out["clickClassifications"].push_back(std::move(item));
    }

    out["clickTrains"] = json::array();
    for (const auto& train : result.click_trains) {
        out["clickTrains"].push_back({
            {"trainId", train.train_id},
            {"channelBitmap", train.channel_bitmap},
            {"firstStartSample", train.first_start_sample},
            {"lastStartSample", train.last_start_sample},
            {"firstTimeMs", train.first_time_ms},
            {"lastTimeMs", train.last_time_ms},
            {"clickStartSamples", train.click_start_samples},
            {"clickTimeMs", train.click_time_ms},
            {"clickCount", train.click_count},
            {"durationSamples", train.duration_samples},
            {"durationSeconds", train.duration_seconds},
            {"timeSpanSeconds", train.time_span_seconds},
            {"lastIciSeconds", train.last_ici_seconds},
            {"minIciSeconds", train.min_ici_seconds},
            {"maxIciSeconds", train.max_ici_seconds},
            {"meanIciSeconds", train.mean_ici_seconds},
            {"medianIciSeconds", train.median_ici_seconds},
            {"stdIciSeconds", train.std_ici_seconds},
            {"iciCv", train.ici_cv},
            {"clickRateHz", train.click_rate_hz},
            {"completed", train.completed},
        });
    }

    out["clickTrainClassifications"] = json::array();
    for (const auto& classification : result.click_train_classifications) {
        json item = {
            {"trainId", classification.train_id},
            {"junkTrain", classification.junk_train},
            {"speciesId", classification.species_id},
            {"classifierSpeciesIds", classification.classifier_species_ids},
        };
        if (classification.template_correlation != 0.0) {
            item["templateCorrelation"] = classification.template_correlation;
        }
        out["clickTrainClassifications"].push_back(std::move(item));
    }

    out["mhtClickTrains"] = json::array();
    for (const auto& train : result.mht_click_trains) {
        json item = {
            {"trainId", train.train_id},
            {"channelBitmap", train.channel_bitmap},
            {"chi2", train.chi2},
            {"clickCount", train.click_count},
            {"firstStartSample", train.first_start_sample},
            {"lastStartSample", train.last_start_sample},
            {"clickStartSamples", train.click_start_samples},
            {"clickTimeMs", train.click_time_ms},
        };
        if (train.classified) {
            json classification = {
                {"junkTrain", train.junk_train},
                {"speciesId", train.species_id},
                {"classifierSpeciesIds", train.classifier_species_ids},
            };
            if (train.template_correlation != 0.0) {
                classification["templateCorrelation"] = train.template_correlation;
            }
            item["classification"] = std::move(classification);
        }
        out["mhtClickTrains"].push_back(std::move(item));
    }

    out["whistlePeaks"] = json::array();
    for (const auto& peak : result.whistle_peaks) {
        json item = {
            {"channel", peak.channel},
            {"startSample", peak.start_sample},
            {"slice", peak.slice_number},
            {"minFreq", peak.min_freq},
            {"peakFreq", peak.peak_freq},
            {"maxFreq", peak.max_freq},
            {"maxAmp", peak.max_amp},
            {"signal", peak.signal},
            {"noise", peak.noise},
        };
        if (include_frequency_hz) {
            item["minFreqHz"] = bin_to_hz(peak.min_freq);
            item["peakFreqHz"] = bin_to_hz(peak.peak_freq);
            item["maxFreqHz"] = bin_to_hz(peak.max_freq);
        }
        out["whistlePeaks"].push_back(std::move(item));
    }

    out["whistleRegions"] = json::array();
    for (const auto& region : result.whistle_regions) {
        json contour_points = json::array();
        for (const auto& slice : region.slices) {
            for (const auto& peak : slice.peak_info) {
                json point = {
                    {"slice", slice.slice_number},
                    {"startSample", slice.start_sample},
                    {"timeMs", slice.time_ms},
                    {"minBin", peak[0]},
                    {"peakBin", peak[1]},
                    {"maxBin", peak[2]},
                };
                if (include_frequency_hz) {
                    point["minHz"] = bin_to_hz(static_cast<std::size_t>(peak[0]));
                    point["peakHz"] = bin_to_hz(static_cast<std::size_t>(peak[1]));
                    point["maxHz"] = bin_to_hz(static_cast<std::size_t>(peak[2]));
                }
                contour_points.push_back(std::move(point));
            }
        }

        json item = {
            {"channel", region.channel},
            {"regionNumber", region.region_number},
            {"firstSlice", region.first_slice},
            {"startSample", region.start_sample},
            {"lastStartSample", region.last_start_sample},
            {"timeMs", region.time_ms},
            {"timeSpanSamples", region.time_span_samples},
            {"durationSamples", region.duration_samples},
            {"timeSpanMs", region.time_span_ms},
            {"timeSpanSeconds", region.time_span_seconds},
            {"durationSeconds", region.duration_seconds},
            {"totalPixels", region.total_pixels},
            {"minFrequencyBin", region.min_frequency_bin},
            {"maxFrequencyBin", region.max_frequency_bin},
            {"frequencySpanBins", region.frequency_span_bins},
            {"minPeakBin", region.min_peak_bin},
            {"maxPeakBin", region.max_peak_bin},
            {"meanPeakBin", region.mean_peak_bin},
            {"startPeakBin", region.start_peak_bin},
            {"endPeakBin", region.end_peak_bin},
            {"peakSweepRateBinsPerSecond", region.peak_sweep_rate_bins_per_second},
            {"freqRange", region.freq_range},
            {"timesBins", region.times_bins},
            {"peakFreqsBins", region.peak_freqs_bins},
            {"sliceCount", region.slices.size()},
            {"contourPoints", std::move(contour_points)},
        };
        if (include_frequency_hz && region.freq_range.size() >= 2) {
            item["freqRangeHz"] = {bin_to_hz(static_cast<std::size_t>(region.freq_range[0])), bin_to_hz(static_cast<std::size_t>(region.freq_range[1]))};
            item["minFrequencyHz"] = bin_to_hz(static_cast<std::size_t>(region.min_frequency_bin));
            item["maxFrequencyHz"] = bin_to_hz(static_cast<std::size_t>(region.max_frequency_bin));
            item["frequencySpanHz"] = bin_value_to_hz(static_cast<double>(region.frequency_span_bins));
            item["minPeakHz"] = bin_to_hz(static_cast<std::size_t>(region.min_peak_bin));
            item["maxPeakHz"] = bin_to_hz(static_cast<std::size_t>(region.max_peak_bin));
            item["meanPeakHz"] = bin_value_to_hz(region.mean_peak_bin);
            item["startPeakHz"] = bin_to_hz(static_cast<std::size_t>(region.start_peak_bin));
            item["endPeakHz"] = bin_to_hz(static_cast<std::size_t>(region.end_peak_bin));
            item["peakSweepRateHzPerSecond"] = bin_value_to_hz(region.peak_sweep_rate_bins_per_second);
        }
        out["whistleRegions"].push_back(std::move(item));
    }

    out["whistleBackgrounds"] = json::array();
    for (const auto& background : result.whistle_backgrounds) {
        out["whistleBackgrounds"].push_back({
            {"channel", background.channel},
            {"channelBitmap", std::uint64_t{1} << background.channel},
            {"timeMs", background.time_ms},
            {"startSample", background.start_sample},
            {"durationMs", background.duration_ms},
            {"loBin", 0},
            {"hiBin", background.spectrum.size()},
            {"spectrum", background.spectrum},
        });
    }

    out["noiseBands"] = json::array();
    for (const auto& noise : result.noise_bands) {
        out["noiseBands"].push_back({
            {"channel", noise.channel},
            {"endSample", noise.end_sample},
            {"timeMs", noise.time_unix_ms},
            {"rmsDb", noise.rms_db},
            {"peakDb", noise.peak_db},
        });
    }

    out["fftNoise"] = json::array();
    for (const auto& noise : result.fft_noise) {
        json bands = json::array();
        for (const auto& band : noise.bands) {
            bands.push_back({
                {"name", band.name},
                {"lowFrequencyHz", band.low_frequency_hz},
                {"highFrequencyHz", band.high_frequency_hz},
                {"meanDb", band.statistics_db.mean},
                {"medianDb", band.statistics_db.median},
                {"low95Db", band.statistics_db.low_95},
                {"high95Db", band.statistics_db.high_95},
                {"minDb", band.statistics_db.minimum},
                {"maxDb", band.statistics_db.maximum},
            });
        }
        out["fftNoise"].push_back({
            {"channel", noise.channel},
            {"endSample", noise.end_sample},
            {"timeMs", noise.time_unix_ms},
            {"nMeasurements", noise.n_measurements},
            {"bands", std::move(bands)},
        });
    }

    out["ltsa"] = json::array();
    for (const auto& entry : result.ltsa) {
        out["ltsa"].push_back({
            {"channel", entry.channel},
            {"startTimeMs", entry.interval.start_time_ms},
            {"endTimeMs", entry.interval.end_time_ms},
            {"nFft", entry.interval.n_fft},
            {"startSample", entry.interval.start_sample},
            {"durationSamples", entry.interval.duration_samples},
            {"magnitude", entry.interval.magnitude},
        });
    }

    out["ishmaelDetections"] = json::array();
    for (const auto& detection : result.ishmael_detections) {
        out["ishmaelDetections"].push_back({
            {"channel", detection.channel},
            {"startSample", detection.start_sample},
            {"durationSamples", detection.duration_samples},
            {"peakTimeSample", detection.peak_time_sample},
            {"peakHeight", detection.peak_height},
            {"startTimeMs", detection.start_time_ms},
            {"lowFreqHz", detection.low_freq_hz},
            {"highFreqHz", detection.high_freq_hz},
        });
    }

    out["sgramCorrDetections"] = json::array();
    for (const auto& detection : result.sgram_corr_detections) {
        out["sgramCorrDetections"].push_back({
            {"channel", detection.channel},
            {"startSample", detection.start_sample},
            {"durationSamples", detection.duration_samples},
            {"peakTimeSample", detection.peak_time_sample},
            {"peakHeight", detection.peak_height},
            {"startTimeMs", detection.start_time_ms},
            {"lowFreqHz", detection.low_freq_hz},
            {"highFreqHz", detection.high_freq_hz},
        });
    }

    out["matchFiltDetections"] = json::array();
    for (const auto& detection : result.match_filt_detections) {
        out["matchFiltDetections"].push_back({
            {"channel", detection.channel},
            {"startSample", detection.start_sample},
            {"durationSamples", detection.duration_samples},
            {"peakTimeSample", detection.peak_time_sample},
            {"peakHeight", detection.peak_height},
            {"startTimeMs", detection.start_time_ms},
            {"lowFreqHz", detection.low_freq_hz},
            {"highFreqHz", detection.high_freq_hz},
        });
    }

    out["matchedTemplateClassifications"] = json::array();
    for (const auto& entry : result.matched_template_classifications) {
        json results = json::array();
        for (const auto& mt : entry.results) {
            json item = {
                {"threshold", mt.threshold},
                {"matchCorr", mt.match_corr},
            };
            // A zeroed "none" reject template yields NaN; omit rather than
            // serialise a null.
            if (std::isfinite(mt.reject_corr)) {
                item["rejectCorr"] = mt.reject_corr;
            }
            results.push_back(std::move(item));
        }
        out["matchedTemplateClassifications"].push_back({
            {"clickIndex", entry.click_index},
            {"clickStartSample", entry.click_start_sample},
            {
                "classifierInstanceId",
                entry.classifier_instance_id,
            },
            {"clickType", entry.click_type},
            {"classified", entry.classified},
            {"results", std::move(results)},
        });
    }

    out["whistleGroups"] = json::array();
    for (const auto& group : result.whistle_groups) {
        out["whistleGroups"].push_back({
            {"groupId", group.group_id},
            {"regionIndices", group.region_indices},
            {"channels", group.channels},
            {"firstStartSample", group.first_start_sample},
            {"lastStartSample", group.last_start_sample},
            {"earlierRegionCount", group.earlier_region_count},
        });
    }

    out["whistleDelays"] = json::array();
    for (const auto& whistle_delay : result.whistle_delays) {
        json item;
        item["channel"] = whistle_delay.channel;
        item["regionNumber"] = whistle_delay.region_number;
        item["startSample"] = whistle_delay.start_sample;
        item["delays"] = json::array();
        for (const auto& delay : whistle_delay.delays) {
            json delay_item = {
                {"pairIndex", delay.pair_index},
                {"audioChannelA", delay.audio_channel_a},
                {"audioChannelB", delay.audio_channel_b},
                {"delaySamples", delay.delay.delay_samples},
                {"delayScore", delay.delay.delay_score},
            };
            if (include_delay_seconds) {
                const double delay_seconds = delay.delay.delay_samples / static_cast<double>(options.sample_rate_hz);
                delay_item["delaySeconds"] = delay_seconds;
                if (include_path_difference_m) {
                    delay_item["pathDifferenceM"] = delay_seconds * options.speed_of_sound_mps;
                }
            }
            delay_item["geometryConstrained"] = delay.geometry_constrained;
            if (delay.geometry_constrained) {
                delay_item["maxDelaySamples"] = delay.max_delay_samples;
                delay_item["hydrophoneDistanceM"] = delay.hydrophone_distance_m;
                if (include_delay_seconds) {
                    delay_item["maxDelaySeconds"] = delay.max_delay_samples / static_cast<double>(options.sample_rate_hz);
                }
            }
            if (delay.pair_bearing_valid && std::isfinite(delay.pair_bearing_radians)) {
                delay_item["pairBearingRadians"] = delay.pair_bearing_radians;
                delay_item["pairBearingDegrees"] = delay.pair_bearing_radians * 180.0 / 3.141592653589793238462643383279502884;
                if (std::isfinite(delay.pair_bearing_error_radians)) {
                    delay_item["pairBearingErrorRadians"] = delay.pair_bearing_error_radians;
                }
                if (!delay.pair_bearing_world_vectors.empty()) {
                    delay_item["pairBearingWorldVectors"] = world_vectors_to_json(delay.pair_bearing_world_vectors);
                }
                if (!delay.pair_bearing_earth_world_vectors.empty()) {
                    delay_item["pairBearingEarthWorldVectors"] =
                        world_vectors_to_json(delay.pair_bearing_earth_world_vectors);
                }
            }
            item["delays"].push_back(std::move(delay_item));
        }
        if (whistle_delay.bearing_valid) {
            json bearing_item = {
                {"bearingRadians", whistle_delay.bearing_radians},
                {"bearingDegrees", whistle_delay.bearing_radians * 180.0 / 3.141592653589793238462643383279502884},
                {"bearingAmbiguity", whistle_delay.bearing_ambiguity},
                {"pairCount", whistle_delay.bearing_pair_count},
            };
            if (std::isfinite(whistle_delay.bearing_error_radians)) {
                bearing_item["bearingErrorRadians"] = whistle_delay.bearing_error_radians;
            }
            item["bearing"] = std::move(bearing_item);
        }
        if (whistle_delay.lsq_bearing.valid) {
            json lsq_item = {
                {"azimuthRadians", whistle_delay.lsq_bearing.azimuth_radians},
                {"azimuthDegrees", whistle_delay.lsq_bearing.azimuth_radians * 180.0 / 3.141592653589793238462643383279502884},
                {"elevationRadians", whistle_delay.lsq_bearing.elevation_radians},
                {"elevationDegrees", whistle_delay.lsq_bearing.elevation_radians * 180.0 / 3.141592653589793238462643383279502884},
                {"usedPairs", whistle_delay.lsq_bearing.used_pairs},
            };
            if (std::isfinite(whistle_delay.lsq_bearing.azimuth_error_radians)) {
                lsq_item["azimuthErrorRadians"] = whistle_delay.lsq_bearing.azimuth_error_radians;
            }
            if (std::isfinite(whistle_delay.lsq_bearing.elevation_error_radians)) {
                lsq_item["elevationErrorRadians"] = whistle_delay.lsq_bearing.elevation_error_radians;
            }
            if (!whistle_delay.lsq_bearing.world_vectors.empty()) {
                lsq_item["worldVectors"] = world_vectors_to_json(whistle_delay.lsq_bearing.world_vectors);
            }
            if (!whistle_delay.lsq_bearing.earth_world_vectors.empty()) {
                lsq_item["earthWorldVectors"] = world_vectors_to_json(whistle_delay.lsq_bearing.earth_world_vectors);
            }
            item["lsqBearing"] = std::move(lsq_item);
        }
        if (whistle_delay.grid_bearing.valid) {
            item["gridBearing"] = grid_bearing_to_json(whistle_delay.grid_bearing);
        }
        item["arrayShape"] = std::string(pamguard::localisation::array_shape_name(whistle_delay.array_shape));
        item["bearingLocaliser"] = std::string(pamguard::localisation::bearing_localiser_name(whistle_delay.bearing_localiser));
        out["whistleDelays"].push_back(std::move(item));
    }

    return out;
}

json config_to_json(const pamguard::core::AnalysisConfig& config, const SessionRuntimeStats* stats = nullptr) {
    json body;
    auto range_to_json = [](const pamguard::detectors::FrequencyRange& range) {
        return json::array({range.low_hz, range.high_hz});
    };
    auto sweep_range_to_json = [](const pamguard::detectors::SweepRange& range) {
        return json::array({range.low, range.high});
    };
    auto iir_filter_to_json = [](const pamguard::dsp::IirFilterParams& filter) {
        std::string type;
        switch (filter.type) {
        case pamguard::dsp::IirFilterType::None: type = "none"; break;
        case pamguard::dsp::IirFilterType::Butterworth: type = "butterworth"; break;
        case pamguard::dsp::IirFilterType::Chebyshev: type = "chebyshev"; break;
        case pamguard::dsp::IirFilterType::FirWindow: type = "firwindow"; break;
        case pamguard::dsp::IirFilterType::FirArbitrary: type = "firarbitrary"; break;
        case pamguard::dsp::IirFilterType::Fft: type = "fft"; break;
        }
        std::string band;
        switch (filter.band) {
        case pamguard::dsp::IirFilterBand::HighPass: band = "highpass"; break;
        case pamguard::dsp::IirFilterBand::LowPass: band = "lowpass"; break;
        case pamguard::dsp::IirFilterBand::BandPass: band = "bandpass"; break;
        case pamguard::dsp::IirFilterBand::BandStop: band = "bandstop"; break;
        }
        return json{
            {"type", type},
            {"band", band},
            {"order", filter.order},
            {"highPassFreq", filter.high_pass_freq_hz},
            {"lowPassFreq", filter.low_pass_freq_hz},
            {"passBandRipple", filter.pass_band_ripple_db},
            {"stopBandRipple", filter.stop_band_ripple_db},
            {"chebyGamma", filter.cheby_gamma},
            {"arbitraryFrequenciesHz", filter.arbitrary_frequencies_hz},
            {"arbitraryGainsDb", filter.arbitrary_gains_db},
        };
    };
    auto delay_measurement_to_json =
        [](const pamguard::localisation::DelayMeasurementConfig& delay) {
            std::string band;
            switch (delay.filter_band) {
            case pamguard::localisation::DelayFilterBand::HighPass:
                band = "highpass";
                break;
            case pamguard::localisation::DelayFilterBand::LowPass:
                band = "lowpass";
                break;
            case pamguard::localisation::DelayFilterBand::BandPass:
                band = "bandpass";
                break;
            case pamguard::localisation::DelayFilterBand::BandStop:
                band = "bandstop";
                break;
            }
            return json{
                {"filterBearings", delay.filter_bearings},
                {"filter", {
                    {"band", band},
                    {"highPassFreq", delay.filter_high_pass_hz},
                    {"lowPassFreq", delay.filter_low_pass_hz},
                }},
                {"envelopeBearings", delay.envelope_bearings},
                {"useLeadingEdge", delay.use_leading_edge},
                {"upSample", delay.up_sample},
                {"useRestrictedBins", delay.use_restricted_bins},
                {"restrictedBins", delay.restricted_bins},
            };
        };
    body["sessionId"] = config.session_id;
    body["sourceId"] = config.source_id;
    body["ownerId"] = config.owner_id.empty() ? json(nullptr) : json(config.owner_id);
    body["tenantId"] = config.tenant_id.empty() ? json(nullptr) : json(config.tenant_id);
    body["sampleRateHz"] = config.sample_rate_hz;
    body["channelCount"] = config.channel_count;
    body["fft"] = {
        {"length", config.detector.fft.fft_length},
        {"hop", config.detector.fft.fft_hop},
        {"windowType", std::string(pamguard::dsp::window_name(config.detector.fft.window_type))},
        {"windowTypeId", static_cast<int>(config.detector.fft.window_type)},
        {"channels", config.detector.fft.channels},
    };
    body["click"] = {
        {"enabled", config.detector.click_detector_enabled},
        {"localisation", config.detector.click_localisation_enabled},
        {"channelBitmap", config.detector.click.channel_bitmap},
        {"triggerBitmap", config.detector.click.trigger_bitmap},
        {"minTriggerChannels", config.detector.click.min_trigger_channels},
        {"thresholdDb", config.detector.click.threshold_db},
        {"longFilter", config.detector.click.long_filter},
        {"longFilter2", config.detector.click.long_filter_2},
        {"shortFilter", config.detector.click.short_filter},
        {"preSample", config.detector.click.pre_sample},
        {"postSample", config.detector.click.post_sample},
        {"minSep", config.detector.click.min_sep},
        {"maxLength", config.detector.click.max_length},
        {"publishTriggerFunction", config.detector.click.publish_trigger_function},
        {"featuresEnabled", config.detector.click_features_enabled},
        {"preFilterActive", config.detector.click.pre_filter.type != pamguard::dsp::IirFilterType::None},
        {"triggerFilterActive", config.detector.click.trigger_filter.type != pamguard::dsp::IirFilterType::None},
        {"echoRunOnline", config.detector.click_echo_enabled},
        {"echoDiscard", config.detector.click_echo_discard},
        {"echoMaxIntervalSeconds", config.detector.click_echo_max_interval_seconds},
        {"basicClassifierEnabled", config.detector.click_basic_classifier_enabled},
        {"basicClassifierTypeCount", config.detector.click_basic_classifier.click_types.size()},
        {"trainEnabled", config.detector.click_train_tracker_enabled},
        {"trainAlgorithm", config.detector.click_train_mht ? "mht" : "ici"},
        {"trainMaxIciSeconds", config.detector.click_train.max_ici_seconds},
        {"trainMinClicks", config.detector.click_train.min_clicks},
        {"trainClassifierEnabled", config.detector.click_train_classifier_enabled},
    };
    switch (config.detector.click_grouping_type) {
    case pamguard::core::DetectorConfig::ClickGroupingType::Singles:
        body["click"]["groupingType"] = "singles";
        break;
    case pamguard::core::DetectorConfig::ClickGroupingType::All:
        body["click"]["groupingType"] = "all";
        break;
    case pamguard::core::DetectorConfig::ClickGroupingType::User:
        body["click"]["groupingType"] = "user";
        break;
    }

    body["click"]["channelGroups"] = config.detector.click_channel_groups;
    body["click"]["angleVetoes"] = json::array();
    for (const auto& veto : config.detector.click_angle_vetoes) {
        body["click"]["angleVetoes"].push_back({
            {"channels", veto.channels},
            {"startAngleDegrees", veto.start_angle_degrees},
            {"endAngleDegrees", veto.end_angle_degrees},
        });
    }
    body["click"]["delayMeasurement"] =
        delay_measurement_to_json(config.detector.click_delay_measurement);
    body["click"]["delayMeasurement"]["typeSettings"] = json::array();
    for (const auto& [click_type, delay] :
         config.detector.click_delay_measurement_by_type) {
        auto item = delay_measurement_to_json(delay);
        item["clickType"] = click_type;
        body["click"]["delayMeasurement"]["typeSettings"].push_back(
            std::move(item));
    }
    body["click"]["groups"] = json::array();
    for (const auto& group : config.detector.click_groups) {
        body["click"]["groups"].push_back({
            {"channelBitmap", group.channel_bitmap},
            {"triggerBitmap", group.trigger_bitmap},
        });
    }
    std::string click_classifier_type;
    switch (config.detector.click_classifier_type) {
    case pamguard::core::DetectorConfig::ClickClassifierType::Basic:
        click_classifier_type = "basic";
        break;
    case pamguard::core::DetectorConfig::ClickClassifierType::Sweep:
        click_classifier_type = "sweep";
        break;
    case pamguard::core::DetectorConfig::ClickClassifierType::None:
        click_classifier_type = "none";
        break;
    }
    body["click"]["classifier"] = {
        {"type", click_classifier_type},
        {"runOnline", config.detector.click_classify_online},
        {"discardUnclassifiedClicks", config.detector.click_discard_unclassified},
        {"basicEnabled", config.detector.click_basic_classifier_enabled},
        {"basicTypeCount", config.detector.click_basic_classifier.click_types.size()},
        {"sweepEnabled", config.detector.click_sweep_classifier_enabled},
        {"sweepTypeCount", config.detector.click_sweep_classifier.click_types.size()},
        {"sweepCheckAllClassifiers",
         config.detector.click_sweep_classifier.check_all_classifiers},
    };
    body["click"]["classifier"]["sweepTypes"] = json::array();
    for (const auto& type : config.detector.click_sweep_classifier.click_types) {
        std::string filter_band;
        switch (type.fft_filter.band) {
        case pamguard::detectors::SweepFftFilterBand::HighPass: filter_band = "highPass"; break;
        case pamguard::detectors::SweepFftFilterBand::LowPass: filter_band = "lowPass"; break;
        case pamguard::detectors::SweepFftFilterBand::BandPass: filter_band = "bandPass"; break;
        case pamguard::detectors::SweepFftFilterBand::BandStop: filter_band = "bandStop"; break;
        }
        body["click"]["classifier"]["sweepTypes"].push_back({
            {"name", type.name},
            {"speciesCode", type.species_code},
            {"discard", type.discard},
            {"enabled", type.enabled},
            {"channelChoice", static_cast<int>(type.channel_choice)},
            {"restrictLength", type.restrict_length},
            {"restrictedBins", type.restricted_bins},
            {"restrictedBinType", static_cast<int>(type.restricted_bin_type)},
            {"enableLength", type.enable_length},
            {"lengthSmoothing", type.length_smoothing},
            {"lengthDb", type.length_db},
            {"lengthMs", sweep_range_to_json(type.length_ms)},
            {"enableEnergyBands", type.enable_energy_bands},
            {"testEnergyBandHz", sweep_range_to_json(type.test_energy_band_hz)},
            {"controlEnergyBand0Hz", sweep_range_to_json(type.control_energy_band_0_hz)},
            {"controlEnergyBand1Hz", sweep_range_to_json(type.control_energy_band_1_hz)},
            {"energyThreshold0Db", type.energy_threshold_0_db},
            {"energyThreshold1Db", type.energy_threshold_1_db},
            {"testAmplitude", type.test_amplitude},
            {"amplitudeRangeDb", sweep_range_to_json(type.amplitude_range_db)},
            {"enableFftFilter", type.enable_fft_filter},
            {"fftFilter", {
                {"band", filter_band},
                {"lowPassFreqHz", type.fft_filter.low_pass_freq_hz},
                {"highPassFreqHz", type.fft_filter.high_pass_freq_hz},
            }},
            {"enablePeak", type.enable_peak},
            {"enableWidth", type.enable_width},
            {"enableMean", type.enable_mean},
            {"peakSearchRangeHz", sweep_range_to_json(type.peak_search_range_hz)},
            {"peakRangeHz", sweep_range_to_json(type.peak_range_hz)},
            {"peakWidthRangeHz", sweep_range_to_json(type.peak_width_range_hz)},
            {"meanRangeHz", sweep_range_to_json(type.mean_range_hz)},
            {"peakSmoothing", type.peak_smoothing},
            {"peakWidthThresholdDb", type.peak_width_threshold_db},
            {"enableZeroCrossings", type.enable_zero_crossings},
            {"zeroCrossingCount", sweep_range_to_json(type.zero_crossing_count)},
            {"enableSweep", type.enable_sweep},
            {"zeroCrossingSweepKhzPerMs",
             sweep_range_to_json(type.zero_crossing_sweep_khz_per_ms)},
            {"enableMinCrossCorrelation", type.enable_min_cross_correlation},
            {"enablePeakCrossCorrelation", type.enable_peak_cross_correlation},
            {"minCorrelation", type.min_correlation},
            {"correlationFactor", type.correlation_factor},
            {"enableBearingLimits", type.enable_bearing_limits},
            {"excludeBearingLimits", type.exclude_bearing_limits},
            {"bearingLimitsRadians", sweep_range_to_json(type.bearing_limits_radians)},
        });
    }
    body["click"]["preFilter"] = iir_filter_to_json(config.detector.click.pre_filter);
    body["click"]["triggerFilter"] = iir_filter_to_json(config.detector.click.trigger_filter);
    body["click"]["noise"] = {
        {"sampleWaveforms", config.detector.click.sample_noise},
        {"waveformIntervalSeconds",
         config.detector.click.noise_sample_interval_seconds},
        {"storeBackground", config.detector.click.store_background},
        {"backgroundIntervalMilliseconds",
         config.detector.click.background_interval_milliseconds},
    };
    auto& click_train = body["click"]["train"];
    click_train = {
        {"enabled", config.detector.click_train_tracker_enabled},
        {"algorithm", config.detector.click_train_mht ? "mht" : "ici"},
        {"minIciSeconds", config.detector.click_train.min_ici_seconds},
        {"maxIciSeconds", config.detector.click_train.max_ici_seconds},
        {"minClicks", config.detector.click_train.min_clicks},
    };
    const auto& pre_classifier = config.detector.click_train_pre_classifier;
    const auto& idi_classifier = config.detector.click_train_idi_classifier;
    const auto& bearing_classifier =
        config.detector.click_train_bearing_classifier;
    const auto& template_classifier =
        config.detector.click_train_template_classifier;
    constexpr double radians_to_degrees =
        180.0 / 3.141592653589793238462643383279502884;
    click_train["classifier"] = {
        {"enabled", config.detector.click_train_classifier_enabled},
        {"preClassifier", {
            {"chi2Threshold", pre_classifier.chi2_threshold},
            {"minClicks", pre_classifier.min_clicks},
            {"minTimeSeconds", pre_classifier.min_time_seconds},
            {"speciesFlag", pre_classifier.species_flag},
        }},
        {"idi", {
            {"enabled", config.detector.click_train_idi_classifier_enabled},
            {"useMedianIdi", idi_classifier.use_median_idi},
            {"minMedianIdi", idi_classifier.min_median_idi},
            {"maxMedianIdi", idi_classifier.max_median_idi},
            {"useMeanIdi", idi_classifier.use_mean_idi},
            {"minMeanIdi", idi_classifier.min_mean_idi},
            {"maxMeanIdi", idi_classifier.max_mean_idi},
            {"useStdIdi", idi_classifier.use_std_idi},
            {"minStdIdi", idi_classifier.min_std_idi},
            {"maxStdIdi", idi_classifier.max_std_idi},
            {"speciesFlag", idi_classifier.species_flag},
        }},
        {"bearing", {
            {"enabled",
             config.detector.click_train_bearing_classifier_enabled},
            {"bearingLimMinDegrees",
             bearing_classifier.bearing_lim_min * radians_to_degrees},
            {"bearingLimMaxDegrees",
             bearing_classifier.bearing_lim_max * radians_to_degrees},
            {"useMean", bearing_classifier.use_mean},
            {"minMeanBearingDerivativeDegrees",
             bearing_classifier.min_mean_bearing_derivative *
                 radians_to_degrees},
            {"maxMeanBearingDerivativeDegrees",
             bearing_classifier.max_mean_bearing_derivative *
                 radians_to_degrees},
            {"useMedian", bearing_classifier.use_median},
            {"minMedianBearingDerivativeDegrees",
             bearing_classifier.min_median_bearing_derivative *
                 radians_to_degrees},
            {"maxMedianBearingDerivativeDegrees",
             bearing_classifier.max_median_bearing_derivative *
                 radians_to_degrees},
            {"useStd", bearing_classifier.use_std},
            {"minStdBearingDerivativeDegrees",
             bearing_classifier.min_std_bearing_derivative *
                 radians_to_degrees},
            {"maxStdBearingDerivativeDegrees",
             bearing_classifier.max_std_bearing_derivative *
                 radians_to_degrees},
            {"speciesFlag", bearing_classifier.species_flag},
        }},
        {"template", {
            {"enabled",
             config.detector.click_train_template_classifier_enabled},
            {"spectrum", template_classifier.template_spectrum},
            {"sampleRateHz",
             template_classifier.template_sample_rate_hz},
            {"correlationThreshold",
             template_classifier.correlation_threshold},
            {"speciesFlag", template_classifier.species_flag},
        }},
    };
    if (config.detector.click_train_mht) {
        const auto& chi2 = config.detector.click_train_mht_chi2;
        const auto& kernel = config.detector.click_train_mht_kernel;
        click_train["mht"] = {
            {"enableIdi", chi2.enable_idi},
            {"enableAmplitude", chi2.enable_amplitude},
            {"enableLength", chi2.enable_length},
            {"enableBearing", chi2.enable_bearing},
            {"enablePeakFrequency", chi2.enable_peak_frequency},
            {"enableTimeDelay", chi2.enable_time_delay},
            {"enableCorrelation", chi2.enable_correlation},
            {"coastPenalty", chi2.coast_penalty},
            {"newTrackPenalty", chi2.new_track_penalty},
            {"newTrackN", chi2.new_track_n},
            {"maxIci", chi2.max_ici},
            {"lowIciExponent", chi2.low_ici_exponent},
            {"longTrackExponent", chi2.long_track_exponent},
            {"useElectricalNoiseFilter", chi2.use_electrical_noise_filter},
            {"electricalNoiseMinChi2", chi2.electrical_noise_min_chi2},
            {"electricalNoiseNDataUnits", chi2.electrical_noise_n_data_units},
            {"nHold", kernel.n_hold},
            {"nPruneback", kernel.n_pruneback},
            {"nPrunebackStart", kernel.n_pruneback_start},
            {"maxCoast", kernel.max_coast},
        };
        // Retained as a diagnostic alias for clients predating the nested
        // create/status contract symmetry.
        body["click"]["trainMht"] = click_train["mht"];
    }
    body["click"]["features"] = {
        {"fftLength", config.detector.click_features.fft_length},
        {"lengthEnergyFraction", config.detector.click_features.length_energy_fraction},
        {"widthEnergyFraction", config.detector.click_features.width_energy_fraction},
        {"peakFrequencySearchHz", range_to_json(config.detector.click_features.peak_frequency_search_hz)},
        {"meanFrequencyRangeHz", range_to_json(config.detector.click_features.mean_frequency_range_hz)},
    };
    body["click"]["features"]["energyBandsHz"] = json::array();
    for (const auto& band : config.detector.click_features.energy_bands_hz) {
        body["click"]["features"]["energyBandsHz"].push_back(range_to_json(band));
    }
    body["whistle"] = {
        {"enabled", config.detector.whistle_peak_detector_enabled},
        {"regionEnabled", config.detector.whistle_region_detector_enabled},
        {"channelBitmap", config.detector.whistle_channel_bitmap},
        {"detectionThresholdDb", config.detector.whistle_peak.detection_threshold_db},
        {"peakTimeConstant0", config.detector.whistle_peak.peak_time_constant_0},
        {"peakTimeConstant1", config.detector.whistle_peak.peak_time_constant_1},
        {"maxPercentOverThreshold", config.detector.whistle_peak.max_percent_over_threshold},
        {"minPeakWidth", config.detector.whistle_peak.min_peak_width},
        {"maxPeakWidth", config.detector.whistle_peak.max_peak_width},
        {"searchBin0", config.detector.whistle_peak.search_bin0},
        {"searchBin1", config.detector.whistle_peak.search_bin1},
        {"warmupSlices", config.detector.whistle_peak.warmup_slices},
        {"noiseMedianFilter", config.detector.whistle_noise.run_median_filter},
        {"noiseAverageSubtraction", config.detector.whistle_noise.run_average_subtraction},
        {"noiseKernelSmoothing", config.detector.whistle_noise.run_kernel_smoothing},
        {"noiseThreshold", config.detector.whistle_noise.run_threshold},
        {"minFrequencyHz", config.detector.whistle_region.min_frequency_hz},
        {"maxFrequencyHz", config.detector.whistle_region.max_frequency_hz},
        {"backgroundIntervalSeconds",
         config.detector.whistle_region.background_interval_seconds},
        {"minPixels", config.detector.whistle_region.min_pixels},
        {"minLength", config.detector.whistle_region.min_length},
        {"connectType", config.detector.whistle_region.connect_type},
        {"keepShapeStubs", config.detector.whistle_region.keep_shape_stubs},
        {"fragmentationMethod", config.detector.whistle_region.fragmentation_method},
        {"maxCrossLength", config.detector.whistle_region.max_cross_length},
        {"rejectFirstQuarterSecond", config.detector.whistle_region.reject_first_quarter_second},
    };
    switch (config.detector.whistle_grouping_type) {
    case pamguard::core::DetectorConfig::ClickGroupingType::Singles:
        body["whistle"]["groupingType"] = "singles";
        break;
    case pamguard::core::DetectorConfig::ClickGroupingType::All:
        body["whistle"]["groupingType"] = "all";
        break;
    case pamguard::core::DetectorConfig::ClickGroupingType::User:
        body["whistle"]["groupingType"] = "user";
        break;
    }
    body["whistle"]["channelGroups"] =
        config.detector.whistle_channel_groups;
    body["whistle"]["noise"] = {
        {"medianFilter", config.detector.whistle_noise.run_median_filter},
        {"medianFilterLength", config.detector.whistle_noise.median_filter_length},
        {"averageSubtraction", config.detector.whistle_noise.run_average_subtraction},
        {"updateConstant", config.detector.whistle_noise.average_update_constant},
        {"kernelSmoothing", config.detector.whistle_noise.run_kernel_smoothing},
        {"threshold", config.detector.whistle_noise.run_threshold},
        {"thresholdDb", config.detector.whistle_noise.threshold_db},
        {"finalOutput", config.detector.whistle_noise.threshold_final_output},
    };
    body["fftNoise"] = {
        {"enabled", config.detector.fft_noise.enabled},
        {"measurementIntervalSeconds",
         config.detector.fft_noise.measurement_interval_seconds},
        {"nMeasures", config.detector.fft_noise.n_measures},
        {"useAll", config.detector.fft_noise.use_all},
        {"channels", config.detector.fft_noise.channels},
        {"bands", json::array()},
    };
    std::uint32_t fft_noise_bitmap = 0;
    for (const auto channel : config.detector.fft_noise.channels) {
        if (channel < 32) {
            fft_noise_bitmap |= 1u << channel;
        }
    }
    body["fftNoise"]["channelBitmap"] = fft_noise_bitmap;
    for (const auto& band : config.detector.fft_noise.bands) {
        body["fftNoise"]["bands"].push_back({
            {"name", band.name},
            {"lowFrequencyHz", band.low_frequency_hz},
            {"highFrequencyHz", band.high_frequency_hz},
        });
    }
    std::string noise_band_type;
    switch (config.detector.noise_band.band_type) {
    case pamguard::detectors::NoiseBandType::Octave:
        noise_band_type = "octave";
        break;
    case pamguard::detectors::NoiseBandType::ThirdOctave:
        noise_band_type = "thirdOctave";
        break;
    case pamguard::detectors::NoiseBandType::Decidecade:
        noise_band_type = "decidecade";
        break;
    case pamguard::detectors::NoiseBandType::Decade:
        noise_band_type = "decade";
        break;
    case pamguard::detectors::NoiseBandType::TenthOctave:
        noise_band_type = "tenthOctave";
        break;
    case pamguard::detectors::NoiseBandType::TwelfthOctave:
        noise_band_type = "twelfthOctave";
        break;
    }
    body["acquisition"] = {
        {"voltsPeak2Peak", config.acquisition.volts_peak_to_peak},
        {"preampGainDb", config.acquisition.preamp_gain_db},
    };
    body["noiseBand"] = {
        {"enabled", config.detector.noise_band.enabled},
        {"bandType", noise_band_type},
        {"minFrequencyHz", config.detector.noise_band.min_frequency_hz},
        {"maxFrequencyHz", config.detector.noise_band.max_frequency_hz},
        {"referenceFrequencyHz",
         config.detector.noise_band.reference_frequency_hz},
        {"iirOrder", config.detector.noise_band.iir_order},
        {"outputIntervalSeconds",
         config.detector.noise_band.output_interval_seconds},
    };
    body["ltsa"] = {
        {"enabled", config.detector.ltsa.enabled},
        {"intervalSeconds", config.detector.ltsa.interval_seconds},
    };
    body["ishmael"] = {
        {"enabled", config.detector.ishmael.enabled},
        {"f0", config.detector.ishmael.f0},
        {"f1", config.detector.ishmael.f1},
        {"ratioF0", config.detector.ishmael.ratio_f0},
        {"ratioF1", config.detector.ishmael.ratio_f1},
        {"useRatio", config.detector.ishmael.use_ratio},
        {"useLog", config.detector.ishmael.use_log},
        {"adaptiveThreshold", config.detector.ishmael.adaptive_threshold},
        {"longFilter", config.detector.ishmael.long_filter},
        {"spikeDecay", config.detector.ishmael.spike_decay},
        {"outputSmoothing", config.detector.ishmael.output_smoothing},
        {"shortFilter", config.detector.ishmael.short_filter},
        {"thresh", config.detector.ishmael.thresh},
        {"minTimeSeconds", config.detector.ishmael.min_time_s},
        {"maxTimeSeconds", config.detector.ishmael.max_time_s},
        {"refractoryTimeSeconds",
         config.detector.ishmael.refractory_time_s},
    };
    body["sgramCorr"] = {
        {"enabled", config.detector.sgram_corr.enabled},
        {"segments", json::array()},
        {"spread", config.detector.sgram_corr.spread},
        {"useLog", config.detector.sgram_corr.use_log},
        {"thresh", config.detector.sgram_corr.thresh},
        {"minTimeSeconds", config.detector.sgram_corr.min_time_s},
        {"maxTimeSeconds", config.detector.sgram_corr.max_time_s},
        {"refractoryTimeSeconds",
         config.detector.sgram_corr.refractory_time_s},
    };
    for (const auto& segment : config.detector.sgram_corr.segments) {
        body["sgramCorr"]["segments"].push_back(
            {segment[0], segment[1], segment[2], segment[3]});
    }
    body["matchFilt"] = {
        {"enabled", config.detector.match_filt.enabled},
        {"kernel", config.detector.match_filt.kernel},
        {"channels", config.detector.match_filt.channels},
        {"thresh", config.detector.match_filt.thresh},
        {"minTimeSeconds", config.detector.match_filt.min_time_s},
        {"maxTimeSeconds", config.detector.match_filt.max_time_s},
        {"refractoryTimeSeconds",
         config.detector.match_filt.refractory_time_s},
    };
    body["matchedTemplate"] = {
        {"enabled", config.detector.matched_template.enabled},
    };
    if (config.detector.matched_template.enabled) {
        pamguard::core::MatchedTemplateSettings settings;
        settings.click_type =
            config.detector.matched_template_click_type;
        settings.normalisation_type =
            config.detector.matched_template.normalisation_type;
        settings.peak_search =
            config.detector.matched_template.peak_search;
        settings.peak_smoothing =
            config.detector.matched_template.peak_smoothing;
        settings.length_db =
            config.detector.matched_template.length_db;
        settings.restricted_bins =
            config.detector.matched_template.restricted_bins;
        settings.channel_classification =
            config.detector.matched_template.channel_classification;
        settings.classifiers =
            config.detector.matched_template.classifiers;
        auto canonical = json::parse(
            pamguard::core::matched_template_settings_to_json(
                settings,
                1));
        canonical["enabled"] = true;
        body["matchedTemplate"] = std::move(canonical);
    }
    body["array"] = {
        {"id", config.array.id},
        {"speedOfSoundMps", config.array.speed_of_sound_mps},
        {"speedOfSoundErrorMps", config.array.speed_of_sound_error_mps},
        {"timingErrorSeconds", config.array.timing_error_seconds},
        {"spacingErrorM", config.array.spacing_error_m},
        {"wobbleRadians", config.array.wobble_radians},
        {"hydrophoneCount", config.array.hydrophones.size()},
        {"clickLocalisationReadiness", click_localisation_readiness_to_json(config)},
    };
    body["array"]["hydrophones"] = json::array();
    for (const auto& hydrophone : config.array.hydrophones) {
        body["array"]["hydrophones"].push_back({
            {"channel", hydrophone.channel},
            {"xM", hydrophone.x_m},
            {"yM", hydrophone.y_m},
            {"zM", hydrophone.z_m},
            {"sensitivityDb", hydrophone.sensitivity_db},
        });
    }
    if (stats != nullptr) {
        body["runtime"] = {
            {"chunksReceived", stats->chunks_received},
            {"framesReceived", stats->frames_received},
            {"bytesReceived", stats->bytes_received},
            {"createdUnixMs", stats->created_unix_ms},
            {"lastReceiveUnixMs", stats->last_receive_unix_ms},
            {"lastStartSample", stats->last_start_sample},
            {"expectedStartSample", stats->expected_start_sample},
            {"sampleDiscontinuities", stats->sample_discontinuities},
            {"lastSampleDelta", stats->last_sample_delta},
            {"lastSampleContinuity", stats->last_sample_continuity},
            {"lastTimeMs", stats->last_time_ms},
            {"spectrogramFrames", stats->spectrogram_frames},
            {"clicks", stats->clicks},
            {"clickFeatures", stats->click_features},
            {"clickClassifications", stats->click_classifications},
            {"clickTrains", stats->click_trains},
            {"clickTrainLocalisations", stats->click_train_localisations},
            {"clickTrainBearings", stats->click_train_bearings},
            {"clickLocalisations", stats->click_localisations},
            {"clickBearings", stats->click_bearings},
            {"whistlePeaks", stats->whistle_peaks},
            {"whistleRegions", stats->whistle_regions},
            {"processCalls", stats->process_calls},
            {"totalProcessMs", stats->total_process_ms},
            {"lastProcessMs", stats->last_process_ms},
        };
        body["status"] = session_operational_status_to_json(*stats, current_unix_ms());
    }
    return body;
}

void json_response(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

void encoded_json_response(
    httplib::Response& res,
    std::string body,
    int status = 200,
    const std::string& etag = {}) {
    res.status = status;
    if (!etag.empty()) {
        res.set_header("ETag", etag);
    }
    res.set_header("Cache-Control", "no-store");
    res.set_content(
        std::move(body),
        "application/json; charset=utf-8");
}

std::string projection_status_name(
    const pamguard::project::ProjectProjectionResult& projection) {
    switch (projection.status()) {
    case pamguard::project::ProjectionStatus::Invalid:
        return "invalid";
    case pamguard::project::ProjectionStatus::NeedsConfiguration:
        return "needsConfiguration";
    case pamguard::project::ProjectionStatus::Runnable:
        return "runnable";
    }
    return "invalid";
}

bool inject_sound_recorder_deployment(
    const pamguard::project::ActiveProjectSnapshot& snapshot,
    const SoundRecorderDeploymentContext& deployment,
    pamguard::core::ModuleGraphDocument& graph) {
    for (const auto& unit : snapshot.project.controlled_units) {
        if (unit.type_id != "pamguard.sound-recorder") {
            continue;
        }
        if (!deployment.ready() ||
            !uuid_path_component(snapshot.project.project_id) ||
            !uuid_path_component(unit.id)) {
            return false;
        }
        const auto* projected =
            snapshot.projection.index.find_runtime_node(
                unit.id,
                "recorder-process");
        if (!projected ||
            projected->runtime_type_id !=
                "pamguard.sound-recorder") {
            return false;
        }
        const auto runtime = std::find_if(
            graph.modules.begin(),
            graph.modules.end(),
            [&](const auto& module) {
                return module.id == projected->runtime_node_id &&
                    module.type_id ==
                        "pamguard.sound-recorder";
            });
        if (runtime == graph.modules.end()) {
            return false;
        }
        try {
            const auto placeholder =
                json::parse(runtime->settings_json);
            if (!placeholder.is_object() ||
                !placeholder.contains("settings") ||
                !placeholder.at("settings").is_object()) {
                return false;
            }
            const auto folder =
                *deployment.root /
                snapshot.project.project_id /
                unit.id;
            runtime->settings_json =
                pamguard::project::
                    sound_recorder_runtime_settings_json(
                        placeholder.at("settings").dump(),
                        1,
                        {
                            folder.string(),
                        });
        }
        catch (const std::exception&) {
            return false;
        }
    }
    return true;
}

bool project_runtime_deployment_ready(
    const pamguard::project::ActiveProjectSnapshot& snapshot,
    const SoundRecorderDeploymentContext& deployment) {
    if (!snapshot.projection.runnable()) {
        return false;
    }
    auto graph = snapshot.projection.graph;
    return inject_sound_recorder_deployment(
        snapshot,
        deployment,
        graph);
}

pamguard::core::ModuleGraphDocument project_runtime_document(
    const pamguard::project::ActiveProjectSnapshot& snapshot,
    const SoundRecorderDeploymentContext& deployment) {
    if (snapshot.projection.runnable()) {
        auto graph = snapshot.projection.graph;
        if (inject_sound_recorder_deployment(
                snapshot,
                deployment,
                graph)) {
            return graph;
        }
    }
    // An editor-valid but incomplete project remains saveable. Its previous
    // runtime must not remain addressable, so install a stopped, empty
    // runtime at the active working revision until every required portable
    // binding and deployment-owned storage binding is ready.
    pamguard::core::ModuleGraphDocument graph;
    graph.revision = snapshot.projection.graph.revision;
    return graph;
}

std::unique_ptr<pamguard::core::ModuleRuntime>
preflight_project_runtime(
    const pamguard::project::ActiveProjectSnapshot& snapshot,
    const SoundRecorderDeploymentContext& deployment) {
    auto candidate =
        std::make_unique<pamguard::core::ModuleRuntime>();
    candidate->configure(
        project_runtime_document(snapshot, deployment));
    return candidate;
}

void activate_prepared_project_runtime(
    const pamguard::project::ActiveProjectSnapshot& snapshot,
    const SoundRecorderDeploymentContext& deployment,
    pamguard::core::ModuleGraph& graph,
    pamguard::core::ModuleRuntime& runtime,
    pamguard::core::ModuleRuntime& prepared_runtime) {
    const auto validation =
        graph.restore(
            project_runtime_document(snapshot, deployment));
    if (!validation.valid()) {
        const auto message = validation.issues.empty()
            ? std::string("unknown graph validation error")
            : validation.issues.front().message;
        throw std::runtime_error(
            "Generated project runtime graph is invalid: " + message);
    }
    runtime.swap_stopped(prepared_runtime);
}

void activate_project_runtime(
    const pamguard::project::ActiveProjectSnapshot& snapshot,
    const SoundRecorderDeploymentContext& deployment,
    pamguard::core::ModuleGraph& graph,
    pamguard::core::ModuleRuntime& runtime) {
    auto prepared =
        preflight_project_runtime(snapshot, deployment);
    activate_prepared_project_runtime(
        snapshot,
        deployment,
        graph,
        runtime,
        *prepared);
}

int project_authority_http_status(
    const pamguard::project::ProjectAuthorityError& error) {
    if (error.code() == "precondition_required") {
        return 428;
    }
    if (error.code() == "precondition_failed") {
        return 412;
    }
    if (error.code() == "dirty_project" ||
        error.code() == "durable_conflict") {
        return 409;
    }
    if (error.code() == "public_input_not_found") {
        return 404;
    }
    if (error.code() == "project_save_failed" ||
        error.code() == "project_save_uncertain" ||
        error.code() == "validated_only_candidate") {
        return 500;
    }
    return 422;
}

void project_authority_error_response(
    httplib::Response& res,
    const pamguard::project::ProjectAuthorityError& error) {
    json body = {
        {"error", error.what()},
        {"code", error.code()},
    };
    if (!error.current_etag().empty()) {
        body["currentEtag"] = error.current_etag();
    }
    encoded_json_response(
        res,
        body.dump(),
        project_authority_http_status(error),
        error.current_etag());
}

void project_runtime_running_response(
    httplib::Response& res,
    const pamguard::project::ActiveProjectSnapshot& snapshot) {
    encoded_json_response(
        res,
        json({
            {
                "error",
                "Project changes that can alter runtime state require the "
                "module runtime to be stopped",
            },
            {"code", "runtime_running"},
            {"currentEtag", snapshot.etag},
        }).dump(),
        409,
        snapshot.etag);
}

std::vector<std::string> active_acquisition_unit_ids(
    const pamguard::project::ActiveProjectSnapshot& snapshot) {
    std::vector<std::string> result;
    for (const auto& unit :
         snapshot.project.controlled_units) {
        if (unit.type_id == "pamguard.acquisition") {
            result.push_back(unit.id);
        }
    }
    return result;
}

std::vector<std::string> active_sound_recorder_unit_ids(
    const pamguard::project::ActiveProjectSnapshot& snapshot) {
    std::vector<std::string> result;
    for (const auto& unit :
         snapshot.project.controlled_units) {
        if (unit.type_id == "pamguard.sound-recorder") {
            result.push_back(unit.id);
        }
    }
    return result;
}

const pamguard::project::ControlledUnitInstance*
find_active_acquisition(
    const pamguard::project::ActiveProjectSnapshot& snapshot,
    const std::string_view unit_id) {
    const auto found = std::find_if(
        snapshot.project.controlled_units.begin(),
        snapshot.project.controlled_units.end(),
        [unit_id](const auto& unit) {
            return unit.id == unit_id;
        });
    if (found == snapshot.project.controlled_units.end() ||
        found->type_id != "pamguard.acquisition") {
        return nullptr;
    }
    return &*found;
}

const pamguard::project::ProjectedPublicOutput*
find_active_acquisition_audio_output(
    const pamguard::project::ActiveProjectSnapshot& snapshot,
    const std::string_view unit_id) {
    return snapshot.projection.index.find_public_output(
        unit_id,
        "rawAudio");
}

const pamguard::project::ControlledUnitInstance*
find_active_sound_recorder(
    const pamguard::project::ActiveProjectSnapshot& snapshot,
    const std::string_view unit_id) {
    const auto found = std::find_if(
        snapshot.project.controlled_units.begin(),
        snapshot.project.controlled_units.end(),
        [unit_id](const auto& unit) {
            return unit.id == unit_id;
        });
    if (found == snapshot.project.controlled_units.end() ||
        found->type_id != "pamguard.sound-recorder") {
        return nullptr;
    }
    return &*found;
}

const pamguard::project::ProjectedRuntimeNode*
find_active_sound_recorder_runtime(
    const pamguard::project::ActiveProjectSnapshot& snapshot,
    const std::string_view unit_id) {
    return snapshot.projection.index.find_runtime_node(
        unit_id,
        "recorder-process");
}

std::string sound_recorder_transport_name(
    const pamguard::core::SoundRecorderTransportState state) {
    switch (state) {
    case pamguard::core::SoundRecorderTransportState::Off:
        return "off";
    case pamguard::core::SoundRecorderTransportState::Continuous:
        return "continuous";
    }
    throw std::logic_error(
        "Unknown sound-recorder transport state");
}

const pamguard::project::ControlledUnitInstance*
find_active_click_detector(
    const pamguard::project::ActiveProjectSnapshot& snapshot,
    const std::string_view unit_id) {
    const auto found = std::find_if(
        snapshot.project.controlled_units.begin(),
        snapshot.project.controlled_units.end(),
        [unit_id](const auto& unit) {
            return unit.id == unit_id;
        });
    if (found == snapshot.project.controlled_units.end() ||
        found->type_id != "pamguard.click-detector") {
        return nullptr;
    }
    return &*found;
}

std::string project_mode_name(
    const pamguard::project::ProjectMode mode) {
    switch (mode) {
    case pamguard::project::ProjectMode::Normal:
        return "normal";
    case pamguard::project::ProjectMode::Mixed:
        return "mixed";
    case pamguard::project::ProjectMode::Viewer:
        return "viewer";
    }
    throw std::logic_error("Unknown project mode");
}

pamguard::service::TrackedClickLocaliserSettings
tracked_click_localiser_settings(
    const pamguard::project::ControlledUnitInstance& unit) {
    const auto settings = json::parse(unit.settings_json);
    const auto& tracked =
        settings.at("localisation").at("trackedTrain");
    pamguard::service::TrackedClickLocaliserSettings result;
    result.is_selected.clear();
    for (const auto& selected : tracked.at("isSelected")) {
        result.is_selected.push_back(selected.get<bool>());
    }
    result.max_range_m = tracked.at("maxRangeM").get<double>();
    result.max_height_m = tracked.at("maxHeightM").get<double>();
    result.min_height_m = tracked.at("minHeightM").get<double>();
    result.max_time_milliseconds =
        tracked.at("maxTimeMilliseconds").get<std::uint64_t>();
    result.limit_points = tracked.at("limitPoints").get<bool>();
    result.max_points =
        tracked.at("maxPoints").get<std::size_t>();
    return result;
}

json tracked_click_localiser_settings_to_json(
    const pamguard::service::TrackedClickLocaliserSettings& settings) {
    return {
        {"isSelected", settings.is_selected},
        {"maxRangeM", settings.max_range_m},
        {"maxHeightM", settings.max_height_m},
        {"minHeightM", settings.min_height_m},
        {
            "maxTimeMilliseconds",
            settings.max_time_milliseconds,
        },
        {"limitPoints", settings.limit_points},
        {"maxPoints", settings.max_points},
    };
}

json tracked_click_assessment_to_json(
    const pamguard::service::
        TrackedClickLocalisationAssessment& assessment) {
    json algorithms = json::array();
    for (const auto& algorithm : assessment.algorithms) {
        algorithms.push_back({
            {"javaIndex", algorithm.java_index},
            {"id", algorithm.id},
            {"javaName", algorithm.java_name},
            {"selected", algorithm.selected},
            {"available", algorithm.available},
            {
                "unavailableReason",
                algorithm.available
                    ? json(nullptr)
                    : json(algorithm.unavailable_reason),
            },
        });
    }
    return {
        {
            "status",
            pamguard::service::
                tracked_click_localisation_status_name(
                    assessment.status),
        },
        {"available", assessment.available()},
        {"code", assessment.code},
        {"message", assessment.message},
        {"algorithms", std::move(algorithms)},
    };
}

const char* tracked_target_motion_status_name(
    const pamguard::localisation::TrackedTargetMotionStatus status) {
    using Status =
        pamguard::localisation::TrackedTargetMotionStatus;
    switch (status) {
    case Status::success:
        return "success";
    case Status::invalid_point_limit:
        return "invalid_point_limit";
    case Status::non_finite_input:
        return "non_finite_input";
    case Status::no_observations:
        return "no_observations";
    case Status::degenerate_fit:
        return "degenerate_fit";
    case Status::non_convergent_bearings:
        return "non_convergent_bearings";
    }
    return "unknown";
}

json finite_number_or_null(const double value) {
    return std::isfinite(value) ? json(value) : json(nullptr);
}

json tracked_click_localisation_run_to_json(
    const pamguard::service::TrackedClickLocalisationRun& run) {
    json ambiguities = json::array();
    for (const auto& ambiguity : run.ambiguities) {
        const auto& fit = ambiguity.fit;
        json item = {
            {"ambiguityIndex", ambiguity.ambiguity_index},
            {"accepted", ambiguity.accepted()},
            {
                "fit",
                {
                    {
                        "status",
                        tracked_target_motion_status_name(
                            fit.status),
                    },
                    {"succeeded", fit.succeeded()},
                    {
                        "positionMetres",
                        {
                            finite_number_or_null(
                                fit.position_metres[0]),
                            finite_number_or_null(
                                fit.position_metres[1]),
                            finite_number_or_null(
                                fit.position_metres[2]),
                        },
                    },
                    {
                        "rawChi2",
                        finite_number_or_null(fit.raw_chi2),
                    },
                    {
                        "reducedChi2",
                        finite_number_or_null(
                            fit.reduced_chi2),
                    },
                    {"aic", finite_number_or_null(fit.aic)},
                    {
                        "perpendicularErrorMetres",
                        finite_number_or_null(
                            fit.
                                perpendicular_error_metres),
                    },
                    {
                        "parallelErrorMetres",
                        finite_number_or_null(
                            fit.parallel_error_metres),
                    },
                    {
                        "errorAngleRadians",
                        finite_number_or_null(
                            fit.error_angle_radians),
                    },
                    {
                        "referenceObservationIndex",
                        fit.reference_observation_index ==
                                std::numeric_limits<
                                    std::size_t>::max()
                            ? json(nullptr)
                            : json(
                                  fit.
                                      reference_observation_index),
                    },
                    {
                        "selectedObservationIndices",
                        fit.selected_observation_indices,
                    },
                },
            },
            {
                "beamSampleTimeMs",
                ambiguity.beam_sample_time_ms
                    ? json(*ambiguity.beam_sample_time_ms)
                    : json(nullptr),
            },
            {
                "beamDistanceMetres",
                ambiguity.beam_distance_metres
                    ? finite_number_or_null(
                          *ambiguity.beam_distance_metres)
                    : json(nullptr),
            },
        };
        if (ambiguity.filter_input) {
            item["filterInput"] = {
                {
                    "perpendicularDistanceMetres",
                    finite_number_or_null(
                        ambiguity.filter_input->
                            perpendicular_distance_metres),
                },
                {
                    "heightMetres",
                    finite_number_or_null(
                        ambiguity.filter_input->
                            height_metres),
                },
            };
        }
        else {
            item["filterInput"] = nullptr;
        }
        if (ambiguity.filter_assessment) {
            item["filterAssessment"] = {
                {
                    "passesRunawayGuard",
                    ambiguity.filter_assessment->
                        passes_runaway_guard,
                },
                {
                    "passesConfiguredLimits",
                    ambiguity.filter_assessment->
                        passes_configured_limits,
                },
                {
                    "accepted",
                    ambiguity.filter_assessment->accepted,
                },
            };
        }
        else {
            item["filterAssessment"] = nullptr;
        }
        ambiguities.push_back(std::move(item));
    }
    return {
        {
            "status",
            pamguard::service::
                tracked_click_localisation_run_status_name(
                    run.status),
        },
        {"executed", run.executed()},
        {"accepted", run.accepted()},
        {"code", run.code},
        {"message", run.message},
        {
            "assessment",
            tracked_click_assessment_to_json(run.assessment),
        },
        {"ambiguities", std::move(ambiguities)},
    };
}

json tracked_click_event_to_json(
    const pamguard::service::TrackedClickEvent& event,
    const pamguard::service::
        TrackedClickLocalisationAssessment& assessment) {
    json clicks = json::array();
    for (const auto& click : event.clicks) {
        clicks.push_back({
            {"uid", click.uid},
            {"startSample", click.start_sample},
            {"timeMs", click.time_ms},
            {"channelBitmap", click.channel_bitmap},
            {
                "bearingRadians",
                click.bearing_radians
                    ? json(*click.bearing_radians)
                    : json(nullptr),
            },
            {
                "originMetres",
                click.origin_metres
                    ? json(*click.origin_metres)
                    : json(nullptr),
            },
            {
                "headingRadians",
                click.heading_radians
                    ? json(*click.heading_radians)
                    : json(nullptr),
            },
            {
                "earthBearingAmbiguitiesRadians",
                click.earth_bearing_ambiguities_radians,
            },
            {
                "navigationReferenceId",
                click.navigation_reference_id.empty()
                    ? json(nullptr)
                    : json(click.navigation_reference_id),
            },
        });
    }
    return {
        {"eventId", event.event_id},
        {"comment", event.comment},
        {"clickCount", event.clicks.size()},
        {
            "startTimeMs",
            event.clicks.empty()
                ? json(nullptr)
                : json(event.clicks.front().time_ms),
        },
        {
            "endTimeMs",
            event.clicks.empty()
                ? json(nullptr)
                : json(event.clicks.back().time_ms),
        },
        {"clicks", std::move(clicks)},
        {
            "localisation",
            tracked_click_assessment_to_json(assessment),
        },
    };
}

std::vector<pamguard::service::TrackedClickObservation>
resolve_retained_clicks(
    const json& requested_clicks,
    const std::shared_ptr<pamguard::core::DataBlock>& click_block) {
    if (!requested_clicks.is_array() ||
        requested_clicks.empty() ||
        requested_clicks.size() > 4096) {
        throw std::invalid_argument(
            "clicks must contain between 1 and 4096 retained-click "
            "locators");
    }
    const auto history = click_block->recent_history();
    std::vector<pamguard::service::TrackedClickObservation> result;
    result.reserve(requested_clicks.size());
    for (const auto& locator : requested_clicks) {
        if (!locator.is_object() ||
            locator.size() != 3 ||
            !locator.contains("uid") ||
            !locator.contains("startSample") ||
            !locator.contains("channelBitmap")) {
            throw std::invalid_argument(
                "Each retained-click locator must contain only uid, "
                "startSample, and channelBitmap");
        }
        const auto uid = locator.at("uid").get<std::uint64_t>();
        const auto start_sample =
            locator.at("startSample").get<std::int64_t>();
        const auto channel_bitmap =
            locator.at("channelBitmap").get<std::uint32_t>();
        const auto found = std::find_if(
            history.begin(),
            history.end(),
            [&](const auto& unit) {
                return unit.metadata.uid == uid &&
                    unit.metadata.start_sample == start_sample &&
                    unit.metadata.channel_bitmap == channel_bitmap;
            });
        if (found == history.end()) {
            throw std::out_of_range(
                "A requested click is no longer present in the retained "
                "Click Detector history");
        }
        const auto* click = std::any_cast<
            pamguard::detectors::ClickDetectionResult>(
                &found->payload);
        if (!click) {
            throw std::logic_error(
                "Click Detector public output contains a non-click "
                "payload");
        }
        pamguard::service::TrackedClickObservation observation{
            found->metadata.uid,
            found->metadata.start_sample,
            found->metadata.time_unix_ms,
            found->metadata.channel_bitmap,
            click->bearing_radians,
        };
        if (click->navigation_origin_declared) {
            observation.origin_metres =
                std::array<double, 3>{
                    click->navigation_origin_east_metres,
                    click->navigation_origin_north_metres,
                    click->navigation_origin_height_metres,
                };
            observation.navigation_reference_id =
                click->navigation_reference_id;
        }
        if (click->orientation_declared) {
            observation.heading_radians =
                click->orientation_heading_degrees *
                std::numbers::pi / 180.0;
        }
        observation.earth_bearing_ambiguities_radians =
            click->earth_bearing_ambiguities_radians;
        result.push_back(std::move(observation));
    }
    return result;
}

pamguard::service::AcquisitionCaptureTarget
acquisition_capture_target(
    const pamguard::project::ActiveProjectSnapshot& snapshot,
    std::string unit_id) {
    return {
        snapshot.project.project_id,
        std::move(unit_id),
        snapshot.working_revision,
    };
}

json acquisition_host_binding_to_json(
    const pamguard::service::
        AcquisitionHostBindingSnapshot& binding) {
    json source;
    if (const auto* device = std::get_if<
            pamguard::service::ExactAudioDeviceHostBinding>(
            &binding.source)) {
        source = {
            {"kind", "device"},
            {"deviceName", device->device_name},
        };
    }
    else {
        const auto& url = std::get<
            pamguard::service::NonSecretHttpUrlHostBinding>(
            binding.source);
        source = {
            {"kind", "url"},
            {"url", url.url},
        };
    }
    return {
        {"schemaVersion", 1},
        {"projectId", binding.target.project_id},
        {
            "acquisitionUnitId",
            binding.target.acquisition_unit_id,
        },
        {
            "workingRevision",
            binding.target.working_revision,
        },
        {"bindingRevision", binding.binding_revision},
        {"source", std::move(source)},
    };
}

void working_revision_conflict_response(
    httplib::Response& res,
    const std::uint64_t requested,
    const std::uint64_t current) {
    json_response(
        res,
        {
            {
                "error",
                "The active project working revision changed",
            },
            {"code", "working_revision_conflict"},
            {"requestedWorkingRevision", requested},
            {"currentWorkingRevision", current},
        },
        409);
}

void active_project_mismatch_response(
    httplib::Response& res,
    const std::string& requested,
    const std::string& current) {
    json_response(
        res,
        {
            {
                "error",
                "The expected project is not active",
            },
            {"code", "active_project_mismatch"},
            {"requestedProjectId", requested},
            {"currentProjectId", current},
        },
        409);
}

void acquisition_binding_conflict_response(
    httplib::Response& res,
    const pamguard::service::
        AcquisitionHostBindingConflict& error) {
    json_response(
        res,
        {
            {"error", error.what()},
            {"code", "binding_revision_conflict"},
            {
                "expectedBindingRevision",
                error.expected_binding_revision(),
            },
            {
                "currentBindingRevision",
                error.current_binding_revision()
                    ? json(*error.current_binding_revision())
                    : json(nullptr),
            },
        },
        409);
}

void stale_acquisition_target_response(
    httplib::Response& res,
    const pamguard::service::
        StaleAcquisitionCaptureTarget& error) {
    working_revision_conflict_response(
        res,
        error.requested_working_revision(),
        error.current_working_revision());
}

std::uint64_t required_json_uint64(
    const json& body,
    const char* name) {
    if (!body.contains(name) ||
        !body.at(name).is_number_unsigned()) {
        throw std::invalid_argument(
            std::string(name) +
            " is required and must be an unsigned integer");
    }
    return body.at(name).get<std::uint64_t>();
}

std::uint64_t required_query_uint64(
    const httplib::Request& req,
    const char* name) {
    if (!req.has_param(name)) {
        throw std::invalid_argument(
            std::string(name) +
            " query parameter is required");
    }
    const auto value = req.get_param_value(name);
    if (value.empty() || value.front() == '-') {
        throw std::invalid_argument(
            std::string(name) +
            " must be an unsigned integer");
    }
    std::size_t parsed = 0;
    const auto result = std::stoull(value, &parsed);
    if (parsed != value.size()) {
        throw std::invalid_argument(
            std::string(name) +
            " must be an unsigned integer");
    }
    return result;
}

std::string required_query_string(
    const httplib::Request& req,
    const char* name) {
    if (!req.has_param(name)) {
        throw std::invalid_argument(
            std::string(name) +
            " query parameter is required");
    }
    const auto value = req.get_param_value(name);
    if (value.empty()) {
        throw std::invalid_argument(
            std::string(name) +
            " query parameter must not be empty");
    }
    return value;
}

double required_finite_query_double(
    const httplib::Request& req,
    const char* name) {
    if (!req.has_param(name)) {
        throw std::invalid_argument(
            std::string(name) +
            " query parameter is required");
    }
    const auto value = req.get_param_value(name);
    std::size_t parsed = 0;
    const double result = std::stod(value, &parsed);
    if (parsed != value.size() || !std::isfinite(result)) {
        throw std::invalid_argument(
            std::string(name) +
            " must be a finite number");
    }
    return result;
}

void require_working_revision(
    const std::uint64_t expected,
    const pamguard::project::ActiveProjectSnapshot& snapshot) {
    if (expected != snapshot.working_revision) {
        throw pamguard::service::StaleAcquisitionCaptureTarget(
            expected,
            snapshot.working_revision);
    }
}

bool capture_bridge_source_is_safe(
    const std::string_view source) noexcept {
    // ffmpeg_stream_ingest currently constructs FFmpeg's command through
    // popen. Keep shell expansion characters out of even an exact,
    // enumerated/bounded source until that bridge executes FFmpeg directly.
    return source.find_first_of("\"$`%!") ==
        std::string_view::npos;
}

/**
 * Constant-time equality so the comparison's timing does not leak how much of
 * a guessed key matched. Length still short-circuits: leaking the key LENGTH
 * is acceptable, leaking a prefix is not.
 */
bool constant_time_equals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        difference |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return difference == 0;
}

bool request_authorized(const httplib::Request& req, const std::string& api_key) {
    if (api_key.empty()) {
        return true;
    }
    if (req.has_header("X-API-Key") && constant_time_equals(req.get_header_value("X-API-Key"), api_key)) {
        return true;
    }
    if (req.has_header("Authorization")) {
        const std::string authorization = req.get_header_value("Authorization");
        constexpr std::string_view bearer = "Bearer ";
        if (authorization.rfind(bearer, 0) == 0 &&
            constant_time_equals(authorization.substr(bearer.size()), api_key)) {
            return true;
        }
    }
    return false;
}

bool require_authorized(const httplib::Request& req, httplib::Response& res, const std::string& api_key) {
    if (request_authorized(req, api_key)) {
        return true;
    }
    res.set_header("WWW-Authenticate", "Bearer");
    json_response(res, {{"error", "unauthorized"}}, 401);
    return false;
}

void append_audit_event(const std::filesystem::path& audit_log_file, std::mutex& audit_mutex, json event) {
    if (audit_log_file.empty()) {
        return;
    }
    try {
        event["timeMs"] = current_unix_ms();
        const auto parent = audit_log_file.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        std::lock_guard lock(audit_mutex);
        std::ofstream output(audit_log_file, std::ios::app);
        if (!output) {
            throw std::runtime_error("could not open audit log file");
        }
        output << event.dump() << '\n';
    }
    catch (const std::exception& error) {
        std::cerr << "Audit log write failed: " << error.what() << "\n";
    }
}

std::string prometheus_label_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

void append_ingest_status_metrics(std::ostringstream& metrics, const std::filesystem::path& ingest_status_file) {
    metrics << "pamguard_ingest_status_configured " << (ingest_status_file.empty() ? 0 : 1) << "\n";
    if (ingest_status_file.empty()) {
        metrics << "pamguard_ingest_status_file_exists 0\n";
        metrics << "pamguard_ingest_status_parse_error 0\n";
        return;
    }

    std::error_code exists_error;
    const bool exists = std::filesystem::is_regular_file(ingest_status_file, exists_error);
    metrics << "pamguard_ingest_status_file_exists " << (!exists_error && exists ? 1 : 0) << "\n";
    if (exists_error || !exists) {
        metrics << "pamguard_ingest_status_parse_error 0\n";
        return;
    }

    try {
        std::ifstream input(ingest_status_file);
        if (!input) {
            throw std::runtime_error("unable to open ingest status file");
        }
        const auto status = json::parse(input);
        metrics << "pamguard_ingest_status_parse_error 0\n";

        const auto workers = status.contains("workers") && status["workers"].is_array()
            ? status["workers"]
            : json::array();
        const auto worker_count = status.contains("workerCount") && status["workerCount"].is_number()
            ? status["workerCount"].get<double>()
            : static_cast<double>(workers.size());
        metrics << "pamguard_ingest_workers " << worker_count << "\n";

        if (status.contains("healthCounts") && status["healthCounts"].is_object()) {
            for (const auto& [health, value] : status["healthCounts"].items()) {
                if (value.is_number()) {
                    metrics << "pamguard_ingest_health_count{health=\"" << prometheus_label_escape(health) << "\"} "
                            << value.get<double>() << "\n";
                }
            }
        }
        if (status.contains("statusCounts") && status["statusCounts"].is_object()) {
            for (const auto& [worker_status, value] : status["statusCounts"].items()) {
                if (value.is_number()) {
                    metrics << "pamguard_ingest_status_count{status=\"" << prometheus_label_escape(worker_status) << "\"} "
                            << value.get<double>() << "\n";
                }
            }
        }

        for (const auto& worker : workers) {
            if (!worker.is_object()) {
                continue;
            }
            const auto source_id = worker.value("sourceId", std::string());
            const auto session_id = worker.value("sessionId", std::string());
            const auto worker_status = worker.value("status", std::string());
            const auto health = worker.value("health", std::string());
            const auto labels = std::string("source=\"") + prometheus_label_escape(source_id)
                + "\",session=\"" + prometheus_label_escape(session_id)
                + "\",status=\"" + prometheus_label_escape(worker_status)
                + "\",health=\"" + prometheus_label_escape(health) + "\"";

            metrics << "pamguard_ingest_worker_health{" << labels << "} " << (health == "healthy" ? 1 : 0) << "\n";
            if (worker.contains("restarts") && worker["restarts"].is_number()) {
                metrics << "pamguard_ingest_worker_restarts{" << labels << "} " << worker["restarts"].get<double>() << "\n";
            }
            if (worker.contains("uptimeMs") && worker["uptimeMs"].is_number()) {
                metrics << "pamguard_ingest_worker_uptime_ms{" << labels << "} " << worker["uptimeMs"].get<double>() << "\n";
            }
            if (worker.contains("lastObservedUnixMs") && worker["lastObservedUnixMs"].is_number()) {
                metrics << "pamguard_ingest_worker_last_observed_unix_ms{" << labels << "} "
                        << worker["lastObservedUnixMs"].get<double>() << "\n";
            }
        }
    }
    catch (...) {
        metrics << "pamguard_ingest_status_parse_error 1\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    const int port = argc > 1 ? std::stoi(argv[1]) : 8080;
    const auto max_sessions = max_sessions_from_environment();
    const auto max_pcm_body_bytes = max_pcm_body_bytes_from_environment();
    const auto max_archive_query_records = max_archive_query_records_from_environment();
    const auto http_threads = http_threads_from_environment();
    const auto require_session_metadata = bool_from_environment("PAMGUARD_REQUIRE_SESSION_METADATA");
    const auto session_config_dir = session_config_dir_from_environment();
    const auto result_archive_dir = result_archive_dir_from_environment();
    const auto ingest_status_file = ingest_status_file_from_environment();
    const auto audit_log_file = audit_log_file_from_environment();
    const auto web_ui_file = web_ui_file_from_environment();
    std::filesystem::path web_asset_root;
    try {
        web_asset_root =
            web_asset_root_from_environment(web_ui_file);
    }
    catch (const std::exception& error) {
        std::cerr << "Invalid web asset configuration: "
                  << error.what() << "\n";
        return 2;
    }
    const auto openapi_file = openapi_file_from_environment();
    const auto module_graph_file = module_graph_file_from_environment();
    const auto workspace_file = workspace_file_from_environment();
    const auto project_dir = project_dir_from_environment();
    const auto sound_recorder_deployment =
        sound_recorder_deployment_from_environment();
    const auto requested_active_project_id =
        active_project_id_from_environment();
    const bool legacy_model_compat =
        bool_from_environment(
            "PAMGUARD_LEGACY_MODEL_COMPAT");
    const auto cors_origin = cors_origin_from_environment();
    const auto api_key = api_key_from_environment();

    const bool capture_enabled = bool_from_environment("PAMGUARD_CAPTURE_ENABLED");
    const auto ffmpeg_path = ffmpeg_path_from_environment();
    const auto ingest_exe = ingest_exe_path(argc > 0 ? argv[0] : nullptr);
    CaptureState capture_state;
    pamguard::service::AcquisitionHostBindingStore
        acquisition_host_bindings;
    std::unordered_map<
        std::string,
        std::unique_ptr<
            pamguard::service::TrackedClickEventStore>>
        tracked_click_events;
    std::unordered_map<
        std::string,
        ActiveProjectNavigationTrack>
        project_navigation_tracks;
#ifdef _WIN32
    if (capture_enabled && !api_key.empty()) {
        // The spawned ingest bridge reads the key from the inherited
        // environment rather than the (inspectable) command line.
        SetEnvironmentVariableA("PAMGUARD_CAPTURE_API_KEY", api_key.c_str());
    }
#else
    if (capture_enabled && !api_key.empty()) {
        (void)setenv(
            "PAMGUARD_CAPTURE_API_KEY",
            api_key.c_str(),
            1);
    }
#endif

    const auto job_audio_dir = job_audio_dir_from_environment();
    const auto audio_archive_dir = audio_archive_dir_from_environment();
    std::mutex audio_archive_mutex;
    const auto result_feed_depth = result_feed_depth_from_environment();
    std::mutex result_feed_mutex;
    std::condition_variable result_feed_cv;
    std::unordered_map<std::string, SessionResultFeed> result_feeds;
    const auto job_workers = job_workers_from_environment();
    JobQueueState job_state;

    pamguard::core::SessionManager manager;
    pamguard::core::ModuleRegistry module_registry;
    pamguard::core::register_builtin_module_types(module_registry);
    if (legacy_model_compat &&
        !requested_active_project_id.empty()) {
        std::cerr
            << "PAMGUARD_LEGACY_MODEL_COMPAT and "
               "PAMGUARD_ACTIVE_PROJECT_ID are mutually exclusive\n";
        return 2;
    }
    pamguard::project::ControlledUnitRegistry controlled_unit_registry;
    pamguard::project::register_builtin_controlled_units(
        controlled_unit_registry);
    std::unique_ptr<pamguard::project::ProjectStore> project_store;
    std::unique_ptr<pamguard::project::ProjectAuthority>
        project_authority_owner;
    try {
        project_store =
            std::make_unique<pamguard::project::ProjectStore>(
                project_dir);
        project_authority_owner =
            std::make_unique<pamguard::project::ProjectAuthority>(
                controlled_unit_registry,
                module_registry,
                *project_store);
        if (!requested_active_project_id.empty()) {
            const auto current =
                project_authority_owner->snapshot();
            auto prepared =
                project_authority_owner->prepare_open(
                    current.etag,
                    requested_active_project_id,
                    true);
            preflight_project_runtime(
                prepared.preview(),
                sound_recorder_deployment);
            (void)project_authority_owner->
                commit_project_switch(std::move(prepared));
        }
    }
    catch (const std::exception& error) {
        std::cerr << "Could not initialize project authority: "
                  << error.what() << "\n";
        return 2;
    }
    auto& project_authority = *project_authority_owner;

    pamguard::core::ModuleGraph module_graph(module_registry);
    // This is the outer lifecycle lock for both graph/runtime transitions and
    // project, graph/runtime, and capture transitions. If capture state is
    // also needed, acquire this mutex first and CaptureState::mutex second.
    std::mutex module_graph_update_mutex;
    if (!legacy_model_compat &&
        !module_graph_file.empty()) {
        std::cerr
            << "PAMGUARD_MODULE_GRAPH_FILE is ignored: the active project "
               "is now the only writable graph authority\n";
    }
    if (legacy_model_compat &&
        !module_graph_file.empty() &&
        std::filesystem::exists(module_graph_file)) {
        try {
            const auto document =
                pamguard::core::module_graph_from_json(
                    read_text_file(module_graph_file));
            pamguard::core::ModuleRuntime candidate_runtime;
            candidate_runtime.configure(document);
            const auto restored =
                module_graph.restore(document);
            if (!restored.valid()) {
                throw std::runtime_error(
                    "persisted compatibility graph is invalid: " +
                    graph_issues_to_json(
                        restored.issues).dump());
            }
            std::cout
                << "Loaded isolated legacy compatibility graph revision "
                << document.revision << "\n";
        }
        catch (const std::exception& error) {
            std::cerr
                << "Could not load legacy compatibility graph "
                << module_graph_file.string() << ": "
                << error.what() << "\n";
        }
    }
    pamguard::core::ModuleRuntime module_runtime;
    bool project_runtime_prepared = false;
    try {
        if (legacy_model_compat) {
            module_runtime.configure(
                module_graph.snapshot());
        }
        else {
            const auto active =
                project_authority.snapshot();
            activate_project_runtime(
                active,
                sound_recorder_deployment,
                module_graph,
                module_runtime);
            project_runtime_prepared =
                project_runtime_deployment_ready(
                    active,
                    sound_recorder_deployment);
        }
    }
    catch (const std::exception& error) {
        std::cerr << "Could not prepare initial module runtime: "
                  << error.what() << "\n";
        return 2;
    }
    // Callers hold module_graph_update_mutex after HTTP serving begins. This
    // reconciles the binding registry from the one project authority and
    // ensures no project-owned child can survive a project, revision, unit,
    // runtime, or generated-node transition.
    const auto reconcile_acquisition_host_state =
        [&](const pamguard::project::ActiveProjectSnapshot& snapshot)
            -> std::size_t {
        const auto acquisition_ids =
            active_acquisition_unit_ids(snapshot);
        (void)acquisition_host_bindings.reconcile_project(
            snapshot.project.project_id,
            snapshot.working_revision,
            acquisition_ids);
        const std::unordered_set<std::string> active_ids(
            acquisition_ids.begin(),
            acquisition_ids.end());
        for (auto track = project_navigation_tracks.begin();
             track != project_navigation_tracks.end();) {
            if (!active_ids.contains(track->first) ||
                track->second.project_id !=
                    snapshot.project.project_id ||
                track->second.working_revision !=
                    snapshot.working_revision) {
                track =
                    project_navigation_tracks.erase(track);
            }
            else {
                ++track;
            }
        }
        std::size_t stopped = 0;
        std::lock_guard capture_lock(capture_state.mutex);
        for (auto capture = capture_state.running.begin();
             capture != capture_state.running.end();) {
            if (capture_process_running(capture->second)) {
                ++capture;
                continue;
            }
            const auto requirement =
                capture_state.required_project_captures.find(
                    capture->first);
            if (requirement !=
                capture_state.required_project_captures.end()) {
                requirement->second.child_failed = true;
            }
            close_capture_process(capture->second);
            capture = capture_state.running.erase(capture);
        }
        for (auto capture = capture_state.running.begin();
             capture != capture_state.running.end();) {
            const auto& value = capture->second;
            if (value.project_id.empty()) {
                ++capture;
                continue;
            }
            const auto* output =
                find_active_acquisition_audio_output(
                    snapshot,
                    value.acquisition_unit_id);
            const bool stale =
                value.project_id !=
                    snapshot.project.project_id ||
                !value.working_revision ||
                *value.working_revision !=
                    snapshot.working_revision ||
                !active_ids.contains(
                    value.acquisition_unit_id) ||
                !output ||
                value.module_id !=
                    output->runtime_node_id ||
                !module_runtime.running() ||
                module_runtime.revision() !=
                    snapshot.working_revision ||
                !module_runtime.find_block(
                    output->block_id);
            if (!stale) {
                ++capture;
                continue;
            }
            capture_state.required_project_captures.erase(
                capture->first);
            close_capture_process(capture->second);
            capture = capture_state.running.erase(capture);
            ++stopped;
        }
        for (auto requirement =
                 capture_state.required_project_captures.begin();
             requirement !=
                 capture_state.required_project_captures.end();) {
            const auto& target = requirement->second.target;
            const auto* output =
                find_active_acquisition_audio_output(
                    snapshot,
                    target.acquisition_unit_id);
            const bool stale =
                target.project_id !=
                    snapshot.project.project_id ||
                target.working_revision !=
                    snapshot.working_revision ||
                !active_ids.contains(
                    target.acquisition_unit_id) ||
                !output ||
                !module_runtime.running() ||
                module_runtime.revision() !=
                    snapshot.working_revision ||
                !module_runtime.find_block(output->block_id);
            if (!stale) {
                ++requirement;
                continue;
            }
            requirement =
                capture_state.required_project_captures.erase(
                    requirement);
        }
        return stopped;
    };
    (void)reconcile_acquisition_host_state(
        project_authority.snapshot());
    // Callers hold module_graph_update_mutex. Tracked events are runtime
    // scientific data tied to retained click UIDs, so a project graph
    // revision change clears membership exactly when those retained blocks
    // are replaced.
    const auto tracked_click_store =
        [&](const pamguard::project::ActiveProjectSnapshot& snapshot,
            const std::string& unit_id)
            -> pamguard::service::TrackedClickEventStore& {
        auto& store = tracked_click_events[unit_id];
        if (!store) {
            store = std::make_unique<
                pamguard::service::TrackedClickEventStore>();
        }
        store->reconcile({
            snapshot.project.project_id,
            unit_id,
            snapshot.working_revision,
        });
        return *store;
    };
    json workspace_store = {
        {"schemaVersion", 1},
        {"workspaces", json::object()},
    };
    std::mutex workspace_mutex;
    if (legacy_model_compat &&
        !workspace_file.empty() &&
        std::filesystem::exists(workspace_file)) {
        try {
            auto loaded = json::parse(read_text_file(workspace_file));
            if (!loaded.is_object() ||
                loaded.value("schemaVersion", 0) != 1 ||
                !loaded.contains("workspaces") ||
                !loaded.at("workspaces").is_object()) {
                throw std::invalid_argument(
                    "workspace store has an unsupported schema");
            }
            for (const auto& [id, layout] :
                 loaded.at("workspaces").items()) {
                if (!valid_workspace_id(id)) {
                    throw std::invalid_argument(
                        "workspace store contains an invalid id");
                }
                validate_workspace_layout(layout);
            }
            workspace_store = std::move(loaded);
            std::cout << "Loaded "
                      << workspace_store.at("workspaces").size()
                      << " persisted workspaces\n";
        }
        catch (const std::exception& error) {
            std::cerr << "Could not load persisted workspaces "
                      << workspace_file.string() << ": "
                      << error.what() << "\n";
        }
    }
    std::mutex configs_mutex;
    std::mutex archive_mutex;
    std::mutex audit_mutex;
    std::unordered_map<std::string, pamguard::core::AnalysisConfig> configs;
    std::unordered_map<std::string, SessionRuntimeStats> runtime_stats;

    if (legacy_model_compat &&
        !session_config_dir.empty() &&
        std::filesystem::exists(session_config_dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(session_config_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }
            try {
                std::ifstream input(entry.path());
                const auto body = json::parse(input);
                auto config = parse_config(body);
                const auto session_id = config.session_id;
                if (require_session_metadata && (config.owner_id.empty() || config.tenant_id.empty())) {
                    std::cerr << "Skipping persisted session " << session_id << ": ownerId and tenantId are required\n";
                    continue;
                }
                if (max_sessions > 0 && manager.session_count() >= max_sessions) {
                    std::cerr << "Skipping persisted session " << session_id << ": capacity reached\n";
                    continue;
                }
                manager.create_session(config);
                configs.emplace(session_id, std::move(config));
                runtime_stats.emplace(session_id, make_runtime_stats());
                std::cout << "Loaded persisted session config: " << session_id << "\n";
            }
            catch (const std::exception& error) {
                std::cerr << "Skipping persisted session config " << entry.path().string() << ": " << error.what() << "\n";
            }
        }
    }

    httplib::Server server;
    // A global body-size ceiling: without one, any JSON endpoint would accept
    // an arbitrarily large body before parsing. The PCM cap (when configured)
    // still applies its own, tighter check with a clear 413.
    const std::size_t payload_ceiling = max_pcm_body_bytes > 0
        ? max_pcm_body_bytes + (1u << 20)
        : (static_cast<std::size_t>(256) << 20);
    server.set_payload_max_length(payload_ceiling);

    if (http_threads > 0) {
        server.new_task_queue = [http_threads] {
            return new httplib::ThreadPool(http_threads);
        };
    }
    server.set_post_routing_handler([&](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", cors_origin);
        res.set_header(
            "Access-Control-Allow-Headers",
            "Content-Type, Authorization, X-API-Key, If-Match");
        res.set_header("Access-Control-Expose-Headers", "ETag");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    });

    server.Options(R"(.*)", [&](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
        res.set_header("Access-Control-Allow-Origin", cors_origin);
        res.set_header(
            "Access-Control-Allow-Headers",
            "Content-Type, Authorization, X-API-Key, If-Match");
        res.set_header("Access-Control-Expose-Headers", "ETag");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    });

    const auto request_etag =
        [](const httplib::Request& req) -> std::string {
        return req.has_header("If-Match")
            ? req.get_header_value("If-Match")
            : std::string{};
    };

    const auto require_project_authority_mode =
        [&](httplib::Response& res) -> bool {
        if (!legacy_model_compat) {
            return true;
        }
        json_response(
            res,
            {
                {
                    "error",
                    "The unified project authority is disabled while "
                    "isolated legacy model compatibility is active",
                },
                {"code", "legacy_model_compat_active"},
            },
            409);
        return false;
    };

    const auto require_legacy_analysis_compatibility =
        [&](httplib::Response& res) -> bool {
        if (legacy_model_compat) {
            res.set_header("Deprecation", "true");
            return true;
        }
        json_response(
            res,
            {
                {
                    "error",
                    "Legacy compatibility routes are not exposed in "
                    "project authority mode",
                },
                {"code", "legacy_compatibility_required"},
            },
            404);
        return false;
    };

    const auto restore_project_runtime =
        [&](const pamguard::project::ActiveProjectSnapshot& snapshot,
            bool restart) -> std::string {
        try {
            activate_project_runtime(
                snapshot,
                sound_recorder_deployment,
                module_graph,
                module_runtime);
            project_runtime_prepared =
                project_runtime_deployment_ready(
                    snapshot,
                    sound_recorder_deployment);
            if (restart) {
                if (!project_runtime_prepared) {
                    throw std::runtime_error(
                        "previous project is not runnable");
                }
                module_runtime.start();
            }
            return {};
        }
        catch (const std::exception& error) {
            return error.what();
        }
    };

    server.Get(
        "/v1/controlled-unit-types",
        [&](const httplib::Request& req, httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            try {
                encoded_json_response(
                    res,
                    pamguard::project::
                        controlled_unit_catalogue_to_json(
                            controlled_unit_registry));
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "catalogue_unavailable"},
                    },
                    500);
            }
        });

    server.Get(
        "/v1/projects",
        [&](const httplib::Request& req, httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            try {
                encoded_json_response(
                    res,
                    pamguard::project::saved_project_list_to_json(
                        project_store->list()));
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "project_store_error"},
                    },
                    500);
            }
        });

    server.Get(
        "/v1/projects/active",
        [&](const httplib::Request& req, httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            std::lock_guard lifecycle_lock(
                module_graph_update_mutex);
            const auto snapshot =
                project_authority.snapshot();
            encoded_json_response(
                res,
                pamguard::project::
                    active_project_snapshot_to_json(snapshot),
                200,
                snapshot.etag);
        });

    server.Get(
        "/v1/projects/active/inspection",
        [&](const httplib::Request& req, httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            std::lock_guard lifecycle_lock(
                module_graph_update_mutex);
            const auto snapshot =
                project_authority.snapshot();
            encoded_json_response(
                res,
                pamguard::project::project_inspection_to_json(
                    snapshot),
                200,
                snapshot.etag);
        });

    server.Get(
        "/v1/projects/active/compatible-sources",
        [&](const httplib::Request& req, httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            if (!req.has_param("unitId") ||
                !req.has_param("inputRole")) {
                json_response(
                    res,
                    {
                        {
                            "error",
                            "unitId and inputRole query parameters "
                            "are required",
                        },
                        {"code", "invalid_query"},
                    },
                    400);
                return;
            }
            const auto unit_id =
                req.get_param_value("unitId");
            const auto input_role =
                req.get_param_value("inputRole");
            try {
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto sources =
                    project_authority.compatible_sources(
                        unit_id,
                        input_role);
                const auto snapshot =
                    project_authority.snapshot();
                encoded_json_response(
                    res,
                    pamguard::project::
                        project_compatible_sources_to_json(
                            unit_id,
                            input_role,
                            sources),
                    200,
                    snapshot.etag);
            }
            catch (
                const pamguard::project::ProjectAuthorityError&
                    error) {
                project_authority_error_response(res, error);
            }
            catch (const std::invalid_argument& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_query"},
                    },
                    400);
            }
        });

    server.Post(
        "/v1/projects/active/mutations",
        [&](const httplib::Request& req, httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                const auto batch =
                    pamguard::project::
                        project_mutation_batch_from_json(
                            req.body);
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto before =
                    project_authority.snapshot();
                if (!batch.validate_only &&
                    !batch.operations.empty() &&
                    module_runtime.running()) {
                    project_runtime_running_response(
                        res,
                        before);
                    return;
                }

                auto prepared =
                    project_authority.prepare_mutation(
                        request_etag(req),
                        batch);
                const auto& preview = prepared.preview();
                if (batch.validate_only) {
                    encoded_json_response(
                        res,
                        pamguard::project::
                            project_mutation_result_to_json(
                                preview),
                        200,
                        before.etag);
                    return;
                }
                if (!preview.changed) {
                    auto result =
                        project_authority.commit_mutation(
                            std::move(prepared));
                    (void)reconcile_acquisition_host_state(
                        result.active);
                    encoded_json_response(
                        res,
                        pamguard::project::
                            project_mutation_result_to_json(
                                result),
                        200,
                        result.active.etag);
                    return;
                }

                auto prepared_runtime =
                    preflight_project_runtime(
                        preview.active,
                        sound_recorder_deployment);
                bool runtime_installed = false;
                try {
                    activate_prepared_project_runtime(
                        preview.active,
                        sound_recorder_deployment,
                        module_graph,
                        module_runtime,
                        *prepared_runtime);
                    runtime_installed = true;
                    auto result =
                        project_authority.commit_mutation(
                            std::move(prepared));
                    project_runtime_prepared =
                        project_runtime_deployment_ready(
                            result.active,
                            sound_recorder_deployment);
                    (void)reconcile_acquisition_host_state(
                        result.active);
                    append_audit_event(
                        audit_log_file,
                        audit_mutex,
                        {
                            {"event", "project_mutation"},
                            {
                                "projectId",
                                result.active.project.project_id,
                            },
                            {
                                "workingRevision",
                                result.active.working_revision,
                            },
                            {
                                "projectionStatus",
                                projection_status_name(
                                    result.active.projection),
                            },
                        });
                    encoded_json_response(
                        res,
                        pamguard::project::
                            project_mutation_result_to_json(
                                result),
                        200,
                        result.active.etag);
                }
                catch (...) {
                    std::string rollback_error;
                    if (runtime_installed) {
                        try {
                            activate_prepared_project_runtime(
                                before,
                                sound_recorder_deployment,
                                module_graph,
                                module_runtime,
                                *prepared_runtime);
                            project_runtime_prepared =
                                project_runtime_deployment_ready(
                                    before,
                                    sound_recorder_deployment);
                        }
                        catch (const std::exception& error) {
                            rollback_error = error.what();
                        }
                    }
                    else {
                        rollback_error =
                            restore_project_runtime(
                                before,
                                false);
                    }
                    if (!rollback_error.empty()) {
                        throw std::runtime_error(
                            "Project mutation failed and runtime "
                            "rollback also failed: " +
                            rollback_error);
                    }
                    throw;
                }
            }
            catch (
                const pamguard::project::
                    ProjectAuthorityJsonError& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_project_request"},
                    },
                    400);
            }
            catch (
                const pamguard::project::ProjectAuthorityError&
                    error) {
                project_authority_error_response(res, error);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "project_runtime_error"},
                    },
                    500);
            }
        });

    const auto activate_prepared_switch =
        [&](pamguard::project::PreparedProjectSwitch prepared,
            const char* audit_event,
            httplib::Response& res) {
        const auto before = project_authority.snapshot();
        const auto candidate = prepared.preview();
        auto prepared_runtime =
            preflight_project_runtime(
                candidate,
                sound_recorder_deployment);
        const bool was_running = module_runtime.running();
        std::size_t captures_stopped = 0;
        if (was_running) {
            captures_stopped =
                quiesce_module_captures(capture_state);
            module_runtime.stop();
        }
        bool runtime_installed = false;
        try {
            activate_prepared_project_runtime(
                candidate,
                sound_recorder_deployment,
                module_graph,
                module_runtime,
                *prepared_runtime);
            runtime_installed = true;
            auto active =
                project_authority.commit_project_switch(
                    std::move(prepared));
            project_runtime_prepared =
                project_runtime_deployment_ready(
                    active,
                    sound_recorder_deployment);
            (void)reconcile_acquisition_host_state(
                active);
            append_audit_event(
                audit_log_file,
                audit_mutex,
                {
                    {"event", audit_event},
                    {"projectId", active.project.project_id},
                    {"workingRevision", active.working_revision},
                    {"stoppedRuntime", was_running},
                    {"capturesStopped", captures_stopped},
                });
            encoded_json_response(
                res,
                pamguard::project::
                    active_project_snapshot_to_json(active),
                200,
                active.etag);
        }
        catch (...) {
            std::string rollback_error;
            if (runtime_installed) {
                try {
                    activate_prepared_project_runtime(
                        before,
                        sound_recorder_deployment,
                        module_graph,
                        module_runtime,
                        *prepared_runtime);
                    project_runtime_prepared =
                        project_runtime_deployment_ready(
                            before,
                            sound_recorder_deployment);
                    if (was_running) {
                        module_runtime.start();
                    }
                }
                catch (const std::exception& error) {
                    rollback_error = error.what();
                }
            }
            else {
                rollback_error =
                    restore_project_runtime(
                        before,
                        was_running);
            }
            if (!rollback_error.empty()) {
                throw std::runtime_error(
                    "Project switch failed and runtime rollback "
                    "also failed: " +
                    rollback_error);
            }
            throw;
        }
    };

    server.Post(
        "/v1/projects/active/new",
        [&](const httplib::Request& req, httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                const auto command =
                    pamguard::project::
                        new_project_request_from_json(req.body);
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                auto prepared =
                    project_authority.prepare_new_project(
                        request_etag(req),
                        command.name,
                        command.description,
                        command.discard_dirty);
                activate_prepared_switch(
                    std::move(prepared),
                    "project_new",
                    res);
            }
            catch (
                const pamguard::project::
                    ProjectAuthorityJsonError& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_project_request"},
                    },
                    400);
            }
            catch (
                const pamguard::project::ProjectAuthorityError&
                    error) {
                project_authority_error_response(res, error);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "project_switch_error"},
                    },
                    500);
            }
        });

    server.Post(
        "/v1/projects/active/open",
        [&](const httplib::Request& req, httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                const auto command =
                    pamguard::project::
                        open_project_request_from_json(req.body);
                if (!project_store->exists(
                        command.project_id)) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Saved project does not exist",
                            },
                            {"code", "project_not_found"},
                        },
                        404);
                    return;
                }
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                auto prepared =
                    project_authority.prepare_open(
                        request_etag(req),
                        command.project_id,
                        command.discard_dirty);
                activate_prepared_switch(
                    std::move(prepared),
                    "project_open",
                    res);
            }
            catch (
                const pamguard::project::
                    ProjectAuthorityJsonError& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_project_request"},
                    },
                    400);
            }
            catch (
                const pamguard::project::
                    UnsupportedProjectFile& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "unsupported_project"},
                    },
                    422);
            }
            catch (
                const pamguard::project::
                    CorruptProjectFile& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "corrupt_project"},
                    },
                    422);
            }
            catch (
                const pamguard::project::ProjectAuthorityError&
                    error) {
                project_authority_error_response(res, error);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "project_switch_error"},
                    },
                    500);
            }
        });

    server.Post(
        "/v1/projects/active/save",
        [&](const httplib::Request& req, httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            if (!req.body.empty()) {
                json_response(
                    res,
                    {
                        {
                            "error",
                            "Save does not accept a request body",
                        },
                        {"code", "invalid_project_request"},
                    },
                    400);
                return;
            }
            try {
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto active =
                    project_authority.save(
                        request_etag(req));
                append_audit_event(
                    audit_log_file,
                    audit_mutex,
                    {
                        {"event", "project_save"},
                        {
                            "projectId",
                            active.project.project_id,
                        },
                        {
                            "savedRevision",
                            active.saved_revision
                                ? json(*active.saved_revision)
                                : json(nullptr),
                        },
                    });
                encoded_json_response(
                    res,
                    pamguard::project::
                        active_project_snapshot_to_json(active),
                    200,
                    active.etag);
            }
            catch (
                const pamguard::project::ProjectFileConflict&
                    error) {
                const auto current =
                    project_authority.snapshot();
                encoded_json_response(
                    res,
                    json({
                        {"error", error.what()},
                        {"code", "saved_project_conflict"},
                        {"currentEtag", current.etag},
                    }).dump(),
                    409,
                    current.etag);
            }
            catch (
                const pamguard::project::ProjectAuthorityError&
                    error) {
                project_authority_error_response(res, error);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "project_save_error"},
                    },
                    500);
            }
        });

    server.Post(
        "/v1/projects/active/save-as",
        [&](const httplib::Request& req, httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                const auto command =
                    pamguard::project::
                        save_as_project_request_from_json(
                            req.body);
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto before =
                    project_authority.snapshot();
                if (module_runtime.running()) {
                    project_runtime_running_response(
                        res,
                        before);
                    return;
                }
                auto prepared =
                    project_authority.prepare_save_as(
                        request_etag(req),
                        command.name);
                const auto candidate =
                    prepared.preview();
                auto prepared_runtime =
                    preflight_project_runtime(
                        candidate,
                        sound_recorder_deployment);
                pamguard::project::ActiveProjectSnapshot active;
                bool runtime_installed = false;
                try {
                    activate_prepared_project_runtime(
                        candidate,
                        sound_recorder_deployment,
                        module_graph,
                        module_runtime,
                        *prepared_runtime);
                    runtime_installed = true;
                    active =
                        project_authority.commit_save_as(
                            std::move(prepared));
                    project_runtime_prepared =
                        project_runtime_deployment_ready(
                            active,
                            sound_recorder_deployment);
                    (void)reconcile_acquisition_host_state(
                        active);
                }
                catch (...) {
                    std::string rollback_error;
                    if (runtime_installed) {
                        try {
                            activate_prepared_project_runtime(
                                before,
                                sound_recorder_deployment,
                                module_graph,
                                module_runtime,
                                *prepared_runtime);
                            project_runtime_prepared =
                                project_runtime_deployment_ready(
                                    before,
                                    sound_recorder_deployment);
                        }
                        catch (const std::exception& error) {
                            rollback_error = error.what();
                        }
                    }
                    else {
                        rollback_error =
                            restore_project_runtime(
                                before,
                                false);
                    }
                    if (!rollback_error.empty()) {
                        throw std::runtime_error(
                            "Save As failed and runtime rollback "
                            "also failed: " +
                            rollback_error);
                    }
                    throw;
                }
                append_audit_event(
                    audit_log_file,
                    audit_mutex,
                    {
                        {"event", "project_save_as"},
                        {
                            "projectId",
                            active.project.project_id,
                        },
                        {
                            "savedRevision",
                            active.saved_revision
                                ? json(*active.saved_revision)
                                : json(nullptr),
                        },
                    });
                res.set_header(
                    "Location",
                    "/v1/projects/" +
                        active.project.project_id);
                encoded_json_response(
                    res,
                    pamguard::project::
                        active_project_snapshot_to_json(active),
                    201,
                    active.etag);
            }
            catch (
                const pamguard::project::
                    ProjectAuthorityJsonError& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_project_request"},
                    },
                    400);
            }
            catch (
                const pamguard::project::ProjectFileConflict&
                    error) {
                const auto current =
                    project_authority.snapshot();
                encoded_json_response(
                    res,
                    json({
                        {"error", error.what()},
                        {"code", "saved_project_conflict"},
                        {"currentEtag", current.etag},
                    }).dump(),
                    409,
                    current.etag);
            }
            catch (
                const pamguard::project::ProjectAuthorityError&
                    error) {
                project_authority_error_response(res, error);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "project_save_error"},
                    },
                    500);
            }
        });

    // Register this parameterized route after every fixed active-project
    // route so "active" can never be interpreted as a durable project ID.
    server.Get(
        R"(/v1/projects/([^/]+))",
        [&](const httplib::Request& req, httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            const auto project_id =
                req.matches[1].str();
            try {
                if (!pamguard::project::is_uuid_v4(project_id)) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "projectId must be a lowercase UUIDv4",
                            },
                            {"code", "invalid_project_id"},
                        },
                        400);
                    return;
                }
                if (!project_store->exists(project_id)) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Saved project does not exist",
                            },
                            {"code", "project_not_found"},
                        },
                        404);
                    return;
                }
                const auto loaded =
                    project_store->load(project_id);
                const auto etag =
                    pamguard::project::project_authority_etag(
                        project_id,
                        loaded.envelope.authority_revision,
                        loaded.envelope.content_hash,
                        loaded.envelope.content_hash);
                encoded_json_response(
                    res,
                    pamguard::project::
                        project_file_envelope_to_json(
                            loaded.envelope),
                    200,
                    etag);
            }
            catch (
                const pamguard::project::
                    UnsupportedProjectFile& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "unsupported_project"},
                    },
                    422);
            }
            catch (
                const pamguard::project::
                    CorruptProjectFile& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "corrupt_project"},
                    },
                    422);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "project_store_error"},
                    },
                    500);
            }
        });

    // ---- live sound-card capture (opt-in, Windows/DirectShow) ----

    const auto list_capture_devices = [&](std::string& error)
        -> std::vector<std::pair<std::string, std::string>> {
#ifdef _WIN32
        std::string output;
        if (!run_command_capture({ffmpeg_path, "-hide_banner", "-list_devices", "true",
                                  "-f", "dshow", "-i", "dummy"}, output)) {
            error = "ffmpeg could not be started (" + ffmpeg_path +
                "); install ffmpeg or set PAMGUARD_FFMPEG_PATH";
            return {};
        }
        return parse_dshow_devices(output);
#else
        error = "sound-card capture is only implemented for Windows/DirectShow";
        return {};
#endif
    };

    const auto stop_acquisition_capture =
        [&](const pamguard::service::AcquisitionCaptureTarget&
                target) -> bool {
        const auto key =
            pamguard::service::
                acquisition_capture_target_key(target);
        std::lock_guard capture_lock(capture_state.mutex);
        const bool was_required =
            capture_state.required_project_captures.erase(key) != 0;
        const auto found = capture_state.running.find(key);
        if (found == capture_state.running.end()) {
            return was_required;
        }
        close_capture_process(found->second);
        capture_state.running.erase(found);
        return true;
    };

    const auto acquisition_not_found_response =
        [](httplib::Response& res) {
        json_response(
            res,
            {
                {
                    "error",
                    "The unit is not an active Acquisition "
                    "controlled-unit instance",
                },
                {"code", "acquisition_not_found"},
            },
            404);
    };

    const auto sound_recorder_not_found_response =
        [](httplib::Response& res) {
        json_response(
            res,
            {
                {
                    "error",
                    "The unit is not an active Sound Recorder "
                    "controlled-unit instance",
                },
                {"code", "sound_recorder_not_found"},
            },
            404);
    };

    const auto sound_recorder_status_document =
        [&](const pamguard::project::ActiveProjectSnapshot& snapshot,
            const std::string& unit_id) -> json {
        const bool runtime_prepared =
            project_runtime_prepared &&
            module_runtime.revision() ==
                snapshot.working_revision;
        const bool runtime_running =
            runtime_prepared && module_runtime.running();
        json body = {
            {"schemaVersion", 1},
            {"projectId", snapshot.project.project_id},
            {"soundRecorderUnitId", unit_id},
            {"workingRevision", snapshot.working_revision},
            {
                "configurationReady",
                snapshot.projection.runnable(),
            },
            {
                "deploymentReady",
                sound_recorder_deployment.ready(),
            },
            {"runtimePrepared", runtime_prepared},
            {"runtimeRunning", runtime_running},
            {"transport", "off"},
            {"fileOpen", false},
            {"currentFileName", nullptr},
            {"framesInCurrentFile", 0},
            {"completedFileCount", 0},
            {"selectedChannelBitmap", 0},
            {"sampleRateHz", 0},
            {"channelCount", 0},
            {"bitDepth", 16},
        };
        if (!sound_recorder_deployment.ready()) {
            body["deploymentError"] =
                sound_recorder_deployment.readiness_error;
        }
        const auto* projected =
            find_active_sound_recorder_runtime(
                snapshot,
                unit_id);
        if (!runtime_prepared || projected == nullptr ||
            projected->runtime_type_id !=
                "pamguard.sound-recorder") {
            return body;
        }
        const auto status =
            module_runtime.sound_recorder_status(
                projected->runtime_node_id);
        body["transport"] =
            sound_recorder_transport_name(status.transport);
        body["fileOpen"] = status.file_open;
        body["currentFileName"] =
            status.file_open
            ? json(status.current_path.filename().string())
            : json(nullptr);
        body["framesInCurrentFile"] =
            status.frames_in_current_file;
        body["completedFileCount"] =
            status.completed_file_count;
        body["selectedChannelBitmap"] =
            status.selected_channel_bitmap;
        body["sampleRateHz"] = status.sample_rate_hz;
        body["channelCount"] = status.channel_count;
        body["bitDepth"] = status.bit_depth;
        return body;
    };

    server.Get(
        R"(/v1/projects/active/sound-recorders/([^/]+)/status)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                const auto unit_id = req.matches[1].str();
                if (!find_active_sound_recorder(
                        snapshot,
                        unit_id)) {
                    sound_recorder_not_found_response(res);
                    return;
                }
                encoded_json_response(
                    res,
                    sound_recorder_status_document(
                        snapshot,
                        unit_id).dump(),
                    200,
                    snapshot.etag);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "sound_recorder_status_error"},
                    },
                    500);
            }
        });

    server.Put(
        R"(/v1/projects/active/sound-recorders/([^/]+)/transport)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                const auto body = json::parse(req.body);
                if (!body.is_object() ||
                    body.size() != 2 ||
                    !body.contains("transport") ||
                    !body.at("transport").is_string()) {
                    throw std::invalid_argument(
                        "Sound Recorder transport body must "
                        "contain only expectedWorkingRevision "
                        "and transport");
                }
                const auto expected_working_revision =
                    required_json_uint64(
                        body,
                        "expectedWorkingRevision");
                const auto requested_name =
                    body.at("transport").get<std::string>();
                const bool unsupported =
                    requested_name == "cycle" ||
                    requested_name == "restore-last";
                pamguard::core::SoundRecorderTransportState
                    requested =
                        pamguard::core::
                            SoundRecorderTransportState::Off;
                if (requested_name == "continuous") {
                    requested =
                        pamguard::core::
                            SoundRecorderTransportState::
                                Continuous;
                }
                else if (requested_name != "off" &&
                         !unsupported) {
                    throw std::invalid_argument(
                        "transport must be off, continuous, "
                        "cycle, or restore-last");
                }

                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                require_working_revision(
                    expected_working_revision,
                    snapshot);
                const auto unit_id = req.matches[1].str();
                if (!find_active_sound_recorder(
                        snapshot,
                        unit_id)) {
                    sound_recorder_not_found_response(res);
                    return;
                }
                if (unsupported) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Cycle and restore-last transport "
                                "are not implemented yet",
                            },
                            {
                                "code",
                                "sound_recorder_transport_unsupported",
                            },
                        },
                        501);
                    return;
                }
                if (!sound_recorder_deployment.ready()) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                sound_recorder_deployment.
                                    readiness_error,
                            },
                            {
                                "code",
                                "sound_recorder_storage_unavailable",
                            },
                        },
                        503);
                    return;
                }
                if (!project_runtime_prepared ||
                    module_runtime.revision() !=
                        snapshot.working_revision) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "The Sound Recorder runtime is "
                                "not prepared at the active "
                                "working revision",
                            },
                            {
                                "code",
                                "sound_recorder_runtime_unprepared",
                            },
                        },
                        409);
                    return;
                }
                if (!module_runtime.running()) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "The module runtime must be "
                                "running before Recorder "
                                "transport can change",
                            },
                            {
                                "code",
                                "sound_recorder_runtime_not_running",
                            },
                        },
                        409);
                    return;
                }
                const auto* projected =
                    find_active_sound_recorder_runtime(
                        snapshot,
                        unit_id);
                if (projected == nullptr ||
                    projected->runtime_type_id !=
                        "pamguard.sound-recorder") {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "The Sound Recorder runtime node "
                                "is unavailable",
                            },
                            {
                                "code",
                                "sound_recorder_runtime_unavailable",
                            },
                        },
                        409);
                    return;
                }
                const auto result =
                    module_runtime.set_sound_recorder_transport(
                        projected->runtime_node_id,
                        requested);
                if (result ==
                    pamguard::core::SoundRecorderCommandResult::
                        NodeNotRunning) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "The Sound Recorder node is not "
                                "running",
                            },
                            {
                                "code",
                                "sound_recorder_runtime_not_running",
                            },
                        },
                        409);
                    return;
                }
                if (result ==
                    pamguard::core::SoundRecorderCommandResult::
                        UnsupportedOperationMode) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "The requested Sound Recorder "
                                "transport is unsupported",
                            },
                            {
                                "code",
                                "sound_recorder_transport_unsupported",
                            },
                        },
                        501);
                    return;
                }
                append_audit_event(
                    audit_log_file,
                    audit_mutex,
                    {
                        {
                            "event",
                            "sound_recorder_transport_changed",
                        },
                        {
                            "projectId",
                            snapshot.project.project_id,
                        },
                        {"unitId", unit_id},
                        {
                            "workingRevision",
                            snapshot.working_revision,
                        },
                        {"transport", requested_name},
                        {
                            "alreadyInRequestedMode",
                            result ==
                                pamguard::core::
                                    SoundRecorderCommandResult::
                                        AlreadyInRequestedMode,
                        },
                    });
                auto response =
                    sound_recorder_status_document(
                        snapshot,
                        unit_id);
                response["commandResult"] =
                    result ==
                        pamguard::core::
                            SoundRecorderCommandResult::
                                AlreadyInRequestedMode
                    ? "already-in-requested-mode"
                    : "applied";
                encoded_json_response(
                    res,
                    response.dump(),
                    200,
                    snapshot.etag);
            }
            catch (
                const pamguard::service::
                    StaleAcquisitionCaptureTarget& error) {
                stale_acquisition_target_response(
                    res,
                    error);
            }
            catch (const json::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {
                            "code",
                            "invalid_sound_recorder_transport",
                        },
                    },
                    400);
            }
            catch (const std::invalid_argument& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {
                            "code",
                            "invalid_sound_recorder_transport",
                        },
                    },
                    400);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {
                            "code",
                            "sound_recorder_transport_error",
                        },
                    },
                    500);
            }
        });

    server.Get(
        R"(/v1/projects/active/click-detectors/([^/]+)/tracked-events)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                const auto unit_id = req.matches[1].str();
                const auto* unit =
                    find_active_click_detector(
                        snapshot,
                        unit_id);
                if (!unit) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Click Detector controlled unit "
                                "does not exist",
                            },
                            {"code", "click_detector_not_found"},
                        },
                        404);
                    return;
                }
                auto& store =
                    tracked_click_store(snapshot, unit_id);
                const auto settings =
                    tracked_click_localiser_settings(*unit);
                const auto mode =
                    project_mode_name(snapshot.project.mode);
                json events = json::array();
                for (const auto& event : store.events()) {
                    events.push_back(
                        tracked_click_event_to_json(
                            event,
                            store.assess_localisation(
                                event.event_id,
                                settings,
                                mode)));
                }
                const auto* output =
                    snapshot.projection.index
                        .find_public_output(
                            unit_id,
                            "clicks");
                encoded_json_response(
                    res,
                    json({
                        {"schemaVersion", 1},
                        {
                            "projectId",
                            snapshot.project.project_id,
                        },
                        {"workingRevision", snapshot.working_revision},
                        {"clickDetectorUnitId", unit_id},
                        {
                            "sourceBlockId",
                            output
                                ? json(output->block_id)
                                : json(nullptr),
                        },
                        {
                            "runtimeRunning",
                            module_runtime.running(),
                        },
                        {
                            "persistence",
                            "retained-runtime-data",
                        },
                        {
                            "settings",
                            tracked_click_localiser_settings_to_json(
                                settings),
                        },
                        {"events", std::move(events)},
                    }).dump(),
                    200,
                    snapshot.etag);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "tracked_event_state_error"},
                    },
                    500);
            }
        });

    server.Post(
        R"(/v1/projects/active/click-detectors/([^/]+)/tracked-events:assign)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                const auto body = json::parse(req.body);
                if (!body.is_object() ||
                    body.size() != 2 ||
                    !body.contains("clicks") ||
                    !body.contains("eventId")) {
                    throw std::invalid_argument(
                        "Assignment body must contain only clicks and "
                        "eventId");
                }
                std::optional<std::uint64_t> event_id;
                if (!body.at("eventId").is_null()) {
                    event_id =
                        body.at("eventId").get<std::uint64_t>();
                    if (*event_id == 0) {
                        throw std::invalid_argument(
                            "eventId must be positive or null");
                    }
                }
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                const auto unit_id = req.matches[1].str();
                const auto* unit =
                    find_active_click_detector(
                        snapshot,
                        unit_id);
                if (!unit) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Click Detector controlled unit "
                                "does not exist",
                            },
                            {"code", "click_detector_not_found"},
                        },
                        404);
                    return;
                }
                const auto* output =
                    snapshot.projection.index
                        .find_public_output(
                            unit_id,
                            "clicks");
                const auto block =
                    output
                    ? module_runtime.find_block(
                          output->block_id)
                    : nullptr;
                if (!block ||
                    module_runtime.revision() !=
                        snapshot.working_revision) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "The Click Detector retained-click "
                                "runtime is not prepared at the active "
                                "project revision",
                            },
                            {"code", "click_runtime_unavailable"},
                        },
                        409);
                    return;
                }
                auto& store =
                    tracked_click_store(snapshot, unit_id);
                const auto assigned = store.assign(
                    resolve_retained_clicks(
                        body.at("clicks"),
                        block),
                    event_id);
                const auto settings =
                    tracked_click_localiser_settings(*unit);
                encoded_json_response(
                    res,
                    tracked_click_event_to_json(
                        assigned,
                        store.assess_localisation(
                            assigned.event_id,
                            settings,
                            project_mode_name(
                                snapshot.project.mode)))
                        .dump(),
                    event_id ? 200 : 201,
                    snapshot.etag);
            }
            catch (const std::out_of_range& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "tracked_click_or_event_not_found"},
                    },
                    404);
            }
            catch (const json::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_tracked_event_request"},
                    },
                    400);
            }
            catch (const std::invalid_argument& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_tracked_event_request"},
                    },
                    400);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "tracked_event_state_error"},
                    },
                    500);
            }
        });

    server.Delete(
        R"(/v1/projects/active/click-detectors/([^/]+)/tracked-clicks/([^/]+))",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                std::size_t parsed = 0;
                const auto raw_uid = req.matches[2].str();
                const auto uid =
                    std::stoull(raw_uid, &parsed);
                if (parsed != raw_uid.size() || uid == 0) {
                    throw std::invalid_argument(
                        "Tracked click UID must be positive");
                }
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                const auto unit_id = req.matches[1].str();
                if (!find_active_click_detector(
                        snapshot,
                        unit_id)) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Click Detector controlled unit "
                                "does not exist",
                            },
                            {"code", "click_detector_not_found"},
                        },
                        404);
                    return;
                }
                auto& store =
                    tracked_click_store(snapshot, unit_id);
                if (!store.remove_click(uid)) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Click is not assigned to a tracked "
                                "event",
                            },
                            {"code", "tracked_click_not_assigned"},
                        },
                        404);
                    return;
                }
                encoded_json_response(
                    res,
                    json({
                        {"removed", true},
                        {"clickUid", uid},
                    }).dump(),
                    200,
                    snapshot.etag);
            }
            catch (const std::invalid_argument& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_tracked_event_request"},
                    },
                    400);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "tracked_event_state_error"},
                    },
                    500);
            }
        });

    server.Post(
        R"(/v1/projects/active/click-detectors/([^/]+)/tracked-events/([^/]+):reassign)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                std::size_t parsed = 0;
                const auto raw_source = req.matches[2].str();
                const auto source_id =
                    std::stoull(raw_source, &parsed);
                if (parsed != raw_source.size() ||
                    source_id == 0) {
                    throw std::invalid_argument(
                        "Source tracked event ID must be positive");
                }
                const auto body = json::parse(req.body);
                if (!body.is_object() ||
                    body.size() != 1 ||
                    !body.contains("targetEventId")) {
                    throw std::invalid_argument(
                        "Reassignment body must contain only "
                        "targetEventId");
                }
                const auto target_id =
                    body.at("targetEventId")
                        .get<std::uint64_t>();
                if (target_id == 0) {
                    throw std::invalid_argument(
                        "targetEventId must be positive");
                }
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                const auto unit_id = req.matches[1].str();
                const auto* unit =
                    find_active_click_detector(
                        snapshot,
                        unit_id);
                if (!unit) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Click Detector controlled unit "
                                "does not exist",
                            },
                            {"code", "click_detector_not_found"},
                        },
                        404);
                    return;
                }
                auto& store =
                    tracked_click_store(snapshot, unit_id);
                const auto assigned =
                    store.reassign_event(
                        source_id,
                        target_id);
                const auto settings =
                    tracked_click_localiser_settings(*unit);
                encoded_json_response(
                    res,
                    tracked_click_event_to_json(
                        assigned,
                        store.assess_localisation(
                            assigned.event_id,
                            settings,
                            project_mode_name(
                                snapshot.project.mode)))
                        .dump(),
                    200,
                    snapshot.etag);
            }
            catch (const std::out_of_range& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "tracked_event_not_found"},
                    },
                    404);
            }
            catch (const json::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_tracked_event_request"},
                    },
                    400);
            }
            catch (const std::invalid_argument& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_tracked_event_request"},
                    },
                    400);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "tracked_event_state_error"},
                    },
                    500);
            }
        });

    server.Delete(
        R"(/v1/projects/active/click-detectors/([^/]+)/tracked-events/([^/]+))",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                std::size_t parsed = 0;
                const auto raw_event = req.matches[2].str();
                const auto event_id =
                    std::stoull(raw_event, &parsed);
                if (parsed != raw_event.size() ||
                    event_id == 0) {
                    throw std::invalid_argument(
                        "Tracked event ID must be positive");
                }
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                const auto unit_id = req.matches[1].str();
                if (!find_active_click_detector(
                        snapshot,
                        unit_id)) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Click Detector controlled unit "
                                "does not exist",
                            },
                            {"code", "click_detector_not_found"},
                        },
                        404);
                    return;
                }
                auto& store =
                    tracked_click_store(snapshot, unit_id);
                if (!store.delete_event(event_id)) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Tracked event does not exist",
                            },
                            {"code", "tracked_event_not_found"},
                        },
                        404);
                    return;
                }
                encoded_json_response(
                    res,
                    json({
                        {"deleted", true},
                        {"eventId", event_id},
                    }).dump(),
                    200,
                    snapshot.etag);
            }
            catch (const std::invalid_argument& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_tracked_event_request"},
                    },
                    400);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "tracked_event_state_error"},
                    },
                    500);
            }
        });

    server.Post(
        R"(/v1/projects/active/click-detectors/([^/]+)/tracked-events/([^/]+):localise)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                if (!req.body.empty()) {
                    const auto body = json::parse(req.body);
                    if (!body.is_object() || !body.empty()) {
                        throw std::invalid_argument(
                            "Localise body must be an empty object");
                    }
                }
                std::size_t parsed = 0;
                const auto raw_event = req.matches[2].str();
                const auto event_id =
                    std::stoull(raw_event, &parsed);
                if (parsed != raw_event.size() ||
                    event_id == 0) {
                    throw std::invalid_argument(
                        "Tracked event ID must be positive");
                }
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                const auto unit_id = req.matches[1].str();
                const auto* unit =
                    find_active_click_detector(
                        snapshot,
                        unit_id);
                if (!unit) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Click Detector controlled unit "
                                "does not exist",
                            },
                            {"code", "click_detector_not_found"},
                        },
                        404);
                    return;
                }
                auto& store =
                    tracked_click_store(snapshot, unit_id);
                const auto settings =
                    tracked_click_localiser_settings(*unit);
                const auto events = store.events();
                const auto event = std::find_if(
                    events.begin(),
                    events.end(),
                    [&](const auto& candidate) {
                        return candidate.event_id == event_id;
                    });
                if (event == events.end()) {
                    throw std::out_of_range(
                        "Tracked event does not exist");
                }
                std::vector<
                    pamguard::service::
                        TrackedClickNavigationSample>
                    navigation_track;
                if (!event->clicks.empty()) {
                    const auto& navigation_reference =
                        event->clicks.front().
                            navigation_reference_id;
                    const auto found_track =
                        project_navigation_tracks.find(
                            navigation_reference);
                    if (!navigation_reference.empty() &&
                        found_track !=
                            project_navigation_tracks.end() &&
                        found_track->second.project_id ==
                            snapshot.project.project_id &&
                        found_track->second.
                                working_revision ==
                            snapshot.working_revision) {
                        const auto first_time =
                            event->clicks.front().time_ms;
                        const auto last_time =
                            event->clicks.back().time_ms;
                        const long double margin =
                            static_cast<long double>(
                                settings.
                                    max_time_milliseconds);
                        const long double window_start =
                            static_cast<long double>(
                                first_time) -
                            margin;
                        const long double window_end =
                            static_cast<long double>(
                                last_time) +
                            margin;
                        for (const auto& sample :
                             found_track->second.samples) {
                            const long double sample_time =
                                static_cast<long double>(
                                    sample.time_ms);
                            if (sample_time >= window_start &&
                                sample_time <= window_end) {
                                navigation_track.push_back(
                                    sample);
                            }
                        }
                    }
                }
                const auto run =
                    store.run_localisation(
                        event_id,
                        settings,
                        project_mode_name(
                            snapshot.project.mode),
                        navigation_track);
                encoded_json_response(
                    res,
                    tracked_click_localisation_run_to_json(
                        run).dump(),
                    run.executed() ? 200 : 409,
                    snapshot.etag);
            }
            catch (const std::out_of_range& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "tracked_event_not_found"},
                    },
                    404);
            }
            catch (const json::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_tracked_event_request"},
                    },
                    400);
            }
            catch (const std::invalid_argument& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_tracked_event_request"},
                    },
                    400);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "tracked_event_state_error"},
                    },
                    500);
            }
        });

    server.Get(
        "/v1/projects/active/acquisitions",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                (void)reconcile_acquisition_host_state(
                    snapshot);
                auto acquisitions = json::array();
                for (const auto& unit :
                     snapshot.project.controlled_units) {
                    if (unit.type_id !=
                        "pamguard.acquisition") {
                        continue;
                    }
                    const auto target =
                        acquisition_capture_target(
                            snapshot,
                            unit.id);
                    const auto binding =
                        acquisition_host_bindings.find(
                            target);
                    const auto* output =
                        find_active_acquisition_audio_output(
                            snapshot,
                            unit.id);
                    const auto block = output
                        ? module_runtime.find_block(
                              output->block_id)
                        : nullptr;
                    bool running = false;
                    {
                        const auto key =
                            pamguard::service::
                                acquisition_capture_target_key(
                                    target);
                        std::lock_guard capture_lock(
                            capture_state.mutex);
                        running =
                            capture_state.running.contains(key);
                    }
                    acquisitions.push_back({
                        {"unitId", unit.id},
                        {"name", unit.name},
                        {"typeId", unit.type_id},
                        {
                            "sampleRateHz",
                            block
                                ? json(block->descriptor().
                                      sample_rate_hz)
                                : json(nullptr),
                        },
                        {
                            "channelCount",
                            block
                                ? json(channel_count_from_bitmap(
                                      block->descriptor().
                                          channel_bitmap))
                                : json(nullptr),
                        },
                        {
                            "hostBindingRevision",
                            binding
                                ? json(binding->
                                      binding_revision)
                                : json(nullptr),
                        },
                        {
                            "configurationStatus",
                            binding
                                ? "configured"
                                : "needsConfiguration",
                        },
                        {"captureRunning", running},
                    });
                }
                encoded_json_response(
                    res,
                    json({
                        {"schemaVersion", 1},
                        {
                            "projectId",
                            snapshot.project.project_id,
                        },
                        {
                            "workingRevision",
                            snapshot.working_revision,
                        },
                        {
                            "runtimeRunning",
                            module_runtime.running(),
                        },
                        {
                            "captureEnabled",
                            capture_enabled,
                        },
                        {
                            "urlCaptureCapability",
#ifdef _WIN32
                            capture_enabled
                                ? "available"
                                : "disabled",
#else
                            "unavailable-current-ingest-bridge",
#endif
                        },
#ifdef _WIN32
                        {
                            "audioDeviceCaptureCapability",
                            capture_enabled
                                ? "windows-directshow"
                                : "disabled",
                        },
#else
                        {
                            "audioDeviceCaptureCapability",
                            "unavailable-on-this-platform",
                        },
#endif
                        {
                            "acquisitions",
                            std::move(acquisitions),
                        },
                    }).dump(),
                    200,
                    snapshot.etag);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "acquisition_state_error"},
                    },
                    500);
            }
        });

    server.Get(
        R"(/v1/projects/active/acquisitions/([^/]+)/host-binding)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                (void)reconcile_acquisition_host_state(
                    snapshot);
                const auto unit_id = req.matches[1].str();
                if (!find_active_acquisition(
                        snapshot,
                        unit_id)) {
                    acquisition_not_found_response(res);
                    return;
                }
                const auto binding =
                    acquisition_host_bindings.find(
                        acquisition_capture_target(
                            snapshot,
                            unit_id));
                if (!binding) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "No host binding exists for the "
                                "Acquisition instance",
                            },
                            {"code", "host_binding_not_found"},
                        },
                        404);
                    return;
                }
                encoded_json_response(
                    res,
                    acquisition_host_binding_to_json(
                        *binding).dump(),
                    200,
                    snapshot.etag);
            }
            catch (
                const pamguard::service::
                    StaleAcquisitionCaptureTarget& error) {
                stale_acquisition_target_response(
                    res,
                    error);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "acquisition_state_error"},
                    },
                    500);
            }
        });

    server.Put(
        R"(/v1/projects/active/acquisitions/([^/]+)/host-binding)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                const auto body = json::parse(req.body);
                if (!body.is_object() ||
                    body.size() != 3 ||
                    !body.contains("source") ||
                    !body.at("source").is_object()) {
                    throw std::invalid_argument(
                        "Host binding body must contain only "
                        "expectedWorkingRevision, "
                        "expectedBindingRevision, and source");
                }
                const auto expected_working_revision =
                    required_json_uint64(
                        body,
                        "expectedWorkingRevision");
                const auto expected_binding_revision =
                    required_json_uint64(
                        body,
                        "expectedBindingRevision");
                const auto& source_body =
                    body.at("source");
                if (!source_body.contains("kind") ||
                    !source_body.at("kind").is_string()) {
                    throw std::invalid_argument(
                        "source.kind is required");
                }
                const auto kind =
                    source_body.at("kind").
                        get<std::string>();
                pamguard::service::
                    AcquisitionHostBindingSource source =
                        pamguard::service::
                            NonSecretHttpUrlHostBinding{};
                std::vector<std::pair<std::string, std::string>>
                    devices;
                if (kind == "url") {
                    if (source_body.size() != 2 ||
                        !source_body.contains("url") ||
                        !source_body.at("url").is_string()) {
                        throw std::invalid_argument(
                            "A URL source must contain only kind "
                            "and url");
                    }
                    source = pamguard::service::
                        NonSecretHttpUrlHostBinding{
                            source_body.at("url").
                                get<std::string>(),
                        };
                }
                else if (kind == "device") {
                    if (source_body.size() != 2 ||
                        !source_body.contains("deviceName") ||
                        !source_body.at("deviceName").
                            is_string()) {
                        throw std::invalid_argument(
                            "A device source must contain only "
                            "kind and deviceName");
                    }
                    if (!capture_enabled) {
                        json_response(
                            res,
                            {
                                {
                                    "error",
                                    "Capture is disabled; set "
                                    "PAMGUARD_CAPTURE_ENABLED=1 "
                                    "before binding a host device",
                                },
                                {"code", "capture_disabled"},
                            },
                            503);
                        return;
                    }
#ifndef _WIN32
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Exact host audio-device binding "
                                "is only available through "
                                "Windows DirectShow; URL capture "
                                "is available on this platform",
                            },
                            {
                                "code",
                                "audio_device_capture_unavailable",
                            },
                        },
                        501);
                    return;
#else
                    std::string device_error;
                    devices =
                        list_capture_devices(device_error);
                    if (!device_error.empty()) {
                        json_response(
                            res,
                            {
                                {"error", device_error},
                                {
                                    "code",
                                    "device_enumeration_failed",
                                },
                            },
                            502);
                        return;
                    }
#endif
                    source = pamguard::service::
                        ExactAudioDeviceHostBinding{
                            source_body.at("deviceName").
                                get<std::string>(),
                        };
                }
                else {
                    throw std::invalid_argument(
                        "source.kind must be url or device");
                }

                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                require_working_revision(
                    expected_working_revision,
                    snapshot);
                (void)reconcile_acquisition_host_state(
                    snapshot);
                const auto unit_id = req.matches[1].str();
                if (!find_active_acquisition(
                        snapshot,
                        unit_id)) {
                    acquisition_not_found_response(res);
                    return;
                }
                const auto target =
                    acquisition_capture_target(
                        snapshot,
                        unit_id);
                const auto binding =
                    acquisition_host_bindings.put(
                        target,
                        expected_binding_revision,
                        std::move(source),
                        devices);
                const bool capture_stopped =
                    stop_acquisition_capture(target);
                append_audit_event(
                    audit_log_file,
                    audit_mutex,
                    {
                        {
                            "event",
                            "acquisition_host_binding_put",
                        },
                        {
                            "projectId",
                            snapshot.project.project_id,
                        },
                        {"unitId", unit_id},
                        {
                            "workingRevision",
                            snapshot.working_revision,
                        },
                        {
                            "bindingRevision",
                            binding.binding_revision,
                        },
                        {"captureStopped", capture_stopped},
                    });
                encoded_json_response(
                    res,
                    json({
                        {
                            "hostBinding",
                            acquisition_host_binding_to_json(
                                binding),
                        },
                        {
                            "captureStopped",
                            capture_stopped,
                        },
                    }).dump(),
                    expected_binding_revision == 0
                        ? 201
                        : 200,
                    snapshot.etag);
            }
            catch (
                const pamguard::service::
                    AcquisitionHostBindingConflict& error) {
                acquisition_binding_conflict_response(
                    res,
                    error);
            }
            catch (
                const pamguard::service::
                    StaleAcquisitionCaptureTarget& error) {
                stale_acquisition_target_response(
                    res,
                    error);
            }
            catch (
                const pamguard::service::
                    InactiveAcquisitionCaptureTarget&) {
                acquisition_not_found_response(res);
            }
            catch (
                const json::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_host_binding"},
                    },
                    400);
            }
            catch (
                const std::invalid_argument& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_host_binding"},
                    },
                    400);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "host_binding_error"},
                    },
                    500);
            }
        });

    server.Delete(
        R"(/v1/projects/active/acquisitions/([^/]+)/host-binding)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                const auto body = json::parse(req.body);
                if (!body.is_object() ||
                    body.size() != 2) {
                    throw std::invalid_argument(
                        "Host binding delete body must contain "
                        "only expectedWorkingRevision and "
                        "expectedBindingRevision");
                }
                const auto expected_working_revision =
                    required_json_uint64(
                        body,
                        "expectedWorkingRevision");
                const auto expected_binding_revision =
                    required_json_uint64(
                        body,
                        "expectedBindingRevision");
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                require_working_revision(
                    expected_working_revision,
                    snapshot);
                (void)reconcile_acquisition_host_state(
                    snapshot);
                const auto unit_id = req.matches[1].str();
                if (!find_active_acquisition(
                        snapshot,
                        unit_id)) {
                    acquisition_not_found_response(res);
                    return;
                }
                const auto target =
                    acquisition_capture_target(
                        snapshot,
                        unit_id);
                const bool deleted =
                    acquisition_host_bindings.erase(
                        target,
                        expected_binding_revision);
                const bool capture_stopped =
                    stop_acquisition_capture(target);
                append_audit_event(
                    audit_log_file,
                    audit_mutex,
                    {
                        {
                            "event",
                            "acquisition_host_binding_delete",
                        },
                        {
                            "projectId",
                            snapshot.project.project_id,
                        },
                        {"unitId", unit_id},
                        {
                            "workingRevision",
                            snapshot.working_revision,
                        },
                        {"deleted", deleted},
                        {"captureStopped", capture_stopped},
                    });
                encoded_json_response(
                    res,
                    json({
                        {"deleted", deleted},
                        {
                            "projectId",
                            snapshot.project.project_id,
                        },
                        {"acquisitionUnitId", unit_id},
                        {
                            "workingRevision",
                            snapshot.working_revision,
                        },
                        {
                            "captureStopped",
                            capture_stopped,
                        },
                    }).dump(),
                    200,
                    snapshot.etag);
            }
            catch (
                const pamguard::service::
                    AcquisitionHostBindingConflict& error) {
                acquisition_binding_conflict_response(
                    res,
                    error);
            }
            catch (
                const pamguard::service::
                    StaleAcquisitionCaptureTarget& error) {
                stale_acquisition_target_response(
                    res,
                    error);
            }
            catch (
                const pamguard::service::
                    InactiveAcquisitionCaptureTarget&) {
                acquisition_not_found_response(res);
            }
            catch (
                const std::invalid_argument& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_host_binding_delete"},
                    },
                    400);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "host_binding_error"},
                    },
                    500);
            }
        });

    server.Get(
        R"(/v1/projects/active/acquisitions/([^/]+)/capture-status)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                (void)reconcile_acquisition_host_state(
                    snapshot);
                const auto unit_id = req.matches[1].str();
                if (!find_active_acquisition(
                        snapshot,
                        unit_id)) {
                    acquisition_not_found_response(res);
                    return;
                }
                const auto target =
                    acquisition_capture_target(
                        snapshot,
                        unit_id);
                const auto binding =
                    acquisition_host_bindings.find(target);
                json process_id = nullptr;
                bool running = false;
                {
                    const auto key =
                        pamguard::service::
                            acquisition_capture_target_key(
                                target);
                    std::lock_guard capture_lock(
                        capture_state.mutex);
                    const auto capture =
                        capture_state.running.find(key);
                    if (capture !=
                        capture_state.running.end()) {
                        running = true;
                        process_id = capture->second.pid;
                    }
                }
                encoded_json_response(
                    res,
                    json({
                        {"schemaVersion", 1},
                        {
                            "projectId",
                            snapshot.project.project_id,
                        },
                        {"acquisitionUnitId", unit_id},
                        {
                            "workingRevision",
                            snapshot.working_revision,
                        },
                        {"captureEnabled", capture_enabled},
                        {
                            "hostBindingRevision",
                            binding
                                ? json(binding->
                                      binding_revision)
                                : json(nullptr),
                        },
                        {"running", running},
                        {"processId", process_id},
                    }).dump(),
                    200,
                    snapshot.etag);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "acquisition_state_error"},
                    },
                    500);
            }
        });

    server.Post(
        R"(/v1/projects/active/acquisitions/([^/]+)/capture:start)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            if (!capture_enabled) {
                json_response(
                    res,
                    {
                        {
                            "error",
                            "Capture is disabled; set "
                            "PAMGUARD_CAPTURE_ENABLED=1",
                        },
                        {"code", "capture_disabled"},
                    },
                    503);
                return;
            }
            try {
                const auto body = json::parse(req.body);
                if (!body.is_object() ||
                    body.size() != 1) {
                    throw std::invalid_argument(
                        "Capture start body must contain only "
                        "expectedWorkingRevision");
                }
                const auto expected_working_revision =
                    required_json_uint64(
                        body,
                        "expectedWorkingRevision");
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                require_working_revision(
                    expected_working_revision,
                    snapshot);
                (void)reconcile_acquisition_host_state(
                    snapshot);
                const auto unit_id = req.matches[1].str();
                if (!find_active_acquisition(
                        snapshot,
                        unit_id)) {
                    acquisition_not_found_response(res);
                    return;
                }
                const auto* output =
                    find_active_acquisition_audio_output(
                        snapshot,
                        unit_id);
                const auto source_block = output
                    ? module_runtime.find_block(
                          output->block_id)
                    : nullptr;
                if (!module_runtime.running() ||
                    module_runtime.revision() !=
                        snapshot.working_revision ||
                    !output ||
                    !source_block ||
                    source_block->descriptor().data_type !=
                        pamguard::core::kRawAudioDataType) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "The active Acquisition runtime "
                                "is not running at the expected "
                                "working revision",
                            },
                            {
                                "code",
                                "acquisition_runtime_unavailable",
                            },
                        },
                        409);
                    return;
                }
                const auto target =
                    acquisition_capture_target(
                        snapshot,
                        unit_id);
                const auto binding =
                    acquisition_host_bindings.find(target);
                if (!binding) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Create a host binding before "
                                "starting capture",
                            },
                            {"code", "host_binding_not_found"},
                        },
                        404);
                    return;
                }
                std::string source;
                auto source_kind =
                    pamguard::service::
                        CaptureSourceKind::HttpUrl;
                std::string public_kind = "url";
                if (const auto* device = std::get_if<
                        pamguard::service::
                            ExactAudioDeviceHostBinding>(
                        &binding->source)) {
#ifndef _WIN32
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "Host audio-device capture is "
                                "only available through Windows "
                                "DirectShow; URL capture is "
                                "available on this platform",
                            },
                            {
                                "code",
                                "audio_device_capture_unavailable",
                            },
                        },
                        501);
                    return;
#else
                    std::string device_error;
                    const auto devices =
                        list_capture_devices(device_error);
                    if (!device_error.empty()) {
                        json_response(
                            res,
                            {
                                {"error", device_error},
                                {
                                    "code",
                                    "device_enumeration_failed",
                                },
                            },
                            502);
                        return;
                    }
                    if (!pamguard::service::
                            is_exact_enumerated_audio_device(
                                devices,
                                device->device_name)) {
                        json_response(
                            res,
                            {
                                {
                                    "error",
                                    "The bound audio device is "
                                    "not currently enumerated",
                                },
                                {
                                    "code",
                                    "bound_device_unavailable",
                                },
                            },
                            409);
                        return;
                    }
#endif
                    source = device->device_name;
                    source_kind =
                        pamguard::service::
                            CaptureSourceKind::
                                DirectShowDevice;
                    public_kind = "device";
                }
                else {
                    source = std::get<
                        pamguard::service::
                            NonSecretHttpUrlHostBinding>(
                        binding->source).url;
#ifndef _WIN32
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "URL capture cannot start on this "
                                "platform because the current "
                                "ffmpeg_stream_ingest bridge uses "
                                "a shell-backed, Windows-binary "
                                "FFmpeg pipe. Use supervised PCM "
                                "ingest until that bridge executes "
                                "FFmpeg directly",
                            },
                            {
                                "code",
                                "url_capture_bridge_unavailable",
                            },
                        },
                        501);
                    return;
#endif
                }
                if (!capture_bridge_source_is_safe(source)) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "The bound source contains shell "
                                "expansion characters unsupported "
                                "by the current FFmpeg ingest "
                                "bridge",
                            },
                            {
                                "code",
                                "capture_bridge_source_unsupported",
                            },
                        },
                        422);
                    return;
                }

                const auto target_key =
                    pamguard::service::
                        acquisition_capture_target_key(target);
                std::lock_guard capture_lock(
                    capture_state.mutex);
                const auto existing =
                    capture_state.running.find(target_key);
                if (existing !=
                    capture_state.running.end()) {
                    if (capture_process_running(
                            existing->second)) {
                        json_response(
                            res,
                            {
                                {
                                    "error",
                                    "A capture is already running "
                                    "for this Acquisition instance",
                                },
                                {
                                    "code",
                                    "capture_already_running",
                                },
                            },
                            409);
                        return;
                    }
                    const auto requirement =
                        capture_state.required_project_captures.find(
                            target_key);
                    if (requirement !=
                        capture_state.required_project_captures.end()) {
                        requirement->second.child_failed = true;
                    }
                    close_capture_process(
                        existing->second);
                    capture_state.running.erase(existing);
                }
                const auto sample_rate =
                    static_cast<std::size_t>(std::llround(
                        source_block->descriptor().
                            sample_rate_hz));
                const auto channel_count =
                    channel_count_from_bitmap(
                        source_block->descriptor().
                            channel_bitmap);
                CaptureProcess capture;
                capture.module_id =
                    output->runtime_node_id;
                capture.project_id =
                    snapshot.project.project_id;
                capture.acquisition_unit_id = unit_id;
                capture.device = source;
                capture.source_kind = source_kind;
                capture.sample_rate_hz = sample_rate;
                capture.channel_count = channel_count;
                capture.graph_revision =
                    snapshot.working_revision;
                capture.working_revision =
                    snapshot.working_revision;
                capture.binding_revision =
                    binding->binding_revision;
                pamguard::service::
                    CaptureIngestCommandOptions
                        command_options;
                command_options.ingest_executable =
                    ingest_exe;
                command_options.ffmpeg_executable =
                    ffmpeg_path;
                command_options.engine_url =
                    "http://127.0.0.1:" +
                    std::to_string(port);
                command_options.project_id =
                    snapshot.project.project_id;
                command_options.acquisition_unit_id =
                    unit_id;
                command_options.working_revision =
                    snapshot.working_revision;
                command_options.source = source;
                command_options.source_kind =
                    source_kind;
                command_options.sample_rate_hz =
                    sample_rate;
                command_options.channel_count =
                    channel_count;
                command_options.pass_api_key_environment =
                    !api_key.empty();
                const auto args =
                    pamguard::service::
                        build_capture_ingest_command(
                            command_options);
                std::string start_error;
                if (!start_capture_process(
                        capture,
                        args,
                        start_error)) {
                    json_response(
                        res,
                        {
                            {"error", start_error},
                            {
                                "code",
                                "capture_process_start_failed",
                            },
                        },
                        502);
                    return;
                }
                const auto process_id = capture.pid;
                capture_state.running.emplace(
                    target_key,
                    std::move(capture));
                capture_state.required_project_captures.insert_or_assign(
                    target_key,
                    RequiredProjectCapture{
                        target,
                        false,
                    });
                append_audit_event(
                    audit_log_file,
                    audit_mutex,
                    {
                        {
                            "event",
                            "acquisition_capture_start",
                        },
                        {
                            "projectId",
                            snapshot.project.project_id,
                        },
                        {"unitId", unit_id},
                        {
                            "workingRevision",
                            snapshot.working_revision,
                        },
                        {
                            "bindingRevision",
                            binding->binding_revision,
                        },
                        {"kind", public_kind},
                    });
                encoded_json_response(
                    res,
                    json({
                        {"started", true},
                        {
                            "projectId",
                            snapshot.project.project_id,
                        },
                        {"acquisitionUnitId", unit_id},
                        {
                            "workingRevision",
                            snapshot.working_revision,
                        },
                        {
                            "bindingRevision",
                            binding->binding_revision,
                        },
                        {"kind", public_kind},
                        {"sampleRateHz", sample_rate},
                        {"channelCount", channel_count},
                        {"processId", process_id},
                    }).dump(),
                    200,
                    snapshot.etag);
            }
            catch (
                const pamguard::service::
                    StaleAcquisitionCaptureTarget& error) {
                stale_acquisition_target_response(
                    res,
                    error);
            }
            catch (
                const json::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_capture_request"},
                    },
                    400);
            }
            catch (
                const std::invalid_argument& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_capture_request"},
                    },
                    400);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "capture_start_error"},
                    },
                    500);
            }
        });

    server.Post(
        R"(/v1/projects/active/acquisitions/([^/]+)/capture:stop)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                const auto body = json::parse(req.body);
                if (!body.is_object() ||
                    body.size() != 1) {
                    throw std::invalid_argument(
                        "Capture stop body must contain only "
                        "expectedWorkingRevision");
                }
                const auto expected_working_revision =
                    required_json_uint64(
                        body,
                        "expectedWorkingRevision");
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                require_working_revision(
                    expected_working_revision,
                    snapshot);
                (void)reconcile_acquisition_host_state(
                    snapshot);
                const auto unit_id = req.matches[1].str();
                if (!find_active_acquisition(
                        snapshot,
                        unit_id)) {
                    acquisition_not_found_response(res);
                    return;
                }
                const auto stopped =
                    stop_acquisition_capture(
                        acquisition_capture_target(
                            snapshot,
                            unit_id));
                if (!stopped) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "No capture is registered for "
                                "this Acquisition instance",
                            },
                            {"code", "capture_not_found"},
                        },
                        404);
                    return;
                }
                append_audit_event(
                    audit_log_file,
                    audit_mutex,
                    {
                        {
                            "event",
                            "acquisition_capture_stop",
                        },
                        {
                            "projectId",
                            snapshot.project.project_id,
                        },
                        {"unitId", unit_id},
                        {
                            "workingRevision",
                            snapshot.working_revision,
                        },
                    });
                encoded_json_response(
                    res,
                    json({
                        {"stopped", true},
                        {
                            "projectId",
                            snapshot.project.project_id,
                        },
                        {"acquisitionUnitId", unit_id},
                        {
                            "workingRevision",
                            snapshot.working_revision,
                        },
                    }).dump(),
                    200,
                    snapshot.etag);
            }
            catch (
                const pamguard::service::
                    StaleAcquisitionCaptureTarget& error) {
                stale_acquisition_target_response(
                    res,
                    error);
            }
            catch (
                const json::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_capture_request"},
                    },
                    400);
            }
            catch (
                const std::invalid_argument& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_capture_request"},
                    },
                    400);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "capture_stop_error"},
                    },
                    500);
            }
        });

    server.Post(
        R"(/v1/projects/active/acquisitions/([^/]+)/pcm-f32le)",
        [&](const httplib::Request& req,
            httplib::Response& res) {
            if (!require_authorized(req, res, api_key)) {
                return;
            }
            if (!require_project_authority_mode(res)) {
                return;
            }
            try {
                const auto expected_project_id =
                    required_query_string(
                        req,
                        "expectedProjectId");
                const auto expected_working_revision =
                    required_query_uint64(
                        req,
                        "expectedWorkingRevision");
                std::lock_guard lifecycle_lock(
                    module_graph_update_mutex);
                const auto snapshot =
                    project_authority.snapshot();
                if (expected_project_id !=
                    snapshot.project.project_id) {
                    active_project_mismatch_response(
                        res,
                        expected_project_id,
                        snapshot.project.project_id);
                    return;
                }
                require_working_revision(
                    expected_working_revision,
                    snapshot);
                (void)reconcile_acquisition_host_state(
                    snapshot);
                const auto unit_id = req.matches[1].str();
                if (!find_active_acquisition(
                        snapshot,
                        unit_id)) {
                    acquisition_not_found_response(res);
                    return;
                }
                const auto* output =
                    find_active_acquisition_audio_output(
                        snapshot,
                        unit_id);
                const auto source_block = output
                    ? module_runtime.find_block(
                          output->block_id)
                    : nullptr;
                if (!module_runtime.running() ||
                    module_runtime.revision() !=
                        snapshot.working_revision ||
                    !output ||
                    !source_block ||
                    source_block->descriptor().data_type !=
                        pamguard::core::kRawAudioDataType) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "The active Acquisition runtime "
                                "is not running at the expected "
                                "working revision",
                            },
                            {
                                "code",
                                "acquisition_runtime_unavailable",
                            },
                        },
                        409);
                    return;
                }
                if (max_pcm_body_bytes > 0 &&
                    req.body.size() >
                        max_pcm_body_bytes) {
                    json_response(
                        res,
                        {
                            {
                                "error",
                                "PCM body exceeds maximum size",
                            },
                            {
                                "code",
                                "pcm_body_too_large",
                            },
                            {
                                "maxPcmBodyBytes",
                                max_pcm_body_bytes,
                            },
                        },
                        413);
                    return;
                }
                const auto channel_count =
                    channel_count_from_bitmap(
                        source_block->descriptor().
                            channel_bitmap);
                const auto sample_rate =
                    static_cast<std::uint32_t>(
                        std::llround(
                            source_block->descriptor().
                                sample_rate_hz));
                const auto bytes_per_frame =
                    channel_count * sizeof(float);
                if (req.body.empty() ||
                    bytes_per_frame == 0 ||
                    req.body.size() %
                        bytes_per_frame != 0) {
                    throw std::invalid_argument(
                        "PCM body must contain whole "
                        "interleaved f32le frames");
                }
                const auto frame_count =
                    req.body.size() / bytes_per_frame;
                const auto start_sample =
                    parse_uint64_param(
                        req,
                        "startSample",
                        0);
                const auto time_ms =
                    req.has_param("timeMs")
                    ? static_cast<std::int64_t>(
                          std::stoll(
                              req.get_param_value(
                                  "timeMs")))
                    : static_cast<std::int64_t>(
                          static_cast<double>(
                              start_sample) *
                          1000.0 / sample_rate);
                pamguard::core::AudioChunk chunk;
                chunk.start_sample = start_sample;
                chunk.time_unix_ms = time_ms;
                chunk.sample_rate_hz = sample_rate;
                chunk.channel_count = channel_count;
                const int orientation_field_count =
                    static_cast<int>(
                        req.has_param("headingDegrees")) +
                    static_cast<int>(
                        req.has_param("pitchDegrees")) +
                    static_cast<int>(
                        req.has_param("rollDegrees"));
                if (orientation_field_count != 0 &&
                    orientation_field_count != 3) {
                    throw std::invalid_argument(
                        "headingDegrees, pitchDegrees, and "
                        "rollDegrees must be supplied together");
                }
                if (orientation_field_count == 3) {
                    chunk.orientation_declared = true;
                    chunk.orientation_heading_degrees =
                        required_finite_query_double(
                            req,
                            "headingDegrees");
                    chunk.orientation_pitch_degrees =
                        required_finite_query_double(
                            req,
                            "pitchDegrees");
                    chunk.orientation_roll_degrees =
                        required_finite_query_double(
                            req,
                            "rollDegrees");
                }
                const int origin_field_count =
                    static_cast<int>(
                        req.has_param("originEastMetres")) +
                    static_cast<int>(
                        req.has_param("originNorthMetres")) +
                    static_cast<int>(
                        req.has_param("originHeightMetres"));
                if (origin_field_count != 0 &&
                    origin_field_count != 3) {
                    throw std::invalid_argument(
                        "originEastMetres, originNorthMetres, "
                        "and originHeightMetres must be supplied "
                        "together");
                }
                if (origin_field_count == 3) {
                    chunk.navigation_origin_declared = true;
                    chunk.navigation_origin_east_metres =
                        required_finite_query_double(
                            req,
                            "originEastMetres");
                    chunk.navigation_origin_north_metres =
                        required_finite_query_double(
                            req,
                            "originNorthMetres");
                    chunk.navigation_origin_height_metres =
                        required_finite_query_double(
                            req,
                            "originHeightMetres");
                    chunk.navigation_reference_id = unit_id;
                }
                chunk.interleaved_pcm.resize(
                    frame_count * channel_count);
                const auto* bytes =
                    reinterpret_cast<
                        const unsigned char*>(
                            req.body.data());
                for (std::size_t frame = 0;
                     frame < frame_count;
                     ++frame) {
                    for (std::size_t channel = 0;
                         channel < channel_count;
                         ++channel) {
                        const auto offset =
                            (frame * channel_count +
                             channel) *
                            sizeof(float);
                        chunk.interleaved_pcm[
                            frame * channel_count +
                            channel] =
                            read_float_le(
                                bytes + offset);
                    }
                }
                std::optional<
                    pamguard::service::
                        TrackedClickNavigationSample>
                    navigation_sample;
                if (chunk.navigation_origin_declared) {
                    navigation_sample =
                        pamguard::service::
                            TrackedClickNavigationSample{
                                time_ms,
                                {
                                    chunk.
                                        navigation_origin_east_metres,
                                    chunk.
                                        navigation_origin_north_metres,
                                    chunk.
                                        navigation_origin_height_metres,
                                },
                            };
                }
                module_runtime.ingest(
                    output->runtime_node_id,
                    std::move(chunk));
                if (navigation_sample) {
                    auto& track =
                        project_navigation_tracks[unit_id];
                    if (track.project_id !=
                            snapshot.project.project_id ||
                        track.working_revision !=
                            snapshot.working_revision) {
                        track = {
                            snapshot.project.project_id,
                            snapshot.working_revision,
                            {},
                        };
                    }
                    track.samples.push_back(
                        *navigation_sample);
                    constexpr std::size_t
                        kMaximumNavigationSamples =
                            65'536;
                    while (track.samples.size() >
                           kMaximumNavigationSamples) {
                        track.samples.pop_front();
                    }
                }
                encoded_json_response(
                    res,
                    json({
                        {"accepted", true},
                        {
                            "projectId",
                            snapshot.project.project_id,
                        },
                        {"acquisitionUnitId", unit_id},
                        {
                            "workingRevision",
                            snapshot.working_revision,
                        },
                        {"inputFrames", frame_count},
                        {"startSample", start_sample},
                        {
                            "orientationAccepted",
                            orientation_field_count == 3,
                        },
                        {
                            "navigationSampleAccepted",
                            navigation_sample.has_value(),
                        },
                    }).dump(),
                    202,
                    snapshot.etag);
            }
            catch (
                const pamguard::service::
                    StaleAcquisitionCaptureTarget& error) {
                stale_acquisition_target_response(
                    res,
                    error);
            }
            catch (
                const std::invalid_argument& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "invalid_pcm_request"},
                    },
                    400);
            }
            catch (const std::exception& error) {
                json_response(
                    res,
                    {
                        {"error", error.what()},
                        {"code", "pcm_ingest_error"},
                    },
                    500);
            }
        });

    server.Get("/capture/devices", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!capture_enabled) {
            json_response(res, {{"error", "capture is disabled; set PAMGUARD_CAPTURE_ENABLED=1"}}, 503);
            return;
        }
        std::string error;
        const auto devices = list_capture_devices(error);
        if (!error.empty()) {
            json_response(res, {{"error", error}}, 502);
            return;
        }
        json list = json::array();
        for (const auto& [name, type] : devices) {
            list.push_back({{"name", name}, {"type", type}});
        }
        json_response(res, {{"devices", std::move(list)}});
    });

    server.Get("/capture/status", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        json list = json::array();
        std::lock_guard lifecycle_lock(
            module_graph_update_mutex);
        const auto current_graph_revision =
            module_runtime.revision();
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(capture_state.mutex);
        pamguard::service::reap_dead_capture_entries(
            capture_state.running,
            [](const CaptureProcess& capture) {
                return capture_process_running(capture);
            },
            [](CaptureProcess& capture) {
                close_capture_process(capture);
            });
        // A module capture is valid only for the exact prepared graph
        // revision and acquisition output against which it was started.
        // Lifecycle transitions normally quiesce these entries first; this
        // defensive pass prevents any stale child surviving a failed or
        // externally interrupted transition.
        for (auto capture = capture_state.running.begin();
             capture != capture_state.running.end();) {
            const auto& value = capture->second;
            const bool stale_module_capture =
                !value.module_id.empty() &&
                (!value.graph_revision ||
                 *value.graph_revision != current_graph_revision ||
                 !module_runtime.running() ||
                 !module_runtime.find_block(
                     pamguard::core::ModuleRuntime::block_id(
                         value.module_id,
                         "audio")));
            if (stale_module_capture) {
                close_capture_process(capture->second);
                capture = capture_state.running.erase(capture);
                continue;
            }
            ++capture;
        }
        for (auto& [target_key, capture] : capture_state.running) {
            (void)target_key;
            list.push_back({
                {"sessionId", capture.session_id.empty()
                    ? json(nullptr)
                    : json(capture.session_id)},
                {"moduleId", capture.module_id.empty()
                    ? json(nullptr)
                    : json(capture.module_id)},
                {"source", capture.device},
                {"kind", capture_source_kind_name(
                    capture.source_kind)},
                {"sampleRateHz", capture.sample_rate_hz},
                {"channels", capture.channel_count},
                {"pid", capture.pid},
                {"running", true},
                {"graphRevision", capture.graph_revision
                    ? json(*capture.graph_revision)
                    : json(nullptr)},
            });
        }
#endif
        json_response(res, {
            {"captureEnabled", capture_enabled},
            {"currentGraphRevision", current_graph_revision},
            {"captures", std::move(list)},
        });
    });

    server.Post("/capture/start", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        if (!capture_enabled) {
            json_response(res, {{"error", "capture is disabled; set PAMGUARD_CAPTURE_ENABLED=1"}}, 503);
            return;
        }
#ifndef _WIN32
        json_response(res, {{"error", "sound-card capture is only implemented for Windows/DirectShow"}}, 501);
#else
        try {
            const auto body = json::parse(req.body);
            const auto session_id = body.value("sessionId", std::string());
            const auto module_id = body.value("moduleId", std::string());
            const auto device = body.value("device", std::string());
            const auto url = body.value("url", std::string());
            if ((session_id.empty() == module_id.empty()) ||
                (device.empty() == url.empty())) {
                json_response(res, {{"error", "exactly one of sessionId or moduleId, and exactly one of device or url, are required"}}, 400);
                return;
            }
            if (!module_id.empty() &&
                (!body.contains("expectedGraphRevision") ||
                 !body.at("expectedGraphRevision").
                    is_number_unsigned())) {
                json_response(res, {
                    {"error",
                     "expectedGraphRevision is required for a module capture and must be an unsigned integer"},
                }, 400);
                return;
            }
            const auto target_id =
                pamguard::service::capture_target_key(
                    session_id,
                    module_id);
            if (!url.empty() &&
                !pamguard::service::is_http_capture_url(url)) {
                // Only plain http(s) stream URLs: no file paths, lavfi
                // graphs, or protocol tricks reach the child command line.
                json_response(res, {{"error", "url must start with http:// or https://"}}, 400);
                return;
            }
            // Start, stop, status, runtime stop/reset, and graph replacement
            // all pass through this outer lock. A capture cannot be validated
            // against one revision and registered after that revision stops.
            std::lock_guard lifecycle_lock(
                module_graph_update_mutex);
            std::size_t sample_rate = 0;
            std::size_t channel_count = 0;
            std::optional<std::uint64_t> graph_revision;
            if (!module_id.empty()) {
                const auto current_graph_revision =
                    module_runtime.revision();
                const auto expected_graph_revision =
                    body.at("expectedGraphRevision").
                        get<std::uint64_t>();
                if (expected_graph_revision !=
                    current_graph_revision) {
                    json_response(res, {
                        {"error",
                         "capture target graph revision is stale"},
                        {"code", "graph_revision_conflict"},
                        {"expectedGraphRevision",
                         expected_graph_revision},
                        {"currentGraphRevision",
                         current_graph_revision},
                    }, 409);
                    return;
                }
                const auto source_block = module_runtime.find_block(
                    pamguard::core::ModuleRuntime::block_id(
                        module_id,
                        "audio"));
                if (!module_runtime.running() ||
                    !source_block ||
                    source_block->descriptor().data_type !=
                        pamguard::core::kRawAudioDataType) {
                    json_response(
                        res,
                        {{"error", "moduleId is not a running acquisition module"}},
                        404);
                    return;
                }
                sample_rate = static_cast<std::size_t>(
                    std::llround(
                        source_block->descriptor().sample_rate_hz));
                channel_count = channel_count_from_bitmap(
                    source_block->descriptor().channel_bitmap);
                graph_revision = module_runtime.revision();
            }
            else {
                std::lock_guard<std::mutex> lock(configs_mutex);
                const auto found = configs.find(session_id);
                if (found == configs.end()) {
                    json_response(res, {{"error", "session does not exist; create it first"}}, 404);
                    return;
                }
                sample_rate = found->second.sample_rate_hz;
                channel_count = found->second.channel_count;
            }
            std::string error;
            if (!device.empty()) {
                // The device must exactly match an enumerated device: no
                // user-composed strings ever reach the child command line.
                const auto devices = list_capture_devices(error);
                if (!error.empty()) {
                    json_response(res, {{"error", error}}, 502);
                    return;
                }
                const bool known =
                    pamguard::service::
                        is_exact_enumerated_audio_device(
                            devices,
                            device);
                if (!known) {
                    json_response(res, {{"error", "device is not an enumerated audio capture device: " + device}}, 400);
                    return;
                }
            }

            std::lock_guard<std::mutex> lock(capture_state.mutex);
            auto existing = capture_state.running.find(target_id);
            if (existing != capture_state.running.end()) {
                if (capture_process_running(existing->second)) {
                    json_response(res, {{"error", "a capture is already running for this target"}}, 409);
                    return;
                }
                close_capture_process(existing->second);
                capture_state.running.erase(existing);
            }

            CaptureProcess capture;
            capture.session_id = session_id;
            capture.module_id = module_id;
            capture.device = device.empty() ? url : device;
            capture.source_kind = device.empty()
                ? pamguard::service::CaptureSourceKind::HttpUrl
                : pamguard::service::
                    CaptureSourceKind::DirectShowDevice;
            capture.sample_rate_hz = sample_rate;
            capture.channel_count = channel_count;
            capture.graph_revision = graph_revision;
            pamguard::service::CaptureIngestCommandOptions
                command_options;
            command_options.ingest_executable = ingest_exe;
            command_options.ffmpeg_executable = ffmpeg_path;
            command_options.engine_url =
                "http://127.0.0.1:" +
                std::to_string(port);
            command_options.session_id = session_id;
            command_options.module_id = module_id;
            command_options.source =
                device.empty() ? url : device;
            command_options.source_kind = capture.source_kind;
            command_options.sample_rate_hz = sample_rate;
            command_options.channel_count = channel_count;
            command_options.pass_api_key_environment =
                !api_key.empty();
            const auto args =
                pamguard::service::build_capture_ingest_command(
                    command_options);
            if (!start_capture_process(capture, args, error)) {
                json_response(res, {{"error", error}}, 502);
                return;
            }
            const auto pid = capture.pid;
            const auto source = capture.device;
            capture_state.running.emplace(target_id, std::move(capture));
            json_response(res, {
                {"started", true},
                {"sessionId", session_id.empty()
                    ? json(nullptr)
                    : json(session_id)},
                {"moduleId", module_id.empty()
                    ? json(nullptr)
                    : json(module_id)},
                {"source", source},
                {"kind", device.empty() ? "url" : "dshow"},
                {"sampleRateHz", sample_rate},
                {"channels", channel_count},
                {"pid", pid},
                {"graphRevision", graph_revision
                    ? json(*graph_revision)
                    : json(nullptr)},
            });
        }
        catch (const std::exception& error_ex) {
            json_response(res, {{"error", error_ex.what()}}, 400);
        }
#endif
    });

    server.Post("/capture/stop", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
#ifndef _WIN32
        json_response(res, {{"error", "sound-card capture is only implemented for Windows/DirectShow"}}, 501);
#else
        try {
            const auto body = json::parse(req.body);
            const auto session_id = body.value("sessionId", std::string());
            const auto module_id = body.value("moduleId", std::string());
            if (session_id.empty() == module_id.empty()) {
                throw std::invalid_argument(
                    "exactly one of sessionId or moduleId is required");
            }
            if (!module_id.empty() &&
                (!body.contains("expectedGraphRevision") ||
                 !body.at("expectedGraphRevision").
                    is_number_unsigned())) {
                json_response(res, {
                    {"error",
                     "expectedGraphRevision is required for a module capture and must be an unsigned integer"},
                }, 400);
                return;
            }
            const auto target_id =
                pamguard::service::capture_target_key(
                    session_id,
                    module_id);
            std::lock_guard lifecycle_lock(
                module_graph_update_mutex);
            if (!module_id.empty()) {
                const auto current_graph_revision =
                    module_runtime.revision();
                const auto expected_graph_revision =
                    body.at("expectedGraphRevision").
                        get<std::uint64_t>();
                if (expected_graph_revision !=
                    current_graph_revision) {
                    json_response(res, {
                        {"error",
                         "capture target graph revision is stale"},
                        {"code", "graph_revision_conflict"},
                        {"expectedGraphRevision",
                         expected_graph_revision},
                        {"currentGraphRevision",
                         current_graph_revision},
                    }, 409);
                    return;
                }
            }
            std::lock_guard<std::mutex> lock(capture_state.mutex);
            const auto found = capture_state.running.find(target_id);
            if (found == capture_state.running.end()) {
                json_response(res, {{"error", "no capture is registered for this target"}}, 404);
                return;
            }
            close_capture_process(found->second);
            capture_state.running.erase(found);
            json_response(res, {
                {"stopped", true},
                {"sessionId", session_id.empty()
                    ? json(nullptr)
                    : json(session_id)},
                {"moduleId", module_id.empty()
                    ? json(nullptr)
                    : json(module_id)},
            });
        }
        catch (const std::exception& error_ex) {
            json_response(res, {{"error", error_ex.what()}}, 400);
        }
#endif
    });

    server.Get("/module-types", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        try {
            auto types = json::array();
            const auto catalogue = module_registry.list();
            for (const auto& type : catalogue) {
                types.push_back(module_type_to_json(type));
            }
            json_response(res, {
                {"moduleTypes", std::move(types)},
                {"count", catalogue.size()},
            });
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 500);
        }
    });

    server.Get("/module-graph", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        std::lock_guard update_lock(module_graph_update_mutex);
        if (legacy_model_compat) {
            res.set_content(
                pamguard::core::module_graph_to_json(
                    module_graph.snapshot(),
                    true),
                "application/json; charset=utf-8");
            return;
        }
        const auto active = project_authority.snapshot();
        auto projection = active.projection.graph;
        projection.revision = active.working_revision;
        res.set_content(
            pamguard::core::module_graph_to_json(projection, true),
            "application/json; charset=utf-8");
    });

    server.Post("/module-graph/validate", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        try {
            const auto document = pamguard::core::module_graph_from_json(req.body);
            auto validation = module_graph.validate(document);
            if (validation.valid()) {
                try {
                    pamguard::core::ModuleRuntime candidate_runtime;
                    candidate_runtime.configure(document);
                    candidate_runtime.start();
                    candidate_runtime.stop();
                }
                catch (const std::exception& error) {
                    validation.issues.push_back({
                        "invalid_runtime_settings",
                        error.what(),
                        {},
                        {},
                    });
                }
            }
            json_response(res, {
                {"valid", validation.valid()},
                {"issues", graph_issues_to_json(validation.issues)},
            });
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    server.Get("/module-graph/compatible-sources", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!req.has_param("moduleId") || !req.has_param("portId")) {
            json_response(res, {{"error", "moduleId and portId query parameters are required"}}, 400);
            return;
        }
        std::lock_guard update_lock(module_graph_update_mutex);
        const auto document = module_graph.snapshot();
        const auto sources = module_graph.compatible_sources(
            document,
            {req.get_param_value("moduleId"), req.get_param_value("portId")});
        auto body = json::array();
        for (const auto& source : sources) {
            body.push_back({
                {"moduleId", source.endpoint.module_id},
                {"portId", source.endpoint.port_id},
                {"moduleName", source.module_name},
                {"portName", source.port_name},
                {"dataType", source.data_type},
                {"capabilities", source.capabilities},
            });
        }
        json_response(res, {
            {"sources", std::move(body)},
            {"count", sources.size()},
        });
    });

    server.Put("/module-graph", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!legacy_model_compat) {
            res.set_header("Allow", "GET");
            json_response(
                res,
                {
                    {
                        "error",
                        "The low-level graph is generated by the active "
                        "project and cannot be written directly",
                    },
                    {"code", "project_authority_required"},
                },
                405);
            return;
        }

        try {
            const auto body = json::parse(req.body);
            if (!body.contains("expectedRevision") ||
                !body["expectedRevision"].is_number_unsigned()) {
                json_response(res, {{"error", "expectedRevision is required and must be an unsigned integer"}}, 400);
                return;
            }
            if (body.contains("stopRuntime") &&
                !body.at("stopRuntime").is_boolean()) {
                json_response(
                    res,
                    {{"error", "stopRuntime must be a boolean"}},
                    400);
                return;
            }
            const bool stop_runtime =
                body.value("stopRuntime", false);
            auto graph_body = body;
            graph_body.erase("expectedRevision");
            graph_body.erase("stopRuntime");
            auto document = pamguard::core::module_graph_from_json(graph_body.dump());
            std::lock_guard update_lock(module_graph_update_mutex);
            const auto previous = module_graph.snapshot();
            const auto expected_revision =
                body["expectedRevision"].get<std::uint64_t>();
            if (expected_revision != previous.revision) {
                json_response(res, {
                    {"applied", false},
                    {"revision", previous.revision},
                    {"issues", graph_issues_to_json({{
                        "revision_conflict",
                        "Expected graph revision does not match the current revision",
                        {},
                        {},
                    }})},
                }, 409);
                return;
            }
            if (module_runtime.running() && !stop_runtime) {
                json_response(res, {
                    {"applied", false},
                    {"revision", previous.revision},
                    {"running", true},
                    {"issues", graph_issues_to_json({{
                        "runtime_running",
                        "The module graph can only be changed while idle; stop the runtime first or retry this complete update with stopRuntime=true",
                        {},
                        {},
                    }})},
                }, 409);
                return;
            }
            const auto validation =
                module_graph.validate(document);
            if (!validation.valid()) {
                json_response(res, {
                    {"applied", false},
                    {"revision", previous.revision},
                    {"issues", graph_issues_to_json(
                        validation.issues)},
                }, 422);
                return;
            }
            // Registry validation checks topology and port contracts. A
            // throwaway runtime build and lifecycle pass additionally validate
            // executable settings before the authoritative graph can change.
            try {
                pamguard::core::ModuleRuntime candidate_runtime;
                candidate_runtime.configure(document);
                candidate_runtime.start();
                candidate_runtime.stop();
            }
            catch (const std::exception& error) {
                json_response(res, {
                    {"applied", false},
                    {"revision", previous.revision},
                    {"issues", graph_issues_to_json({{
                        "invalid_runtime_settings",
                        error.what(),
                        {},
                        {},
                    }})},
                }, 422);
                return;
            }

            const bool runtime_was_running =
                module_runtime.running();
            std::size_t captures_stopped = 0;
            if (stop_runtime) {
                captures_stopped =
                    quiesce_module_captures(capture_state);
            }
            if (runtime_was_running) {
                try {
                    module_runtime.stop();
                }
                catch (const std::exception& error) {
                    json_response(res, {
                        {"error", "module graph update could not safely stop the runtime"},
                        {"detail", error.what()},
                        {"applied", false},
                        {"revision", previous.revision},
                        {"running", module_runtime.running()},
                        {"capturesStopped", captures_stopped},
                    }, 500);
                    return;
                }
            }

            const auto result = module_graph.apply(
                std::move(document),
                expected_revision);
            if (!result.applied) {
                const auto revision_conflict =
                    std::any_of(
                        result.issues.begin(),
                        result.issues.end(),
                        [](const auto& issue) { return issue.code == "revision_conflict"; });
                json_response(res, {
                    {"applied", false},
                    {"revision", result.revision},
                    {"running", module_runtime.running()},
                    {"capturesStopped", captures_stopped},
                    {"issues", graph_issues_to_json(result.issues)},
                }, revision_conflict ? 409 : 422);
                return;
            }
            const auto saved = module_graph.snapshot();
            try {
                // Commit durable state before switching the live runtime. If
                // either step fails, restore the previous graph, runtime, and
                // persisted document as one operator-visible transaction.
                persist_module_graph(module_graph_file, saved);
                module_runtime.configure(saved);
            }
            catch (const std::exception& update_error) {
                std::string rollback_error;
                try {
                    const auto restored = module_graph.restore(previous);
                    if (!restored.valid()) {
                        throw std::runtime_error(
                            "previous graph no longer validates");
                    }
                    module_runtime.configure(previous);
                    persist_module_graph(module_graph_file, previous);
                }
                catch (const std::exception& error) {
                    rollback_error = error.what();
                }
                json response = {
                    {"error", "module graph update failed"},
                    {"detail", update_error.what()},
                    {"rolledBack", rollback_error.empty()},
                    {"revision", module_graph.snapshot().revision},
                    {"running", module_runtime.running()},
                    {"capturesStopped", captures_stopped},
                };
                if (!rollback_error.empty()) {
                    response["rollbackError"] = rollback_error;
                }
                json_response(res, std::move(response), 500);
                return;
            }
            append_audit_event(audit_log_file, audit_mutex, {
                {"event", "module_graph_update"},
                {"revision", result.revision},
                {"moduleCount", saved.modules.size()},
                {"connectionCount", saved.connections.size()},
                {"persisted", !module_graph_file.empty()},
                {"stoppedRuntime", runtime_was_running},
                {"capturesStopped", captures_stopped},
            });
            json_response(res, {
                {"applied", true},
                {"revision", result.revision},
                {"persisted", !module_graph_file.empty()},
                {"running", module_runtime.running()},
                {"stoppedRuntime", runtime_was_running},
                {"capturesStopped", captures_stopped},
            });
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    server.Get("/data-blocks", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        std::lock_guard update_lock(module_graph_update_mutex);
        const auto descriptors = module_runtime.data_blocks();
        auto blocks = json::array();
        for (const auto& descriptor : descriptors) {
            auto block_json = data_block_to_json(descriptor);
            const auto block = module_runtime.find_block(descriptor.id);
            if (block) {
                block_json["stats"] =
                    data_block_stats_to_json(block->stats());
                const auto history = block->recent_history();
                block_json["oldestTimeMs"] = history.empty()
                    ? json(nullptr)
                    : json(history.front().metadata.time_unix_ms);
                block_json["latestTimeMs"] = history.empty()
                    ? json(nullptr)
                    : json(history.back().metadata.time_unix_ms);
                block_json["oldestStartSample"] = history.empty()
                    ? json(nullptr)
                    : json(history.front().metadata.start_sample);
                block_json["latestStartSample"] = history.empty()
                    ? json(nullptr)
                    : json(history.back().metadata.start_sample);
            }
            blocks.push_back(std::move(block_json));
        }
        json_response(res, {
            {"graphRevision", module_runtime.revision()},
            {"running", module_runtime.running()},
            {"dataBlocks", std::move(blocks)},
            {"count", descriptors.size()},
        });
    });

    server.Get("/module-runtime/status", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        std::lock_guard update_lock(module_graph_update_mutex);
        const auto active = project_authority.snapshot();
        const auto graph = module_graph.snapshot();
        const auto descriptors = module_runtime.data_blocks();
        std::unordered_map<std::string, pamguard::core::ModuleState>
            runtime_states;
        for (const auto& status : module_runtime.module_statuses()) {
            runtime_states.emplace(status.instance_id, status.state);
        }
        auto modules = json::array();
        for (const auto& module : graph.modules) {
            auto outputs = json::array();
            for (const auto& descriptor : descriptors) {
                if (descriptor.producer_module_id != module.id) {
                    continue;
                }
                auto output = data_block_to_json(descriptor);
                if (const auto block =
                        module_runtime.find_block(descriptor.id)) {
                    output["stats"] =
                        data_block_stats_to_json(block->stats());
                    const auto history =
                        block->recent_history();
                    output["oldestTimeMs"] = history.empty()
                        ? json(nullptr)
                        : json(
                            history.front()
                                .metadata.time_unix_ms);
                    output["latestTimeMs"] = history.empty()
                        ? json(nullptr)
                        : json(
                            history.back()
                                .metadata.time_unix_ms);
                    output["oldestStartSample"] =
                        history.empty()
                        ? json(nullptr)
                        : json(
                            history.front()
                                .metadata.start_sample);
                    output["latestStartSample"] =
                        history.empty()
                        ? json(nullptr)
                        : json(
                            history.back()
                                .metadata.start_sample);
                }
                outputs.push_back(std::move(output));
            }
            const auto found = runtime_states.find(module.id);
            const std::string state = !module.enabled
                ? "disabled"
                : found != runtime_states.end()
                    ? module_state_name(found->second)
                    : "external";
            modules.push_back({
                {"moduleId", module.id},
                {"typeId", module.type_id},
                {"name", module.name},
                {"enabled", module.enabled},
                {"state", state},
                {"outputs", std::move(outputs)},
            });
        }
        json_response(res, {
            {
                "authorityMode",
                legacy_model_compat
                    ? "legacyCompatibility"
                    : "project",
            },
            {
                "projectId",
                legacy_model_compat
                    ? json(nullptr)
                    : json(active.project.project_id),
            },
            {
                "workingRevision",
                legacy_model_compat
                    ? json(nullptr)
                    : json(active.working_revision),
            },
            {
                "projectionStatus",
                legacy_model_compat
                    ? json(nullptr)
                    : json(projection_status_name(
                          active.projection)),
            },
            {
                "prepared",
                legacy_model_compat
                    ? true
                    : project_runtime_prepared,
            },
            {"graphRevision", graph.revision},
            {"running", module_runtime.running()},
            {"modules", std::move(modules)},
            {"count", graph.modules.size()},
        });
    });

    server.Post("/module-runtime/control", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        try {
            const auto body = json::parse(req.body);
            const auto action = body.at("action").get<std::string>();
            std::lock_guard update_lock(module_graph_update_mutex);
            std::size_t captures_stopped = 0;
            if (action == "start") {
                if (!legacy_model_compat) {
                    const auto active =
                        project_authority.snapshot();
                    if (!active.projection.runnable()) {
                        encoded_json_response(
                            res,
                            json({
                                {
                                    "error",
                                    "The active project needs configuration "
                                    "before its runtime can start",
                                },
                                {"code", "project_not_runnable"},
                                {
                                    "projectionStatus",
                                    projection_status_name(
                                        active.projection),
                                },
                                {"currentEtag", active.etag},
                            }).dump(),
                            422,
                            active.etag);
                        return;
                    }
                    if (!project_runtime_prepared ||
                        module_runtime.revision() !=
                            active.working_revision) {
                        encoded_json_response(
                            res,
                            json({
                                {
                                    "error",
                                    "The active project runtime is not "
                                    "prepared at its working revision",
                                },
                                {
                                    "code",
                                    "project_runtime_unprepared",
                                },
                                {"currentEtag", active.etag},
                            }).dump(),
                            503,
                            active.etag);
                        return;
                    }
                }
                module_runtime.start();
            }
            else if (action == "stop") {
                captures_stopped =
                    quiesce_module_captures(capture_state);
                module_runtime.stop();
            }
            else if (action == "flush") {
                module_runtime.flush();
            }
            else if (action == "reset") {
                const bool restart =
                    body.value("restart", module_runtime.running());
                captures_stopped =
                    quiesce_module_captures(capture_state);
                module_runtime.stop();
                module_runtime.reset();
                if (restart) {
                    module_runtime.start();
                }
            }
            else {
                throw std::invalid_argument(
                    "action must be start, stop, flush, or reset");
            }
            append_audit_event(audit_log_file, audit_mutex, {
                {"event", "module_runtime_control"},
                {"action", action},
                {"revision", module_runtime.revision()},
                {"running", module_runtime.running()},
                {"capturesStopped", captures_stopped},
            });
            json_response(res, {
                {"action", action},
                {"graphRevision", module_runtime.revision()},
                {"running", module_runtime.running()},
                {"capturesStopped", captures_stopped},
            });
        }
        catch (const json::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
        catch (const std::invalid_argument& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
        catch (const std::logic_error& error) {
            json_response(res, {
                {"error", "module runtime lifecycle precondition failed"},
                {"detail", error.what()},
                {"running", module_runtime.running()},
            }, 409);
        }
        catch (const std::exception& error) {
            json_response(res, {
                {"error", "module runtime control failed"},
                {"detail", error.what()},
                {"running", module_runtime.running()},
            }, 500);
        }
    });

    server.Get("/workspaces", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        auto workspaces = json::array();
        {
            std::lock_guard lock(workspace_mutex);
            for (const auto& [id, layout] :
                 workspace_store.at("workspaces").items()) {
                workspaces.push_back({
                    {"id", id},
                    {"name", layout.value("name", id)},
                    {"updatedTimeMs",
                     layout.value("updatedTimeMs", std::int64_t{0})},
                    {"displayCount",
                     layout.at("displays").size()},
                });
            }
        }
        const auto count = workspaces.size();
        json_response(res, {
            {"schemaVersion", 1},
            {"workspaces", std::move(workspaces)},
            {"count", count},
            {"persistent", !workspace_file.empty()},
        });
    });

    server.Get(R"(/workspaces/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        const auto id = req.matches[1].str();
        if (!valid_workspace_id(id)) {
            json_response(res, {{"error", "invalid workspace id"}}, 400);
            return;
        }
        std::lock_guard lock(workspace_mutex);
        const auto& workspaces = workspace_store.at("workspaces");
        if (!workspaces.contains(id)) {
            json_response(res, {{"error", "unknown workspace"}}, 404);
            return;
        }
        auto body = workspaces.at(id);
        body["id"] = id;
        json_response(res, body);
    });

    server.Put(R"(/workspaces/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!legacy_model_compat) {
            res.set_header("Allow", "GET");
            json_response(
                res,
                {
                    {
                        "error",
                        "Display hierarchy and layout are owned by the "
                        "active project and cannot be written through "
                        "/workspaces",
                    },
                    {"code", "project_authority_required"},
                },
                405);
            return;
        }

        const auto id = req.matches[1].str();
        if (!valid_workspace_id(id)) {
            json_response(res, {{"error", "invalid workspace id"}}, 400);
            return;
        }
        try {
            auto layout = json::parse(req.body);
            validate_workspace_layout(layout);
            layout.erase("id");
            layout["updatedTimeMs"] = current_unix_ms();
            {
                std::lock_guard lock(workspace_mutex);
                auto next = workspace_store;
                next["workspaces"][id] = layout;
                persist_json_file(
                    workspace_file,
                    next,
                    "workspace file");
                workspace_store = std::move(next);
            }
            append_audit_event(audit_log_file, audit_mutex, {
                {"event", "workspace_saved"},
                {"workspaceId", id},
                {"displayCount", layout.at("displays").size()},
                {"persisted", !workspace_file.empty()},
            });
            json_response(res, {
                {"saved", true},
                {"id", id},
                {"updatedTimeMs", layout.at("updatedTimeMs")},
                {"persistent", !workspace_file.empty()},
            });
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    server.Delete(R"(/workspaces/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!legacy_model_compat) {
            res.set_header("Allow", "GET");
            json_response(
                res,
                {
                    {
                        "error",
                        "Display hierarchy and layout are owned by the "
                        "active project and cannot be deleted through "
                        "/workspaces",
                    },
                    {"code", "project_authority_required"},
                },
                405);
            return;
        }

        const auto id = req.matches[1].str();
        if (!valid_workspace_id(id)) {
            json_response(res, {{"error", "invalid workspace id"}}, 400);
            return;
        }
        {
            std::lock_guard lock(workspace_mutex);
            if (!workspace_store.at("workspaces").contains(id)) {
                json_response(res, {{"error", "unknown workspace"}}, 404);
                return;
            }
            auto next = workspace_store;
            next["workspaces"].erase(id);
            try {
                persist_json_file(
                    workspace_file,
                    next,
                    "workspace file");
            }
            catch (const std::exception& error) {
                json_response(res, {{"error", error.what()}}, 500);
                return;
            }
            workspace_store = std::move(next);
        }
        append_audit_event(audit_log_file, audit_mutex, {
            {"event", "workspace_deleted"},
            {"workspaceId", id},
            {"persisted", !workspace_file.empty()},
        });
        json_response(res, {{"deleted", true}, {"id", id}});
    });

    server.Get(R"(/data-blocks/([^/]+)/history)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        const auto block_id = req.matches[1].str();
        const auto block = module_runtime.find_block(block_id);
        if (!block) {
            json_response(res, {{"error", "unknown data block"}}, 404);
            return;
        }
        std::size_t limit = block->descriptor().history_capacity;
        if (req.has_param("limit")) {
            try {
                limit = std::stoull(req.get_param_value("limit"));
            }
            catch (const std::exception&) {
                json_response(
                    res,
                    {{"error", "limit must be an unsigned integer"}},
                    400);
                return;
            }
            limit = std::min<std::size_t>(limit, 4096);
        }
        auto history = block->recent_history();
        const auto first = history.size() > limit
            ? history.size() - limit
            : 0;
        auto units = json::array();
        for (std::size_t index = first; index < history.size(); ++index) {
            units.push_back(data_unit_to_json(history[index]));
        }
        json_response(res, {
            {"blockId", block_id},
            {"graphRevision", module_runtime.revision()},
            {"units", std::move(units)},
            {"count", history.size() - first},
            {"stats", data_block_stats_to_json(block->stats())},
        });
    });

    server.Get(R"(/data-blocks/([^/]+)/audio-f32le)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        const auto block_id = req.matches[1].str();
        const auto block = module_runtime.find_block(block_id);
        if (!block ||
            block->descriptor().data_type !=
                pamguard::core::kRawAudioDataType) {
            json_response(
                res,
                {{"error", "unknown or non-playable raw-audio block"}},
                404);
            return;
        }
        std::vector<std::size_t> channels;
        bool framed = false;
        try {
            if (req.has_param("channels")) {
                std::stringstream values(
                    req.get_param_value("channels"));
                std::string token;
                while (std::getline(values, token, ',')) {
                    if (token.empty()) {
                        throw std::invalid_argument(
                            "channels contains an empty value");
                    }
                    std::size_t parsed = 0;
                    const auto channel = std::stoull(token, &parsed);
                    if (parsed != token.size() || channel >= 32 ||
                        (block->descriptor().channel_bitmap &
                         (std::uint32_t{1} << channel)) == 0) {
                        throw std::invalid_argument(
                            "channels contains an unavailable source channel");
                    }
                    channels.push_back(channel);
                }
            }
            else {
                for (std::size_t channel = 0; channel < 32; ++channel) {
                    if ((block->descriptor().channel_bitmap &
                         (std::uint32_t{1} << channel)) != 0) {
                        channels.push_back(channel);
                    }
                }
            }
            std::sort(channels.begin(), channels.end());
            channels.erase(
                std::unique(channels.begin(), channels.end()),
                channels.end());
            if (channels.empty() || channels.size() > 32) {
                throw std::invalid_argument(
                    "at least one playable channel is required");
            }
            if (req.has_param("format")) {
                const auto format = req.get_param_value("format");
                if (format == "framed") {
                    framed = true;
                }
                else if (format != "raw") {
                    throw std::invalid_argument(
                        "format must be raw or framed");
                }
            }
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
            return;
        }

        struct AudioStreamState {
            struct Chunk {
                std::string bytes;
                std::size_t frame_count = 0;
            };
            std::mutex mutex;
            std::condition_variable condition;
            std::deque<Chunk> pending;
            pamguard::core::Subscription subscription;
            bool closed = false;
            std::uint64_t dropped_frames = 0;
        };
        const auto state = std::make_shared<AudioStreamState>();
        const auto graph_revision = module_runtime.revision();
        state->subscription = block->subscribe(
            [state, channels, framed](
                const pamguard::core::DataUnit& unit) {
                const auto* audio =
                    std::any_cast<pamguard::core::AudioChunk>(
                        &unit.payload);
                if (audio == nullptr || audio->channel_count == 0) {
                    return;
                }
                const auto frames = audio->frame_count();
                std::string pcm;
                pcm.resize(
                    frames * channels.size() * sizeof(float));
                std::size_t offset = 0;
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    for (const auto channel : channels) {
                        const auto value = channel < audio->channel_count
                            ? static_cast<float>(
                                audio->sample(frame, channel))
                            : 0.0f;
                        std::memcpy(
                            pcm.data() + offset,
                            &value,
                            sizeof(value));
                        offset += sizeof(value);
                    }
                }
                {
                    std::lock_guard lock(state->mutex);
                    if (state->closed) {
                        return;
                    }
                    if (state->pending.size() == 64) {
                        state->dropped_frames +=
                            state->pending.front().frame_count;
                        state->pending.pop_front();
                    }
                    std::string bytes;
                    if (framed) {
                        constexpr std::size_t header_size = 40;
                        bytes.resize(header_size + pcm.size());
                        std::memcpy(bytes.data(), "PGA1", 4);
                        const auto put = [&](std::size_t target_offset,
                                             const auto& value) {
                            std::memcpy(
                                bytes.data() + target_offset,
                                &value,
                                sizeof(value));
                        };
                        const std::uint32_t encoded_header_size =
                            header_size;
                        const std::uint32_t encoded_channels =
                            static_cast<std::uint32_t>(
                                channels.size());
                        const std::uint32_t encoded_frames =
                            static_cast<std::uint32_t>(frames);
                        const std::int64_t time_unix_ms =
                            unit.metadata.time_unix_ms;
                        const std::uint64_t start_sample =
                            unit.metadata.start_sample;
                        put(4, encoded_header_size);
                        put(8, encoded_channels);
                        put(12, encoded_frames);
                        put(16, time_unix_ms);
                        put(24, start_sample);
                        put(32, state->dropped_frames);
                        std::memcpy(
                            bytes.data() + header_size,
                            pcm.data(),
                            pcm.size());
                    }
                    else {
                        bytes = std::move(pcm);
                    }
                    state->pending.push_back(
                        {std::move(bytes), frames});
                }
                state->condition.notify_one();
            },
            {pamguard::core::DeliveryMode::QueuedDropOldest, 32});
        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-PAMGuard-Block-Id", block_id);
        res.set_header(
            "X-PAMGuard-Sample-Rate-Hz",
            std::to_string(block->descriptor().sample_rate_hz));
        res.set_header(
            "X-PAMGuard-Channel-Count",
            std::to_string(channels.size()));
        res.set_header(
            "X-PAMGuard-Channels",
            req.has_param("channels")
                ? req.get_param_value("channels")
                : "all");
        res.set_header(
            "X-PAMGuard-Audio-Framing",
            framed ? "pga1" : "raw");
        res.set_chunked_content_provider(
            framed
                ? "application/vnd.pamguard.audio-f32le"
                : "application/octet-stream",
            [&, state, graph_revision](
                std::size_t,
                httplib::DataSink& sink) -> bool {
                decltype(state->pending) chunks;
                {
                    std::unique_lock lock(state->mutex);
                    state->condition.wait_for(
                        lock,
                        std::chrono::seconds(1),
                        [&] {
                            return state->closed ||
                                !state->pending.empty();
                        });
                    chunks.swap(state->pending);
                }
                if (module_runtime.revision() != graph_revision ||
                    state->closed) {
                    sink.done();
                    return false;
                }
                for (const auto& chunk : chunks) {
                    if (!sink.write(
                            chunk.bytes.data(),
                            chunk.bytes.size())) {
                        return false;
                    }
                }
                return true;
            },
            [state](bool) {
                {
                    std::lock_guard lock(state->mutex);
                    state->closed = true;
                }
                state->condition.notify_all();
                state->subscription.cancel();
            });
    });

    server.Get(R"(/data-blocks/([^/]+)/stream)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        const auto block_id = req.matches[1].str();
        const auto block = module_runtime.find_block(block_id);
        if (!block) {
            json_response(res, {{"error", "unknown data block"}}, 404);
            return;
        }
        struct StreamSelection {
            std::uint32_t channel_bitmap = 0;
            std::uint32_t sequence_bitmap = 0;
            std::size_t first_bin = 0;
            std::size_t bin_count = 0;
            std::int64_t cadence_ms = 0;
            std::size_t history_count = 0;
        };
        StreamSelection selection;
        try {
            const auto parse_bitmap = [&](const char* name) {
                std::uint32_t bitmap = 0;
                if (!req.has_param(name)) {
                    return bitmap;
                }
                std::stringstream values(req.get_param_value(name));
                std::string token;
                while (std::getline(values, token, ',')) {
                    std::size_t parsed = 0;
                    const auto index = std::stoull(token, &parsed);
                    if (parsed != token.size() || index >= 32) {
                        throw std::invalid_argument(
                            std::string(name) +
                            " must contain comma-separated indices from 0 to 31");
                    }
                    bitmap |= std::uint32_t{1} << index;
                }
                return bitmap;
            };
            selection.channel_bitmap = parse_bitmap("channels");
            selection.sequence_bitmap = parse_bitmap("sequences");
            if (req.has_param("firstBin")) {
                selection.first_bin =
                    std::stoull(req.get_param_value("firstBin"));
            }
            if (req.has_param("binCount")) {
                selection.bin_count =
                    std::stoull(req.get_param_value("binCount"));
            }
            if (req.has_param("cadenceMs")) {
                selection.cadence_ms =
                    std::stoll(req.get_param_value("cadenceMs"));
                if (selection.cadence_ms < 0) {
                    throw std::invalid_argument(
                        "cadenceMs must be zero or greater");
                }
            }
            if (req.has_param("history")) {
                selection.history_count = std::min<std::size_t>(
                    std::stoull(req.get_param_value("history")),
                    4096);
            }
            if (req.has_param("format") &&
                req.get_param_value("format") != "ndjson") {
                throw std::invalid_argument(
                    "Only format=ndjson is currently supported");
            }
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
            return;
        }
        const auto encode = [selection](
                                const pamguard::core::DataUnit& unit)
            -> std::optional<std::string> {
            if (selection.channel_bitmap != 0 &&
                unit.metadata.channel_bitmap != 0 &&
                (selection.channel_bitmap &
                 unit.metadata.channel_bitmap) == 0) {
                return {};
            }
            if (selection.sequence_bitmap != 0 &&
                unit.metadata.sequence_bitmap != 0 &&
                (selection.sequence_bitmap &
                 unit.metadata.sequence_bitmap) == 0) {
                return {};
            }
            auto encoded = data_unit_to_json(unit);
            if (selection.channel_bitmap != 0 &&
                encoded["payload"].is_object() &&
                encoded["payload"].contains("interleavedPcm") &&
                encoded["payload"]["interleavedPcm"].is_array() &&
                encoded["payload"].contains("channelCount")) {
                const auto source_channel_count =
                    encoded["payload"]["channelCount"]
                        .get<std::size_t>();
                std::vector<std::size_t> selected_channels;
                for (std::size_t channel = 0;
                     channel < source_channel_count &&
                     channel < 32;
                     ++channel) {
                    if ((selection.channel_bitmap &
                         (std::uint32_t{1} << channel)) != 0) {
                        selected_channels.push_back(channel);
                    }
                }
                if (selected_channels.empty()) {
                    return {};
                }
                const auto& source =
                    encoded["payload"]["interleavedPcm"];
                const auto frame_count =
                    source_channel_count == 0
                    ? 0
                    : source.size() / source_channel_count;
                auto selected = json::array();
                for (std::size_t frame = 0;
                     frame < frame_count;
                     ++frame) {
                    for (const auto channel :
                         selected_channels) {
                        selected.push_back(
                            source[
                                frame * source_channel_count +
                                channel]);
                    }
                }
                encoded["payload"]["interleavedPcm"] =
                    std::move(selected);
                encoded["payload"]["channelCount"] =
                    selected_channels.size();
                encoded["payload"]["sourceChannels"] =
                    selected_channels;
                encoded["channelBitmap"] =
                    unit.metadata.channel_bitmap &
                    selection.channel_bitmap;
            }
            if ((selection.first_bin != 0 ||
                 selection.bin_count != 0) &&
                encoded["payload"].is_object() &&
                encoded["payload"].contains("magnitudeSquared") &&
                encoded["payload"]["magnitudeSquared"].is_array()) {
                const auto& source =
                    encoded["payload"]["magnitudeSquared"];
                const auto first =
                    std::min(selection.first_bin, source.size());
                const auto count = selection.bin_count == 0
                    ? source.size() - first
                    : std::min(
                        selection.bin_count,
                        source.size() - first);
                auto bins = json::array();
                for (std::size_t index = first;
                     index < first + count;
                     ++index) {
                    bins.push_back(source[index]);
                }
                encoded["payload"]["magnitudeSquared"] =
                    std::move(bins);
                encoded["payload"]["firstBin"] = first;
            }
            return encoded.dump();
        };
        struct StreamState {
            std::mutex mutex;
            std::condition_variable condition;
            std::deque<std::string> pending;
            pamguard::core::Subscription subscription;
            bool closed = false;
            std::int64_t last_time_ms = 0;
            std::uint64_t dropped_units = 0;
        };
        const auto state = std::make_shared<StreamState>();
        const auto graph_revision = module_runtime.revision();
        if (selection.history_count > 0) {
            const auto history = block->recent_history();
            const auto first = history.size() > selection.history_count
                ? history.size() - selection.history_count
                : 0;
            for (std::size_t index = first;
                 index < history.size();
                 ++index) {
                if (const auto line = encode(history[index])) {
                    state->pending.push_back(*line);
                }
            }
        }
        state->subscription = block->subscribe(
            [state, selection, encode](
                const pamguard::core::DataUnit& unit) {
                {
                    std::lock_guard lock(state->mutex);
                    if (state->closed) {
                        return;
                    }
                    if (selection.cadence_ms > 0 &&
                        state->last_time_ms != 0 &&
                        unit.metadata.time_unix_ms -
                            state->last_time_ms <
                            selection.cadence_ms) {
                        return;
                    }
                    auto line = encode(unit);
                    if (!line) {
                        return;
                    }
                    state->last_time_ms =
                        unit.metadata.time_unix_ms;
                    if (state->pending.size() == 256) {
                        state->pending.pop_front();
                        ++state->dropped_units;
                        auto marked = json::parse(*line);
                        marked["discontinuity"] = true;
                        marked["presentationDropped"] =
                            state->dropped_units;
                        *line = marked.dump();
                    }
                    state->pending.push_back(*line);
                }
                state->condition.notify_one();
            },
            {pamguard::core::DeliveryMode::QueuedDropOldest, 64});
        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-PAMGuard-Block-Id", block_id);
        res.set_header(
            "X-PAMGuard-Graph-Revision",
            std::to_string(graph_revision));
        res.set_chunked_content_provider(
            "application/x-ndjson",
            [&, state, graph_revision](std::size_t, httplib::DataSink& sink) -> bool {
                std::deque<std::string> lines;
                {
                    std::unique_lock lock(state->mutex);
                    state->condition.wait_for(
                        lock,
                        std::chrono::seconds(1),
                        [&] { return state->closed || !state->pending.empty(); });
                    lines.swap(state->pending);
                }
                if (module_runtime.revision() != graph_revision || state->closed) {
                    sink.done();
                    return false;
                }
                if (lines.empty()) {
                    return sink.write("\n", 1);
                }
                for (const auto& line : lines) {
                    if (!sink.write(line.data(), line.size()) ||
                        !sink.write("\n", 1)) {
                        return false;
                    }
                }
                return true;
            },
            [state](bool) {
                {
                    std::lock_guard lock(state->mutex);
                    state->closed = true;
                }
                state->condition.notify_all();
                state->subscription.cancel();
            });
    });

    server.Post(R"(/module-runtime/acquisitions/([^/]+)/pcm-f32le)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        try {
            std::lock_guard update_lock(module_graph_update_mutex);
            const auto module_id = req.matches[1].str();
            const auto source_block = module_runtime.find_block(
                pamguard::core::ModuleRuntime::block_id(module_id, "audio"));
            if (!source_block ||
                source_block->descriptor().data_type !=
                    pamguard::core::kRawAudioDataType) {
                json_response(res, {{"error", "unknown acquisition module"}}, 404);
                return;
            }
            if (max_pcm_body_bytes > 0 && req.body.size() > max_pcm_body_bytes) {
                json_response(res, {
                    {"error", "PCM body exceeds maximum size"},
                    {"maxPcmBodyBytes", max_pcm_body_bytes},
                }, 413);
                return;
            }
            const auto channel_count = channel_count_from_bitmap(
                source_block->descriptor().channel_bitmap);
            const auto sample_rate = static_cast<std::uint32_t>(
                std::llround(source_block->descriptor().sample_rate_hz));
            const auto bytes_per_frame = channel_count * sizeof(float);
            if (req.body.empty() ||
                bytes_per_frame == 0 ||
                req.body.size() % bytes_per_frame != 0) {
                throw std::invalid_argument(
                    "PCM body must contain whole interleaved f32le frames");
            }
            const auto frame_count = req.body.size() / bytes_per_frame;
            const auto start_sample = parse_uint64_param(req, "startSample", 0);
            const auto time_ms = req.has_param("timeMs")
                ? static_cast<std::int64_t>(
                    std::stoll(req.get_param_value("timeMs")))
                : static_cast<std::int64_t>(
                    static_cast<double>(start_sample) * 1000.0 / sample_rate);
            pamguard::core::AudioChunk chunk;
            chunk.start_sample = start_sample;
            chunk.time_unix_ms = time_ms;
            chunk.sample_rate_hz = sample_rate;
            chunk.channel_count = channel_count;
            chunk.interleaved_pcm.resize(frame_count * channel_count);
            const auto* bytes =
                reinterpret_cast<const unsigned char*>(req.body.data());
            for (std::size_t frame = 0; frame < frame_count; ++frame) {
                for (std::size_t channel = 0; channel < channel_count; ++channel) {
                    const auto offset =
                        (frame * channel_count + channel) * sizeof(float);
                    chunk.interleaved_pcm[
                        frame * channel_count + channel] =
                        read_float_le(bytes + offset);
                }
            }
            module_runtime.ingest(module_id, std::move(chunk));
            json_response(res, {
                {"accepted", true},
                {"moduleId", module_id},
                {"inputFrames", frame_count},
                {"startSample", start_sample},
                {"graphRevision", module_runtime.revision()},
            }, 202);
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    server.Post(R"(/module-runtime/operator-inputs/([^/]+)/events)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        try {
            std::lock_guard update_lock(
                module_graph_update_mutex);
            const auto body = json::parse(req.body);
            if (!body.is_object()) {
                throw std::invalid_argument(
                    "Operator event body must be an object");
            }
            pamguard::core::GraphOperatorEvent event;
            event.category =
                body.value("category", std::string{});
            event.label =
                body.value("label", std::string{});
            event.notes =
                body.value("notes", std::string{});
            event.value = body.value("value", 0.0);
            if (event.label.empty()) {
                throw std::invalid_argument(
                    "Operator event label is required");
            }
            if (event.category.size() > 128 ||
                event.label.size() > 512 ||
                event.notes.size() > 8192 ||
                !std::isfinite(event.value)) {
                throw std::invalid_argument(
                    "Operator event fields exceed their limits");
            }
            const auto module_id = req.matches[1].str();
            module_runtime.publish_operator_event(
                module_id,
                std::move(event),
                body.value("timeMs", std::int64_t{0}),
                body.value("startSample", std::int64_t{0}));
            append_audit_event(
                audit_log_file,
                audit_mutex,
                {
                    {"event", "operator_input"},
                    {"moduleId", module_id},
                    {"label", body.at("label")},
                });
            json_response(res, {
                {"accepted", true},
                {"moduleId", module_id},
                {"graphRevision", module_runtime.revision()},
            });
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    server.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard lifecycle_lock(
            module_graph_update_mutex);
        const auto active =
            project_authority.snapshot();
        json_response(res, {
            {"ok", true},
            {
                "authorityMode",
                legacy_model_compat
                    ? "legacyCompatibility"
                    : "project",
            },
            {
                "activeProjectId",
                legacy_model_compat
                    ? json(nullptr)
                    : json(active.project.project_id),
            },
            {
                "projectWorkingRevision",
                legacy_model_compat
                    ? json(nullptr)
                    : json(active.working_revision),
            },
            {
                "projectSavedRevision",
                !legacy_model_compat &&
                    active.saved_revision
                    ? json(*active.saved_revision)
                    : json(nullptr),
            },
            {
                "projectDirty",
                legacy_model_compat
                    ? json(nullptr)
                    : json(active.dirty),
            },
            {
                "projectProjectionStatus",
                legacy_model_compat
                    ? json(nullptr)
                    : json(projection_status_name(
                          active.projection)),
            },
            {
                "projectRuntimePrepared",
                legacy_model_compat
                    ? json(nullptr)
                    : json(project_runtime_prepared),
            },
            {"projectRuntimeRunning", module_runtime.running()},
            {"sessions", manager.session_count()},
            {"maxSessions", max_sessions},
            {"legacyCompatibilityEnabled", legacy_model_compat},
            {"resultSchemaVersion", kResultSchemaVersion},
            {"captureEnabled", capture_enabled},
            {
                "jobQueueEnabled",
                legacy_model_compat && !job_audio_dir.empty(),
            },
            {
                "audioArchiveEnabled",
                legacy_model_compat && !audio_archive_dir.empty(),
            },
            {"resultFeedDepth", result_feed_depth},
            {
                "jobWorkers",
                legacy_model_compat && !job_audio_dir.empty()
                    ? job_workers
                    : 0,
            },
            {"maxPcmBodyBytes", max_pcm_body_bytes},
            {"maxArchiveQueryRecords", max_archive_query_records},
            {"httpThreads", http_threads},
            {"authRequired", !api_key.empty()},
            {"corsOrigin", cors_origin},
            {"sessionMetadataRequired", require_session_metadata},
            {"auditLogEnabled", !audit_log_file.empty()},
            {
                "sessionConfigPersistenceEnabled",
                legacy_model_compat && !session_config_dir.empty(),
            },
            {
                "resultArchiveEnabled",
                legacy_model_compat && !result_archive_dir.empty(),
            },
            {
                "archiveEventIndexEnabled",
                legacy_model_compat && !result_archive_dir.empty(),
            },
            {"ingestStatusEnabled", !ingest_status_file.empty()},
            {"webUiEnabled", !web_ui_file.empty()},
            {"webAssetsEnabled", !web_asset_root.empty()},
            {"openApiEnabled", !openapi_file.empty()},
            {
                "moduleGraphPersistenceEnabled",
                legacy_model_compat &&
                    !module_graph_file.empty(),
            },
            {
                "legacyModuleGraphFileIgnored",
                !legacy_model_compat &&
                    !module_graph_file.empty(),
            },
            {"moduleGraphRevision", module_graph.snapshot().revision},
            {
                "workspacePersistenceEnabled",
                legacy_model_compat && !workspace_file.empty(),
            },
            {
                "workspaceWritesEnabled",
                legacy_model_compat,
            },
        });
    });

    server.Get("/ready", [&](const httplib::Request&, httplib::Response& res) {
        const auto sessions = manager.session_count();
        const bool capacity_available =
            max_sessions == 0 || sessions < max_sessions;
        if (legacy_model_compat) {
            // Preserve the deprecated AnalysisSession deployment contract
            // exactly inside its explicitly selected compatibility mode.
            json_response(res, {
                {"ok", capacity_available},
                {"ready", capacity_available},
                {"authorityMode", "legacyCompatibility"},
                {"activeProjectId", nullptr},
                {"projectProjectionStatus", nullptr},
                {"projectRuntimePrepared", nullptr},
                {"sessions", sessions},
                {"maxSessions", max_sessions},
                {"capacityAvailable", capacity_available},
            }, capacity_available ? 200 : 503);
            return;
        }

        std::lock_guard lifecycle_lock(
            module_graph_update_mutex);
        const auto active =
            project_authority.snapshot();
        (void)reconcile_acquisition_host_state(active);

        json issues = json::array();
        for (const auto& issue : active.projection.issues) {
            const bool needs_configuration =
                issue.issue_class ==
                pamguard::project::ProjectionIssueClass::
                    NeedsConfiguration;
            json item = {
                {"source", "projectProjection"},
                {"code", issue.code},
                {"message", issue.message},
                {
                    "action",
                    needs_configuration
                        ? "configure-controlled-unit"
                        : "edit-project",
                },
            };
            if (!issue.unit_id.empty()) {
                item["unitId"] = issue.unit_id;
            }
            if (!issue.role_id.empty()) {
                item["roleId"] = issue.role_id;
            }
            if (!issue.display_id.empty()) {
                item["displayId"] = issue.display_id;
            }
            issues.push_back(std::move(item));
        }

        const auto runtime_revision = module_runtime.revision();
        const bool runtime_prepared_at_revision =
            project_runtime_prepared &&
            runtime_revision == active.working_revision;
        if (active.projection.runnable() &&
            !runtime_prepared_at_revision) {
            json issue = {
                {"source", "projectRuntime"},
                {"code", "project_runtime_preparation_failed"},
                {
                    "message",
                    project_runtime_prepared
                        ? "The prepared runtime revision does not match the "
                          "active project working revision"
                        : "The active project runtime is not prepared",
                },
                {"action", "reprepare-project-runtime"},
                {
                    "expectedWorkingRevision",
                    active.working_revision,
                },
                {"runtimeRevision", runtime_revision},
            };
            issues.push_back(std::move(issue));
        }

        const auto recorder_unit_ids =
            active_sound_recorder_unit_ids(active);
        const bool storage_required =
            !recorder_unit_ids.empty();
        bool storage_ready = true;
        json storage_available_bytes = nullptr;
        json storage_capacity_bytes = nullptr;
        if (storage_required) {
            const auto current_storage =
                sound_recorder_deployment_from_environment();
            storage_ready = current_storage.ready();
            std::string storage_error =
                current_storage.readiness_error;
            std::string storage_code =
                "required_storage_unavailable";
            if (storage_ready) {
                std::error_code space_error;
                const auto space =
                    std::filesystem::space(
                        *current_storage.root,
                        space_error);
                if (space_error) {
                    storage_ready = false;
                    storage_error =
                        "PAMGUARD_RECORDING_ROOT capacity could not be "
                        "inspected";
                }
                else {
                    storage_available_bytes = space.available;
                    storage_capacity_bytes = space.capacity;
                    if (space.available == 0) {
                        storage_ready = false;
                        storage_code = "required_storage_full";
                        storage_error =
                            "PAMGUARD_RECORDING_ROOT has no available "
                            "space";
                    }
                }
            }
            if (!storage_ready) {
                for (const auto& unit_id : recorder_unit_ids) {
                    issues.push_back({
                        {"source", "storage"},
                        {"code", storage_code},
                        {"message", storage_error},
                        {"unitId", unit_id},
                        {"action", "restore-recording-root"},
                    });
                }
            }
        }

        std::size_t required_capture_count = 0;
        std::size_t healthy_capture_count = 0;
        {
            std::lock_guard capture_lock(
                capture_state.mutex);
            for (auto& [key, requirement] :
                 capture_state.required_project_captures) {
                ++required_capture_count;
                auto capture =
                    capture_state.running.find(key);
                if (capture != capture_state.running.end() &&
                    !capture_process_running(capture->second)) {
                    requirement.child_failed = true;
                    close_capture_process(capture->second);
                    capture_state.running.erase(capture);
                    capture = capture_state.running.end();
                }
                if (requirement.child_failed) {
                    issues.push_back({
                        {"source", "acquisitionCapture"},
                        {"code", "required_capture_dead"},
                        {
                            "message",
                            "The required Acquisition capture process "
                            "exited unexpectedly",
                        },
                        {
                            "unitId",
                            requirement.target.
                                acquisition_unit_id,
                        },
                        {"action", "restart-capture"},
                        {
                            "actionTarget",
                            "/v1/projects/active/acquisitions/" +
                                requirement.target.
                                    acquisition_unit_id +
                                "/capture:start",
                        },
                    });
                    continue;
                }
                if (capture == capture_state.running.end()) {
                    issues.push_back({
                        {"source", "acquisitionCapture"},
                        {"code", "required_capture_not_running"},
                        {
                            "message",
                            "The required Acquisition capture process is "
                            "not running",
                        },
                        {
                            "unitId",
                            requirement.target.
                                acquisition_unit_id,
                        },
                        {"action", "restart-capture"},
                        {
                            "actionTarget",
                            "/v1/projects/active/acquisitions/" +
                                requirement.target.
                                    acquisition_unit_id +
                                "/capture:start",
                        },
                    });
                    continue;
                }
                ++healthy_capture_count;
            }
        }

        const bool ready = issues.empty();
        json_response(res, {
            {"schemaVersion", 1},
            {"ok", ready},
            {"ready", ready},
            {"authorityMode", "project"},
            {"activeProjectId", active.project.project_id},
            {"workingRevision", active.working_revision},
            {
                "savedRevision",
                active.saved_revision
                    ? json(*active.saved_revision)
                    : json(nullptr),
            },
            {"dirty", active.dirty},
            {
                "projectProjectionStatus",
                projection_status_name(active.projection),
            },
            {
                "projectRuntimePrepared",
                runtime_prepared_at_revision,
            },
            {"projectRuntimeRevision", runtime_revision},
            {
                "projectRuntimeRunning",
                module_runtime.running(),
            },
            {
                "acquisitionCapture",
                {
                    {
                        "required",
                        required_capture_count > 0,
                    },
                    {
                        "requiredCount",
                        required_capture_count,
                    },
                    {
                        "healthyCount",
                        healthy_capture_count,
                    },
                    {
                        "ready",
                        healthy_capture_count ==
                            required_capture_count,
                    },
                },
            },
            {
                "storage",
                {
                    {"required", storage_required},
                    {"ready", storage_ready},
                    {
                        "recorderUnitCount",
                        recorder_unit_ids.size(),
                    },
                    {
                        "availableBytes",
                        storage_available_bytes,
                    },
                    {
                        "capacityBytes",
                        storage_capacity_bytes,
                    },
                },
            },
            {"issues", std::move(issues)},
        }, ready ? 200 : 503);
    });

    auto serve_web_ui = [&](const httplib::Request&, httplib::Response& res) {
        if (web_ui_file.empty()) {
            json_response(res, {{"error", "web UI is not configured"}}, 404);
            return;
        }
        try {
            res.status = 200;
            res.set_content(read_text_file(web_ui_file), "text/html; charset=utf-8");
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 500);
        }
    };
    server.Get("/", serve_web_ui);
    server.Get("/index.html", serve_web_ui);

    server.Get(
        R"(/assets/(.*))",
        [&](const httplib::Request& req, httplib::Response& res) {
            if (web_asset_root.empty()) {
                json_response(
                    res,
                    {{"error", "web assets are not configured"}},
                    404);
                return;
            }

            const auto resolution = resolve_web_asset(
                web_asset_root,
                req.matches[1].str());
            switch (resolution.status) {
            case WebAssetStatus::Forbidden:
                json_response(
                    res,
                    {{"error", "asset path is outside the allowed assets subtree"}},
                    403);
                return;
            case WebAssetStatus::NotFound:
                json_response(res, {{"error", "asset not found"}}, 404);
                return;
            case WebAssetStatus::Unsupported:
                json_response(
                    res,
                    {{"error", "unsupported web asset type"}},
                    415);
                return;
            case WebAssetStatus::Ok:
                break;
            }

            try {
                res.status = 200;
                res.set_header("X-Content-Type-Options", "nosniff");
                res.set_content(
                    read_binary_file(resolution.path),
                    std::string(resolution.content_type));
            }
            catch (const std::exception& error) {
                json_response(res, {{"error", error.what()}}, 500);
            }
        });

    auto serve_openapi = [&](const httplib::Request&, httplib::Response& res) {
        if (openapi_file.empty()) {
            json_response(res, {{"error", "OpenAPI file is not configured"}}, 404);
            return;
        }
        try {
            res.status = 200;
            res.set_content(read_text_file(openapi_file), "application/yaml; charset=utf-8");
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 500);
        }
    };
    server.Get("/openapi.yaml", serve_openapi);

    server.Get("/metrics", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        pamguard::project::ActiveProjectSnapshot active_project;
        bool active_runtime_prepared = false;
        bool active_runtime_running = false;
        {
            std::lock_guard lifecycle_lock(
                module_graph_update_mutex);
            active_project = project_authority.snapshot();
            active_runtime_prepared =
                project_runtime_prepared;
            active_runtime_running =
                module_runtime.running();
        }
        std::ostringstream metrics;
        const auto active_project_label =
            prometheus_label_escape(
                active_project.project.project_id);
        metrics << "# HELP pamguard_authority_mode_info Active configuration authority mode\n";
        metrics << "# TYPE pamguard_authority_mode_info gauge\n";
        metrics << "pamguard_authority_mode_info{mode=\""
                << (legacy_model_compat
                        ? "legacyCompatibility"
                        : "project")
                << "\"} 1\n";
        metrics << "# HELP pamguard_active_project_info Active unified project identity\n";
        metrics << "# TYPE pamguard_active_project_info gauge\n";
        metrics << "pamguard_active_project_info{project_id=\""
                << active_project_label << "\"} 1\n";
        metrics << "# HELP pamguard_active_project_working_revision In-memory project working revision\n";
        metrics << "# TYPE pamguard_active_project_working_revision gauge\n";
        metrics << "pamguard_active_project_working_revision "
                << active_project.working_revision << "\n";
        metrics << "# HELP pamguard_active_project_authority_revision Project concurrency authority revision\n";
        metrics << "# TYPE pamguard_active_project_authority_revision gauge\n";
        metrics << "pamguard_active_project_authority_revision "
                << active_project.authority_revision << "\n";
        metrics << "# HELP pamguard_active_project_has_saved_baseline Whether the active project has a durable saved baseline\n";
        metrics << "# TYPE pamguard_active_project_has_saved_baseline gauge\n";
        metrics << "pamguard_active_project_has_saved_baseline "
                << (active_project.saved_revision ? 1 : 0)
                << "\n";
        metrics << "# HELP pamguard_active_project_saved_revision Durable saved project revision, 0 before first save\n";
        metrics << "# TYPE pamguard_active_project_saved_revision gauge\n";
        metrics << "pamguard_active_project_saved_revision "
                << active_project.saved_revision.value_or(0)
                << "\n";
        metrics << "# HELP pamguard_active_project_dirty Whether working content differs from the durable baseline\n";
        metrics << "# TYPE pamguard_active_project_dirty gauge\n";
        metrics << "pamguard_active_project_dirty "
                << (active_project.dirty ? 1 : 0) << "\n";
        metrics << "# HELP pamguard_active_project_runnable Whether project projection satisfies all start requirements\n";
        metrics << "# TYPE pamguard_active_project_runnable gauge\n";
        metrics << "pamguard_active_project_runnable "
                << (active_project.projection.runnable() ? 1 : 0)
                << "\n";
        metrics << "# HELP pamguard_active_project_runtime_prepared Whether the projected runtime is prepared at the working revision\n";
        metrics << "# TYPE pamguard_active_project_runtime_prepared gauge\n";
        metrics << "pamguard_active_project_runtime_prepared "
                << (active_runtime_prepared ? 1 : 0) << "\n";
        metrics << "# HELP pamguard_active_project_runtime_running Whether the active project runtime is running\n";
        metrics << "# TYPE pamguard_active_project_runtime_running gauge\n";
        metrics << "pamguard_active_project_runtime_running "
                << (active_runtime_running ? 1 : 0) << "\n";
        metrics << "# HELP pamguard_sessions Legacy compatibility analysis sessions\n";
        metrics << "# TYPE pamguard_sessions gauge\n";
        metrics << "pamguard_sessions " << manager.session_count() << "\n";
        metrics << "# HELP pamguard_max_sessions Configured session capacity, 0 means unlimited\n";
        metrics << "# TYPE pamguard_max_sessions gauge\n";
        metrics << "pamguard_max_sessions " << max_sessions << "\n";
        metrics << "# HELP pamguard_session_chunks_received PCM chunks received by session\n";
        metrics << "# TYPE pamguard_session_chunks_received counter\n";
        metrics << "# HELP pamguard_session_frames_received PCM frames received by session\n";
        metrics << "# TYPE pamguard_session_frames_received counter\n";
        metrics << "# HELP pamguard_session_bytes_received PCM bytes received by session\n";
        metrics << "# TYPE pamguard_session_bytes_received counter\n";
        metrics << "# HELP pamguard_session_detector_outputs Detector outputs produced by session\n";
        metrics << "# TYPE pamguard_session_detector_outputs counter\n";
        metrics << "# HELP pamguard_session_process_calls Audio processing calls by session\n";
        metrics << "# TYPE pamguard_session_process_calls counter\n";
        metrics << "# HELP pamguard_session_process_ms Audio processing milliseconds by session\n";
        metrics << "# TYPE pamguard_session_process_ms counter\n";
        metrics << "# HELP pamguard_session_last_process_ms Last audio processing duration in milliseconds by session\n";
        metrics << "# TYPE pamguard_session_last_process_ms gauge\n";
        metrics << "# HELP pamguard_session_sample_discontinuities PCM sample timeline gaps or overlaps by session\n";
        metrics << "# TYPE pamguard_session_sample_discontinuities counter\n";
        metrics << "# HELP pamguard_session_created_unix_ms Session creation wall-clock time in Unix milliseconds\n";
        metrics << "# TYPE pamguard_session_created_unix_ms gauge\n";
        metrics << "# HELP pamguard_session_last_receive_unix_ms Last PCM receive wall-clock time in Unix milliseconds, 0 if none\n";
        metrics << "# TYPE pamguard_session_last_receive_unix_ms gauge\n";
        metrics << "# HELP pamguard_session_age_ms Session age in milliseconds\n";
        metrics << "# TYPE pamguard_session_age_ms gauge\n";
        metrics << "# HELP pamguard_session_idle_ms Milliseconds since the last PCM chunk, 0 before first audio\n";
        metrics << "# TYPE pamguard_session_idle_ms gauge\n";
        metrics << "# HELP pamguard_session_has_received_audio 1 after a session has received at least one PCM chunk\n";
        metrics << "# TYPE pamguard_session_has_received_audio gauge\n";
        metrics << "# HELP pamguard_session_mean_process_ms Mean audio processing duration in milliseconds by session\n";
        metrics << "# TYPE pamguard_session_mean_process_ms gauge\n";
        const auto now_unix_ms = current_unix_ms();
        {
            std::lock_guard lock(configs_mutex);
            for (const auto& [session_id, stats] : runtime_stats) {
                const auto label = prometheus_label_escape(session_id);
                const bool has_received_audio = stats.last_receive_unix_ms > 0;
                const auto age_ms = non_negative_elapsed_ms(now_unix_ms, stats.created_unix_ms);
                const auto idle_ms = has_received_audio ? non_negative_elapsed_ms(now_unix_ms, stats.last_receive_unix_ms) : 0;
                const double mean_process_ms = stats.process_calls == 0
                    ? 0.0
                    : stats.total_process_ms / static_cast<double>(stats.process_calls);
                metrics << "pamguard_session_chunks_received{session=\"" << label << "\"} " << stats.chunks_received << "\n";
                metrics << "pamguard_session_frames_received{session=\"" << label << "\"} " << stats.frames_received << "\n";
                metrics << "pamguard_session_bytes_received{session=\"" << label << "\"} " << stats.bytes_received << "\n";
                metrics << "pamguard_session_detector_outputs{session=\"" << label << "\",type=\"spectrogram_frames\"} " << stats.spectrogram_frames << "\n";
                metrics << "pamguard_session_detector_outputs{session=\"" << label << "\",type=\"clicks\"} " << stats.clicks << "\n";
                metrics << "pamguard_session_detector_outputs{session=\"" << label << "\",type=\"click_features\"} " << stats.click_features << "\n";
                metrics << "pamguard_session_detector_outputs{session=\"" << label << "\",type=\"click_classifications\"} " << stats.click_classifications << "\n";
                metrics << "pamguard_session_detector_outputs{session=\"" << label << "\",type=\"click_trains\"} " << stats.click_trains << "\n";
                metrics << "pamguard_session_detector_outputs{session=\"" << label << "\",type=\"click_train_localisations\"} " << stats.click_train_localisations << "\n";
                metrics << "pamguard_session_detector_outputs{session=\"" << label << "\",type=\"click_train_bearings\"} " << stats.click_train_bearings << "\n";
                metrics << "pamguard_session_detector_outputs{session=\"" << label << "\",type=\"click_localisations\"} " << stats.click_localisations << "\n";
                metrics << "pamguard_session_detector_outputs{session=\"" << label << "\",type=\"click_bearings\"} " << stats.click_bearings << "\n";
                metrics << "pamguard_session_detector_outputs{session=\"" << label << "\",type=\"whistle_peaks\"} " << stats.whistle_peaks << "\n";
                metrics << "pamguard_session_detector_outputs{session=\"" << label << "\",type=\"whistle_regions\"} " << stats.whistle_regions << "\n";
                metrics << "pamguard_session_process_calls{session=\"" << label << "\"} " << stats.process_calls << "\n";
                metrics << "pamguard_session_process_ms{session=\"" << label << "\"} " << stats.total_process_ms << "\n";
                metrics << "pamguard_session_last_process_ms{session=\"" << label << "\"} " << stats.last_process_ms << "\n";
                metrics << "pamguard_session_sample_discontinuities{session=\"" << label << "\"} " << stats.sample_discontinuities << "\n";
                metrics << "pamguard_session_created_unix_ms{session=\"" << label << "\"} " << stats.created_unix_ms << "\n";
                metrics << "pamguard_session_last_receive_unix_ms{session=\"" << label << "\"} " << stats.last_receive_unix_ms << "\n";
                metrics << "pamguard_session_age_ms{session=\"" << label << "\"} " << age_ms << "\n";
                metrics << "pamguard_session_idle_ms{session=\"" << label << "\"} " << idle_ms << "\n";
                metrics << "pamguard_session_has_received_audio{session=\"" << label << "\"} " << (has_received_audio ? 1 : 0) << "\n";
                metrics << "pamguard_session_mean_process_ms{session=\"" << label << "\"} " << mean_process_ms << "\n";
            }
        }
        append_ingest_status_metrics(metrics, ingest_status_file);
        res.status = 200;
        res.set_content(metrics.str(), "text/plain; version=0.0.4");
    });

    server.Get("/ingest/status", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (ingest_status_file.empty()) {
            json_response(res, {
                {"configured", false},
                {"exists", false},
                {"error", "ingest status file not configured"},
            }, 404);
            return;
        }

        std::error_code exists_error;
        const bool exists = std::filesystem::is_regular_file(ingest_status_file, exists_error);
        if (exists_error || !exists) {
            json_response(res, {
                {"configured", true},
                {"exists", false},
                {"error", "ingest status file not found"},
            }, 404);
            return;
        }

        try {
            std::ifstream input(ingest_status_file);
            if (!input) {
                throw std::runtime_error("unable to open status file");
            }
            const auto status = json::parse(input);
            json_response(res, {
                {"configured", true},
                {"exists", true},
                {"status", status},
            });
        }
        catch (const std::exception& ex) {
            json_response(res, {
                {"configured", true},
                {"exists", true},
                {"error", std::string("failed to read ingest status file: ") + ex.what()},
            }, 500);
        }
    });

    server.Get("/sessions", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        const auto source_filter = req.has_param("sourceId") ? req.get_param_value("sourceId") : std::string();
        const auto owner_filter = req.has_param("ownerId") ? req.get_param_value("ownerId") : std::string();
        const auto tenant_filter = req.has_param("tenantId") ? req.get_param_value("tenantId") : std::string();
        json sessions = json::array();
        {
            std::lock_guard lock(configs_mutex);
            for (const auto& [session_id, config] : configs) {
                if (!source_filter.empty() && config.source_id != source_filter) {
                    continue;
                }
                if (!owner_filter.empty() && config.owner_id != owner_filter) {
                    continue;
                }
                if (!tenant_filter.empty() && config.tenant_id != tenant_filter) {
                    continue;
                }
                const auto stats = runtime_stats.find(session_id);
                sessions.push_back(config_to_json(config, stats == runtime_stats.end() ? nullptr : &stats->second));
            }
        }
        const auto returned_count = sessions.size();
        json_response(res, {
            {"sessions", std::move(sessions)},
            {"count", returned_count},
            {"totalSessions", manager.session_count()},
            {"sourceId", source_filter.empty() ? json(nullptr) : json(source_filter)},
            {"ownerId", owner_filter.empty() ? json(nullptr) : json(owner_filter)},
            {"tenantId", tenant_filter.empty() ? json(nullptr) : json(tenant_filter)},
        });
    });

    server.Post("/sessions", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        try {
            const auto original_body = json::parse(req.body);
            auto config = parse_config(original_body);
            if (require_session_metadata && (config.owner_id.empty() || config.tenant_id.empty())) {
                append_audit_event(audit_log_file, audit_mutex, {
                    {"event", "session_create_rejected"},
                    {"reason", "missing-session-metadata"},
                    {"sessionId", config.session_id},
                    {"sourceId", config.source_id},
                    {"ownerId", config.owner_id.empty() ? json(nullptr) : json(config.owner_id)},
                    {"tenantId", config.tenant_id.empty() ? json(nullptr) : json(config.tenant_id)},
                });
                json_response(res, {{"error", "ownerId and tenantId are required when PAMGUARD_REQUIRE_SESSION_METADATA is enabled"}}, 400);
                return;
            }
            const auto session_id = config.session_id;
            const auto source_id = config.source_id;
            const auto owner_id = config.owner_id;
            const auto tenant_id = config.tenant_id;
            bool session_created = false;
            bool persisted = false;
            try {
                std::lock_guard lock(configs_mutex);
                if (max_sessions > 0 && configs.size() >= max_sessions) {
                    append_audit_event(audit_log_file, audit_mutex, {
                        {"event", "session_create_rejected"},
                        {"reason", "capacity"},
                        {"sessionId", session_id},
                        {"sourceId", source_id},
                        {"ownerId", owner_id.empty() ? json(nullptr) : json(owner_id)},
                        {"tenantId", tenant_id.empty() ? json(nullptr) : json(tenant_id)},
                        {"maxSessions", max_sessions},
                    });
                    json_response(res, {{"error", "maximum session capacity reached"}, {"maxSessions", max_sessions}}, 429);
                    return;
                }
                if (configs.find(session_id) != configs.end()) {
                    throw std::runtime_error("session already exists in config registry: " + session_id);
                }
                manager.create_session(config);
                session_created = true;
                persist_session_config(session_config_dir, session_id, original_body);
                persisted = !session_config_dir.empty();
                configs.emplace(session_id, std::move(config));
                runtime_stats.emplace(session_id, make_runtime_stats());
            }
            catch (...) {
                if (session_created) {
                    manager.remove_session(session_id);
                }
                if (persisted) {
                    remove_persisted_session_config(session_config_dir, session_id);
                }
                throw;
            }
            append_audit_event(audit_log_file, audit_mutex, {
                {"event", "session_create"},
                {"sessionId", session_id},
                {"sourceId", source_id},
                {"ownerId", owner_id.empty() ? json(nullptr) : json(owner_id)},
                {"tenantId", tenant_id.empty() ? json(nullptr) : json(tenant_id)},
                {"persisted", persisted},
            });
            json_response(res, {
                {"sessionId", session_id},
                {"sourceId", source_id},
                {"ownerId", owner_id.empty() ? json(nullptr) : json(owner_id)},
                {"tenantId", tenant_id.empty() ? json(nullptr) : json(tenant_id)},
                {"created", true},
            }, 201);
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    server.Get(R"(/sessions/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        const auto session_id = req.matches[1].str();
        std::lock_guard lock(configs_mutex);
        const auto found = configs.find(session_id);
        if (found == configs.end()) {
            json_response(res, {{"error", "unknown session"}}, 404);
            return;
        }
        const auto stats = runtime_stats.find(session_id);
        auto body = config_to_json(found->second, stats == runtime_stats.end() ? nullptr : &stats->second);
        body["exists"] = true;
        json_response(res, body);
    });

    server.Delete(R"(/sessions/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        const auto session_id = req.matches[1].str();
        const bool removed = manager.remove_session(session_id);
        {
            std::lock_guard feed_lock(result_feed_mutex);
            result_feeds.erase(session_id);
        }
        std::string source_id;
        std::string owner_id;
        std::string tenant_id;
        {
            std::lock_guard lock(configs_mutex);
            const auto found = configs.find(session_id);
            if (found != configs.end()) {
                source_id = found->second.source_id;
                owner_id = found->second.owner_id;
                tenant_id = found->second.tenant_id;
            }
            configs.erase(session_id);
            runtime_stats.erase(session_id);
        }
        remove_persisted_session_config(session_config_dir, session_id);
        append_audit_event(audit_log_file, audit_mutex, {
            {"event", "session_delete"},
            {"sessionId", session_id},
            {"sourceId", source_id.empty() ? json(nullptr) : json(source_id)},
            {"ownerId", owner_id.empty() ? json(nullptr) : json(owner_id)},
            {"tenantId", tenant_id.empty() ? json(nullptr) : json(tenant_id)},
            {"removed", removed},
        });
        json_response(res, {{"sessionId", session_id}, {"removed", removed}}, removed ? 200 : 404);
    });

    server.Get(R"(/sessions/([^/]+)/archive)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        try {
            const auto session_id = req.matches[1].str();
            ArchiveQueryOptions query;
            query.limit = static_cast<std::size_t>(parse_uint64_param(req, "limit", 100));
            if (req.has_param("startSampleFrom")) {
                query.start_sample_from = parse_uint64_param(req, "startSampleFrom", 0);
                query.has_start_sample_from = true;
            }
            if (req.has_param("startSampleTo")) {
                query.start_sample_to = parse_uint64_param(req, "startSampleTo", 0);
                query.has_start_sample_to = true;
            }
            if (query.has_start_sample_from && query.has_start_sample_to && query.start_sample_from > query.start_sample_to) {
                json_response(res, {{"error", "startSampleFrom must be less than or equal to startSampleTo"}}, 400);
                return;
            }
            if (max_archive_query_records > 0 && (query.limit == 0 || query.limit > max_archive_query_records)) {
                json_response(res, {
                    {"error", "archive query limit exceeds maximum"},
                    {"maxArchiveQueryRecords", max_archive_query_records},
                }, 400);
                return;
            }
            json records;
            {
                std::lock_guard archive_lock(archive_mutex);
                records = read_result_archive(result_archive_dir, session_id, query);
            }
            json_response(res, {
                {"sessionId", session_id},
                {"records", records},
                {"count", records.size()},
                {"limit", query.limit},
                {"startSampleFrom", query.has_start_sample_from ? json(query.start_sample_from) : json(nullptr)},
                {"startSampleTo", query.has_start_sample_to ? json(query.start_sample_to) : json(nullptr)},
            });
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    server.Get(R"(/sessions/([^/]+)/archive/detections/summary)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        try {
            const auto session_id = req.matches[1].str();
            ArchiveQueryOptions query;
            query.limit = 0;
            if (req.has_param("startSampleFrom")) {
                query.start_sample_from = parse_uint64_param(req, "startSampleFrom", 0);
                query.has_start_sample_from = true;
            }
            if (req.has_param("startSampleTo")) {
                query.start_sample_to = parse_uint64_param(req, "startSampleTo", 0);
                query.has_start_sample_to = true;
            }
            if (req.has_param("overlapStartSample")) {
                query.overlap_start_sample = parse_uint64_param(req, "overlapStartSample", 0);
                query.has_overlap_start_sample = true;
            }
            if (req.has_param("overlapEndSample")) {
                query.overlap_end_sample = parse_uint64_param(req, "overlapEndSample", 0);
                query.has_overlap_end_sample = true;
            }
            query.source_id_filter = req.has_param("sourceId") ? req.get_param_value("sourceId") : std::string();
            query.owner_id_filter = req.has_param("ownerId") ? req.get_param_value("ownerId") : std::string();
            query.tenant_id_filter = req.has_param("tenantId") ? req.get_param_value("tenantId") : std::string();
            if (query.has_start_sample_from && query.has_start_sample_to && query.start_sample_from > query.start_sample_to) {
                json_response(res, {{"error", "startSampleFrom must be less than or equal to startSampleTo"}}, 400);
                return;
            }
            if (query.has_overlap_start_sample && query.has_overlap_end_sample && query.overlap_start_sample > query.overlap_end_sample) {
                json_response(res, {{"error", "overlapStartSample must be less than or equal to overlapEndSample"}}, 400);
                return;
            }
            const auto type_filter = req.has_param("type") ? req.get_param_value("type") : std::string();
            json summary;
            {
                std::lock_guard archive_lock(archive_mutex);
                summary = summarize_archive_detection_events(result_archive_dir, session_id, query, type_filter);
            }
            summary["type"] = type_filter.empty() ? json(nullptr) : json(type_filter);
            summary["sourceId"] = query.source_id_filter.empty() ? json(nullptr) : json(query.source_id_filter);
            summary["ownerId"] = query.owner_id_filter.empty() ? json(nullptr) : json(query.owner_id_filter);
            summary["tenantId"] = query.tenant_id_filter.empty() ? json(nullptr) : json(query.tenant_id_filter);
            summary["startSampleFrom"] = query.has_start_sample_from ? json(query.start_sample_from) : json(nullptr);
            summary["startSampleTo"] = query.has_start_sample_to ? json(query.start_sample_to) : json(nullptr);
            summary["overlapStartSample"] = query.has_overlap_start_sample ? json(query.overlap_start_sample) : json(nullptr);
            summary["overlapEndSample"] = query.has_overlap_end_sample ? json(query.overlap_end_sample) : json(nullptr);
            json_response(res, summary);
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    server.Get(R"(/sessions/([^/]+)/archive/detections\.csv)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        try {
            const auto session_id = req.matches[1].str();
            ArchiveQueryOptions query;
            query.limit = static_cast<std::size_t>(parse_uint64_param(req, "limit", 100));
            if (req.has_param("startSampleFrom")) {
                query.start_sample_from = parse_uint64_param(req, "startSampleFrom", 0);
                query.has_start_sample_from = true;
            }
            if (req.has_param("startSampleTo")) {
                query.start_sample_to = parse_uint64_param(req, "startSampleTo", 0);
                query.has_start_sample_to = true;
            }
            if (req.has_param("overlapStartSample")) {
                query.overlap_start_sample = parse_uint64_param(req, "overlapStartSample", 0);
                query.has_overlap_start_sample = true;
            }
            if (req.has_param("overlapEndSample")) {
                query.overlap_end_sample = parse_uint64_param(req, "overlapEndSample", 0);
                query.has_overlap_end_sample = true;
            }
            if (req.has_param("cursor")) {
                query.cursor = parse_uint64_param(req, "cursor", 0);
                query.has_cursor = true;
            }
            query.source_id_filter = req.has_param("sourceId") ? req.get_param_value("sourceId") : std::string();
            query.owner_id_filter = req.has_param("ownerId") ? req.get_param_value("ownerId") : std::string();
            query.tenant_id_filter = req.has_param("tenantId") ? req.get_param_value("tenantId") : std::string();
            if (query.has_start_sample_from && query.has_start_sample_to && query.start_sample_from > query.start_sample_to) {
                json_response(res, {{"error", "startSampleFrom must be less than or equal to startSampleTo"}}, 400);
                return;
            }
            if (query.has_overlap_start_sample && query.has_overlap_end_sample && query.overlap_start_sample > query.overlap_end_sample) {
                json_response(res, {{"error", "overlapStartSample must be less than or equal to overlapEndSample"}}, 400);
                return;
            }
            if (max_archive_query_records > 0 && (query.limit == 0 || query.limit > max_archive_query_records)) {
                json_response(res, {
                    {"error", "archive detection CSV query limit exceeds maximum"},
                    {"maxArchiveQueryRecords", max_archive_query_records},
                }, 400);
                return;
            }

            const auto type_filter = req.has_param("type") ? req.get_param_value("type") : std::string();
            ArchiveDetectionReadResult read_result;
            {
                std::lock_guard archive_lock(archive_mutex);
                read_result = read_archive_detection_events(result_archive_dir, session_id, query, type_filter);
            }
            res.status = 200;
            res.set_header("Content-Disposition", "attachment; filename=\"detections.csv\"");
            res.set_content(detection_events_to_csv(read_result.events), "text/csv");
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    server.Get(R"(/sessions/([^/]+)/archive/detections)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        try {
            const auto session_id = req.matches[1].str();
            ArchiveQueryOptions query;
            query.limit = static_cast<std::size_t>(parse_uint64_param(req, "limit", 100));
            if (req.has_param("startSampleFrom")) {
                query.start_sample_from = parse_uint64_param(req, "startSampleFrom", 0);
                query.has_start_sample_from = true;
            }
            if (req.has_param("startSampleTo")) {
                query.start_sample_to = parse_uint64_param(req, "startSampleTo", 0);
                query.has_start_sample_to = true;
            }
            if (req.has_param("overlapStartSample")) {
                query.overlap_start_sample = parse_uint64_param(req, "overlapStartSample", 0);
                query.has_overlap_start_sample = true;
            }
            if (req.has_param("overlapEndSample")) {
                query.overlap_end_sample = parse_uint64_param(req, "overlapEndSample", 0);
                query.has_overlap_end_sample = true;
            }
            if (req.has_param("cursor")) {
                query.cursor = parse_uint64_param(req, "cursor", 0);
                query.has_cursor = true;
            }
            query.source_id_filter = req.has_param("sourceId") ? req.get_param_value("sourceId") : std::string();
            query.owner_id_filter = req.has_param("ownerId") ? req.get_param_value("ownerId") : std::string();
            query.tenant_id_filter = req.has_param("tenantId") ? req.get_param_value("tenantId") : std::string();
            if (query.has_start_sample_from && query.has_start_sample_to && query.start_sample_from > query.start_sample_to) {
                json_response(res, {{"error", "startSampleFrom must be less than or equal to startSampleTo"}}, 400);
                return;
            }
            if (query.has_overlap_start_sample && query.has_overlap_end_sample && query.overlap_start_sample > query.overlap_end_sample) {
                json_response(res, {{"error", "overlapStartSample must be less than or equal to overlapEndSample"}}, 400);
                return;
            }
            if (max_archive_query_records > 0 && (query.limit == 0 || query.limit > max_archive_query_records)) {
                json_response(res, {
                    {"error", "archive detection query limit exceeds maximum"},
                    {"maxArchiveQueryRecords", max_archive_query_records},
                }, 400);
                return;
            }

            const auto type_filter = req.has_param("type") ? req.get_param_value("type") : std::string();
            ArchiveDetectionReadResult read_result;
            {
                std::lock_guard archive_lock(archive_mutex);
                read_result = read_archive_detection_events(result_archive_dir, session_id, query, type_filter);
            }
            json_response(res, {
                {"sessionId", session_id},
                {"events", read_result.events},
                {"count", read_result.events.size()},
                {"limit", query.limit},
                {"indexed", read_result.used_index},
                {"cursor", query.has_cursor ? json(query.cursor) : json(nullptr)},
                {"nextCursor", read_result.has_next_cursor ? json(read_result.next_cursor) : json(nullptr)},
                {"type", type_filter.empty() ? json(nullptr) : json(type_filter)},
                {"sourceId", query.source_id_filter.empty() ? json(nullptr) : json(query.source_id_filter)},
                {"ownerId", query.owner_id_filter.empty() ? json(nullptr) : json(query.owner_id_filter)},
                {"tenantId", query.tenant_id_filter.empty() ? json(nullptr) : json(query.tenant_id_filter)},
                {"startSampleFrom", query.has_start_sample_from ? json(query.start_sample_from) : json(nullptr)},
                {"startSampleTo", query.has_start_sample_to ? json(query.start_sample_to) : json(nullptr)},
                {"overlapStartSample", query.has_overlap_start_sample ? json(query.overlap_start_sample) : json(nullptr)},
                {"overlapEndSample", query.has_overlap_end_sample ? json(query.overlap_end_sample) : json(nullptr)},
            });
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    // Push stream of session results as NDJSON: one JSON body per line the
    // moment the engine produces it, with blank-line heartbeats. This is
    // what live viewers (the web UI waterfall) consume — no polling, so the
    // display advances at the audio chunk cadence. Each open stream holds
    // one HTTP worker thread; PAMGUARD_HTTP_THREADS bounds the total.
    server.Get(R"(/sessions/([^/]+)/results/stream)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        if (result_feed_depth == 0) {
            json_response(res, {{"error", "result feed is disabled (PAMGUARD_RESULT_FEED_DEPTH=0)"}}, 404);
            return;
        }
        const auto session_id = req.matches[1].str();
        {
            std::lock_guard lock(configs_mutex);
            if (configs.find(session_id) == configs.end()) {
                json_response(res, {{"error", "unknown session"}}, 404);
                return;
            }
        }
        auto cursor = std::make_shared<std::uint64_t>(parse_uint64_param(req, "sinceSeq", 0));
        res.set_header("Cache-Control", "no-cache");
        res.set_chunked_content_provider(
            "application/x-ndjson",
            [&, session_id, cursor](std::size_t, httplib::DataSink& sink) -> bool {
                std::vector<std::string> lines;
                {
                    std::unique_lock lock(result_feed_mutex);
                    result_feed_cv.wait_for(lock, std::chrono::seconds(1), [&] {
                        const auto found = result_feeds.find(session_id);
                        return found != result_feeds.end() && !found->second.recent.empty() &&
                            found->second.recent.back().first > *cursor;
                    });
                    const auto found = result_feeds.find(session_id);
                    if (found != result_feeds.end()) {
                        for (const auto& [sequence, result_body] : found->second.recent) {
                            if (sequence > *cursor) {
                                lines.push_back(result_body.dump());
                                *cursor = sequence;
                            }
                        }
                    }
                }
                {
                    // Session deleted: end the stream cleanly.
                    std::lock_guard lock(configs_mutex);
                    if (configs.find(session_id) == configs.end()) {
                        sink.done();
                        return false;
                    }
                }
                if (lines.empty()) {
                    // Heartbeat keeps the connection verifiably alive.
                    return sink.write("\n", 1);
                }
                for (const auto& line : lines) {
                    if (!sink.write(line.data(), line.size()) || !sink.write("\n", 1)) {
                        return false;
                    }
                }
                return true;
            });
    });

    server.Get(R"(/sessions/([^/]+)/results)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        const auto session_id = req.matches[1].str();
        {
            std::lock_guard lock(configs_mutex);
            if (configs.find(session_id) == configs.end()) {
                json_response(res, {{"error", "unknown session"}}, 404);
                return;
            }
        }
        const auto since = parse_uint64_param(req, "sinceSeq", 0);
        json results = json::array();
        std::uint64_t latest = 0;
        {
            std::lock_guard lock(result_feed_mutex);
            const auto found = result_feeds.find(session_id);
            if (found != result_feeds.end()) {
                latest = found->second.next_sequence - 1;
                for (const auto& [sequence, result_body] : found->second.recent) {
                    if (sequence > since) {
                        results.push_back(result_body);
                    }
                }
            }
        }
        json_response(res, {{"sessionId", session_id},
                            {"sinceSeq", since},
                            {"latestSeq", latest},
                            {"feedDepth", result_feed_depth},
                            {"count", results.size()},
                            {"results", std::move(results)}});
    });

    server.Get(R"(/sessions/([^/]+)/audio/index)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        if (audio_archive_dir.empty()) {
            json_response(res, {{"error", "audio archive is not enabled; set PAMGUARD_AUDIO_ARCHIVE_DIR"}}, 404);
            return;
        }
        const auto session_id = req.matches[1].str();
        std::vector<AudioIndexRecord> records;
        {
            std::lock_guard audio_lock(audio_archive_mutex);
            records = read_audio_archive_index(audio_archive_dir, session_id);
        }
        json items = json::array();
        std::uint64_t total_frames = 0;
        bool contiguous = true;
        std::uint64_t expected = 0;
        for (std::size_t i = 0; i < records.size(); ++i) {
            const auto& record = records[i];
            items.push_back({
                {"startSample", record.start_sample},
                {"frames", record.frames},
                {"timeMs", record.time_ms},
                {"byteOffset", record.byte_offset},
                {"byteLength", record.byte_length},
            });
            total_frames += record.frames;
            if (i > 0 && record.start_sample != expected) {
                contiguous = false;
            }
            expected = record.start_sample + record.frames;
        }
        json_response(res, {{"sessionId", session_id},
                            {"count", records.size()},
                            {"totalFrames", total_frames},
                            {"contiguous", contiguous},
                            {"records", std::move(items)}});
    });

    server.Post("/jobs", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        if (job_audio_dir.empty()) {
            json_response(res, {{"error", "offline jobs are not enabled; set PAMGUARD_JOB_AUDIO_DIR"}}, 404);
            return;
        }
        try {
            const auto body = json::parse(req.body);
            OfflineJob job;
            job.job_id = body.value("jobId", std::string());
            if (job.job_id.empty()) {
                job.job_id = std::to_string(now_unix_ms());
            }
            job.wav_file = body.value("wavFile", std::string());
            job.audio_session = body.value("audioSession", std::string());
            job.session_body = body.value("session", json::object());
            job.created_unix_ms = now_unix_ms();
            if (job.wav_file.empty() == job.audio_session.empty()) {
                throw std::invalid_argument("exactly one of wavFile or audioSession is required");
            }
            // Fail fast on unreadable sources so a bad job is a 400 now, not
            // a failed record later.
            if (!job.wav_file.empty()) {
                const auto wav_path = resolve_job_wav(job_audio_dir, job.wav_file);
                if (!std::filesystem::exists(wav_path)) {
                    throw std::invalid_argument("wavFile does not exist under the job audio directory");
                }
            }
            else {
                if (audio_archive_dir.empty()) {
                    throw std::invalid_argument("audioSession replay needs PAMGUARD_AUDIO_ARCHIVE_DIR");
                }
                if (!std::filesystem::exists(audio_archive_index_path(audio_archive_dir, job.audio_session))) {
                    throw std::invalid_argument("no archived audio for that session");
                }
            }
            {
                std::lock_guard lock(job_state.mutex);
                if (job_state.jobs.count(job.job_id) != 0) {
                    json_response(res, {{"error", "job already exists"}, {"jobId", job.job_id}}, 409);
                    return;
                }
                job_state.jobs.emplace(job.job_id, job);
                job_state.pending.push_back(job.job_id);
            }
            job_state.cv.notify_one();
            json_response(res, job_to_json(job), 201);
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    server.Get("/jobs", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        json jobs = json::array();
        std::size_t queued = 0;
        std::size_t running = 0;
        {
            std::lock_guard lock(job_state.mutex);
            for (const auto& [_, job] : job_state.jobs) {
                jobs.push_back(job_to_json(job));
                queued += job.state == "queued" ? 1 : 0;
                running += job.state == "running" ? 1 : 0;
            }
        }
        json_response(res, {{"enabled", !job_audio_dir.empty()},
                            {"count", jobs.size()},
                            {"queued", queued},
                            {"running", running},
                            {"jobs", std::move(jobs)}});
    });

    server.Get(R"(/jobs/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        const auto job_id = req.matches[1].str();
        std::lock_guard lock(job_state.mutex);
        const auto found = job_state.jobs.find(job_id);
        if (found == job_state.jobs.end()) {
            json_response(res, {{"error", "unknown job"}}, 404);
            return;
        }
        json_response(res, job_to_json(found->second));
    });

    server.Delete(R"(/jobs/([^/]+))", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        const auto job_id = req.matches[1].str();
        std::lock_guard lock(job_state.mutex);
        const auto found = job_state.jobs.find(job_id);
        if (found == job_state.jobs.end()) {
            json_response(res, {{"error", "unknown job"}}, 404);
            return;
        }
        if (found->second.state == "queued") {
            found->second.state = "cancelled";
            found->second.finished_unix_ms = now_unix_ms();
            json_response(res, {{"jobId", job_id}, {"state", "cancelled"}});
            return;
        }
        if (found->second.state == "running") {
            // Cancellation lands between chunks, so the state flips when the
            // worker notices; the response reports the request, not the flip.
            found->second.cancel_requested = true;
            json_response(res, {{"jobId", job_id}, {"state", "running"}, {"cancelRequested", true}});
            return;
        }
        job_state.jobs.erase(found);
        json_response(res, {{"jobId", job_id}, {"removed", true}});
    });

    server.Post(R"(/sessions/([^/]+)/pcm-f32le)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        try {
            const auto session_id = req.matches[1].str();
            pamguard::core::AnalysisConfig config;
            {
                std::lock_guard lock(configs_mutex);
                const auto found = configs.find(session_id);
                if (found == configs.end()) {
                    json_response(res, {{"error", "unknown session"}}, 404);
                    return;
                }
                config = found->second;
            }

            if (max_pcm_body_bytes > 0 && req.body.size() > max_pcm_body_bytes) {
                json_response(res, {{"error", "PCM body exceeds maximum size"}, {"maxPcmBodyBytes", max_pcm_body_bytes}}, 413);
                return;
            }

            if (req.body.empty()) {
                throw std::invalid_argument("PCM body must not be empty");
            }
            const auto bytes_per_frame = config.channel_count * sizeof(float);
            if (bytes_per_frame == 0 || req.body.size() % bytes_per_frame != 0) {
                throw std::invalid_argument("PCM body size must be a whole number of interleaved f32le frames");
            }
            const auto frame_count = req.body.size() / bytes_per_frame;
            const auto start_sample = parse_uint64_param(req, "startSample", 0);
            if (frame_count > std::numeric_limits<std::uint64_t>::max() - start_sample) {
                throw std::invalid_argument("startSample plus inputFrames exceeds uint64 range");
            }
            const auto next_expected_start_sample = start_sample + frame_count;
            const auto time_ms = req.has_param("timeMs")
                ? static_cast<std::int64_t>(std::stoll(req.get_param_value("timeMs")))
                : static_cast<std::int64_t>(static_cast<double>(start_sample) * 1000.0 / config.sample_rate_hz);

            pamguard::core::AudioChunk chunk;
            chunk.start_sample = start_sample;
            chunk.time_unix_ms = time_ms;
            chunk.sample_rate_hz = config.sample_rate_hz;
            chunk.channel_count = config.channel_count;
            // Optional per-chunk array attitude. All three angles travel
            // together so a partial declaration cannot silently mix a new
            // heading with a stale pitch or roll.
            const bool has_heading = req.has_param("headingDegrees");
            const bool has_pitch = req.has_param("pitchDegrees");
            const bool has_roll = req.has_param("rollDegrees");
            if (has_heading || has_pitch || has_roll) {
                if (!(has_heading && has_pitch && has_roll)) {
                    throw std::invalid_argument("headingDegrees, pitchDegrees, and rollDegrees must be supplied together");
                }
                chunk.orientation_declared = true;
                chunk.orientation_heading_degrees = std::stod(req.get_param_value("headingDegrees"));
                chunk.orientation_pitch_degrees = std::stod(req.get_param_value("pitchDegrees"));
                chunk.orientation_roll_degrees = std::stod(req.get_param_value("rollDegrees"));
                if (!std::isfinite(chunk.orientation_heading_degrees) ||
                    !std::isfinite(chunk.orientation_pitch_degrees) ||
                    !std::isfinite(chunk.orientation_roll_degrees)) {
                    throw std::invalid_argument("headingDegrees, pitchDegrees, and rollDegrees must be finite");
                }
            }
            chunk.interleaved_pcm.resize(frame_count * config.channel_count);

            const auto* bytes = reinterpret_cast<const unsigned char*>(req.body.data());
            for (std::size_t frame = 0; frame < frame_count; ++frame) {
                for (std::size_t channel = 0; channel < config.channel_count; ++channel) {
                    const auto offset = (frame * config.channel_count + channel) * sizeof(float);
                    chunk.interleaved_pcm[frame * config.channel_count + channel] = read_float_le(bytes + offset);
                }
            }

            const auto process_started = std::chrono::steady_clock::now();
            const auto result = manager.process_audio(session_id, chunk);
            const auto process_finished = std::chrono::steady_clock::now();
            const auto process_ms = std::chrono::duration<double, std::milli>(process_finished - process_started).count();
            std::uint64_t expected_start_sample = start_sample;
            std::int64_t sample_delta = 0;
            std::string sample_continuity = "first";
            {
                std::lock_guard lock(configs_mutex);
                auto& stats = runtime_stats[session_id];
                if (stats.has_expected_start_sample) {
                    expected_start_sample = stats.expected_start_sample;
                    sample_delta = saturated_sample_delta(start_sample, expected_start_sample);
                    if (sample_delta == 0) {
                        sample_continuity = "contiguous";
                    }
                    else if (sample_delta > 0) {
                        sample_continuity = "gap";
                        stats.sample_discontinuities += 1;
                    }
                    else {
                        sample_continuity = "overlap";
                        stats.sample_discontinuities += 1;
                    }
                }
                stats.has_expected_start_sample = true;
                stats.expected_start_sample = next_expected_start_sample;
                stats.last_sample_delta = sample_delta;
                stats.last_sample_continuity = sample_continuity;
                stats.last_receive_unix_ms = current_unix_ms();
                stats.chunks_received += 1;
                stats.frames_received += frame_count;
                stats.bytes_received += req.body.size();
                stats.last_start_sample = start_sample;
                stats.last_time_ms = time_ms;
                stats.spectrogram_frames += result.spectrogram_frames.size();
                stats.clicks += result.clicks.size();
                stats.click_features += result.click_features.size();
                stats.click_classifications += result.click_classifications.size();
                stats.click_trains += result.click_trains.size();
                stats.click_train_localisations += result.click_train_localisations.size();
                stats.click_train_bearings += result.click_train_bearings.size();
                stats.click_localisations += result.click_localisations.size();
                stats.click_bearings += result.click_bearings.size();
                stats.whistle_peaks += result.whistle_peaks.size();
                stats.whistle_regions += result.whistle_regions.size();
                stats.process_calls += 1;
                stats.total_process_ms += process_ms;
                stats.last_process_ms = process_ms;
            }
            ResultJsonOptions result_options;
            result_options.sample_rate_hz = config.sample_rate_hz;
            result_options.echo_detection_running = config.detector.click_echo_enabled;
            result_options.fft_length = config.detector.fft.fft_length;
            result_options.speed_of_sound_mps = config.array.speed_of_sound_mps;
            result_options.include_spectrogram = parse_bool_param(req, "includeSpectrogram", false);
            result_options.include_spectrogram_complex = parse_bool_param(req, "includeSpectrogramComplex", false);
            result_options.include_click_waveforms = parse_bool_param(req, "includeClickWaveforms", false);
            result_options.include_click_spectra = parse_bool_param(req, "includeClickSpectra", false);
            if (req.has_param("spectrogramMaxBins")) {
                result_options.spectrogram_max_bins = static_cast<std::size_t>(std::stoull(req.get_param_value("spectrogramMaxBins")));
            }
            if (req.has_param("spectrogramBinStride")) {
                result_options.spectrogram_bin_stride = static_cast<std::size_t>(std::stoull(req.get_param_value("spectrogramBinStride")));
            }
            auto body = result_to_json(result, result_options);
            body["sessionId"] = session_id;
            body["sourceId"] = config.source_id;
            body["ownerId"] = config.owner_id.empty() ? json(nullptr) : json(config.owner_id);
            body["tenantId"] = config.tenant_id.empty() ? json(nullptr) : json(config.tenant_id);
            body["inputFrames"] = frame_count;
            body["startSample"] = start_sample;
            body["expectedStartSample"] = expected_start_sample;
            body["nextExpectedStartSample"] = next_expected_start_sample;
            body["sampleDelta"] = sample_delta;
            body["sampleContinuity"] = sample_continuity;
            if (!result_archive_dir.empty()) {
                ResultJsonOptions archive_options;
                archive_options.sample_rate_hz = config.sample_rate_hz;
                archive_options.echo_detection_running = config.detector.click_echo_enabled;
                archive_options.fft_length = config.detector.fft.fft_length;
                archive_options.speed_of_sound_mps = config.array.speed_of_sound_mps;
                auto archive_body = result_to_json(result, archive_options);
                archive_body["sessionId"] = session_id;
                archive_body["sourceId"] = config.source_id;
                archive_body["ownerId"] = config.owner_id.empty() ? json(nullptr) : json(config.owner_id);
                archive_body["tenantId"] = config.tenant_id.empty() ? json(nullptr) : json(config.tenant_id);
                archive_body["inputFrames"] = frame_count;
                archive_body["startSample"] = start_sample;
                archive_body["expectedStartSample"] = expected_start_sample;
                archive_body["nextExpectedStartSample"] = next_expected_start_sample;
                archive_body["sampleDelta"] = sample_delta;
                archive_body["sampleContinuity"] = sample_continuity;
                archive_body["timeMs"] = time_ms;
                std::lock_guard archive_lock(archive_mutex);
                append_result_archive(result_archive_dir, session_id, archive_body);
                append_detection_event_archive(result_archive_dir, session_id, archive_body);
            }
            if (!audio_archive_dir.empty()) {
                std::lock_guard audio_lock(audio_archive_mutex);
                append_audio_archive(audio_archive_dir, session_id, req.body, start_sample, frame_count, time_ms,
                                     config.sample_rate_hz, config.channel_count);
            }
            if (result_feed_depth > 0) {
                {
                    std::lock_guard feed_lock(result_feed_mutex);
                    auto& feed = result_feeds[session_id];
                    const auto sequence = feed.next_sequence++;
                    json feed_body = body;
                    feed_body["seq"] = sequence;
                    body["seq"] = sequence;
                    feed.recent.emplace_back(sequence, std::move(feed_body));
                    while (feed.recent.size() > result_feed_depth) {
                        feed.recent.pop_front();
                    }
                }
                // Wake any streaming viewers blocked on this session.
                result_feed_cv.notify_all();
            }
            json_response(res, body);
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    server.Post(R"(/sessions/([^/]+)/flush)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
            return;
        }
        if (!require_legacy_analysis_compatibility(res)) {
            return;
        }
        try {
            const auto session_id = req.matches[1].str();
            pamguard::core::AnalysisConfig config;
            {
                std::lock_guard lock(configs_mutex);
                const auto found = configs.find(session_id);
                if (found == configs.end()) {
                    json_response(res, {{"error", "unknown session"}}, 404);
                    return;
                }
                config = found->second;
            }

            const auto result = manager.flush_session(session_id);
            {
                std::lock_guard lock(configs_mutex);
                auto& stats = runtime_stats[session_id];
                stats.click_trains += result.click_trains.size();
                stats.whistle_regions += result.whistle_regions.size();
            }
            ResultJsonOptions result_options;
            result_options.sample_rate_hz = config.sample_rate_hz;
            result_options.echo_detection_running = config.detector.click_echo_enabled;
            result_options.fft_length = config.detector.fft.fft_length;
            result_options.speed_of_sound_mps = config.array.speed_of_sound_mps;
            auto body = result_to_json(result, result_options);
            body["sessionId"] = session_id;
            body["sourceId"] = config.source_id;
            body["ownerId"] = config.owner_id.empty() ? json(nullptr) : json(config.owner_id);
            body["tenantId"] = config.tenant_id.empty() ? json(nullptr) : json(config.tenant_id);
            body["flushed"] = true;
            if (!result_archive_dir.empty()) {
                auto archive_body = body;
                std::lock_guard archive_lock(archive_mutex);
                append_result_archive(result_archive_dir, session_id, archive_body);
                append_detection_event_archive(result_archive_dir, session_id, archive_body);
            }
            append_audit_event(audit_log_file, audit_mutex, {
                {"event", "session_flush"},
                {"sessionId", session_id},
                {"sourceId", config.source_id},
                {"ownerId", config.owner_id.empty() ? json(nullptr) : json(config.owner_id)},
                {"tenantId", config.tenant_id.empty() ? json(nullptr) : json(config.tenant_id)},
            });
            json_response(res, body);
        }
        catch (const std::exception& error) {
            json_response(res, {{"error", error.what()}}, 400);
        }
    });

    std::cout << "PAMGuard C++ engine service listening on http://0.0.0.0:" << port << "\n";
    if (legacy_model_compat && !session_config_dir.empty()) {
        std::cout << "Session config persistence enabled at " << session_config_dir.string() << "\n";
    }
    if (legacy_model_compat && !result_archive_dir.empty()) {
        std::cout << "Result archiving enabled at " << result_archive_dir.string() << "\n";
    }
    if (!web_ui_file.empty()) {
        std::cout << "Web UI serving enabled from " << web_ui_file.string() << "\n";
    }
    if (!web_asset_root.empty()) {
        std::cout << "Web asset serving enabled at /assets/ from "
                  << web_asset_root.string() << "\n";
    }
    if (!openapi_file.empty()) {
        std::cout << "OpenAPI serving enabled from " << openapi_file.string() << "\n";
    }
    if (!audit_log_file.empty()) {
        std::cout << "Audit logging enabled at " << audit_log_file.string() << "\n";
    }
    if (!api_key.empty()) {
        std::cout << "API key protection enabled\n";
    }
    if (max_pcm_body_bytes > 0) {
        std::cout << "PCM request body limit enabled at " << max_pcm_body_bytes << " bytes\n";
    }
    std::vector<std::thread> job_worker_threads;
    if (legacy_model_compat && !job_audio_dir.empty()) {
        for (std::size_t worker = 0; worker < job_workers; ++worker) {
            job_worker_threads.emplace_back([&job_state, &job_audio_dir, &result_archive_dir, &archive_mutex,
                                             &audio_archive_dir, &audio_archive_mutex] {
                for (;;) {
                    std::string job_id;
                    {
                        std::unique_lock lock(job_state.mutex);
                        job_state.cv.wait(lock, [&] { return job_state.shutting_down || !job_state.pending.empty(); });
                        if (job_state.shutting_down) {
                            return;
                        }
                        job_id = job_state.pending.front();
                        job_state.pending.pop_front();
                        auto found = job_state.jobs.find(job_id);
                        if (found == job_state.jobs.end() || found->second.state != "queued") {
                            continue;
                        }
                        found->second.state = "running";
                        found->second.started_unix_ms = now_unix_ms();
                    }

                    std::string failure;
                    OfflineJob snapshot;
                    {
                        std::lock_guard lock(job_state.mutex);
                        snapshot = job_state.jobs[job_id];
                    }
                    try {
                        pamguard::io::WavData wav;
                        std::vector<AudioIndexRecord> replay_index;
                        if (!snapshot.wav_file.empty()) {
                            const auto wav_path = resolve_job_wav(job_audio_dir, snapshot.wav_file);
                            pamguard::io::WavReader reader;
                            wav = reader.read_all(wav_path);
                        }
                        else {
                            // Replay: decode the archived f32le back into the
                            // same doubles the original session analysed.
                            {
                                std::lock_guard audio_lock(audio_archive_mutex);
                                replay_index = read_audio_archive_index(audio_archive_dir, snapshot.audio_session);
                            }
                            if (replay_index.empty() || replay_index.front().channel_count == 0 ||
                                replay_index.front().sample_rate_hz == 0) {
                                throw std::runtime_error("archived audio index is empty or missing acquisition facts");
                            }
                            wav.sample_rate_hz = replay_index.front().sample_rate_hz;
                            wav.channel_count = static_cast<std::uint16_t>(replay_index.front().channel_count);
                        }
                        if (wav.sample_rate_hz == 0 || wav.channel_count == 0) {
                            throw std::runtime_error("audio source has no readable audio");
                        }

                        json session_body = snapshot.session_body;
                        // Acquisition facts come from the file unless the job
                        // pins them; a pinned mismatch is an error, not a
                        // silent resample.
                        if (!session_body.contains("sampleRateHz")) {
                            session_body["sampleRateHz"] = wav.sample_rate_hz;
                        }
                        if (!session_body.contains("channelCount")) {
                            session_body["channelCount"] = wav.channel_count;
                        }
                        session_body["sessionId"] = std::string("job-") + job_id;
                        auto config = parse_config(session_body);
                        if (config.sample_rate_hz != wav.sample_rate_hz ||
                            config.channel_count != wav.channel_count) {
                            throw std::runtime_error("session config sample rate/channel count do not match the WAV file");
                        }

                        std::uint64_t total_frames = wav.interleaved_pcm.size() / wav.channel_count;
                        if (!replay_index.empty()) {
                            total_frames = 0;
                            for (const auto& record : replay_index) {
                                total_frames += record.frames;
                            }
                        }
                        {
                            std::lock_guard lock(job_state.mutex);
                            job_state.jobs[job_id].total_frames = total_frames;
                        }

                        pamguard::core::AnalysisSession session(config);
                        const std::string archive_session_id = std::string("job-") + job_id;
                        const std::uint64_t chunk_frames = wav.sample_rate_hz; // one second
                        std::uint64_t frame_cursor = 0;
                        std::size_t replay_cursor = 0;
                        std::ifstream replay_data;
                        if (!replay_index.empty()) {
                            replay_data.open(audio_archive_data_path(audio_archive_dir, snapshot.audio_session),
                                             std::ios::binary);
                            if (!replay_data) {
                                throw std::runtime_error("archived audio data file is missing");
                            }
                        }
                        bool cancelled = false;
                        while (replay_index.empty() ? frame_cursor < total_frames
                                                    : replay_cursor < replay_index.size()) {
                            {
                                std::lock_guard lock(job_state.mutex);
                                if (job_state.jobs[job_id].cancel_requested || job_state.shutting_down) {
                                    cancelled = true;
                                    break;
                                }
                            }
                            pamguard::core::AudioChunk chunk;
                            std::uint64_t frames = 0;
                            if (replay_index.empty()) {
                                frames = std::min<std::uint64_t>(chunk_frames, total_frames - frame_cursor);
                                chunk.start_sample = frame_cursor;
                                chunk.sample_rate_hz = wav.sample_rate_hz;
                                chunk.channel_count = wav.channel_count;
                                chunk.time_unix_ms = static_cast<std::int64_t>(frame_cursor * 1000ULL / wav.sample_rate_hz);
                                const auto begin = frame_cursor * wav.channel_count;
                                chunk.interleaved_pcm.assign(
                                    wav.interleaved_pcm.begin() + static_cast<std::ptrdiff_t>(begin),
                                    wav.interleaved_pcm.begin() + static_cast<std::ptrdiff_t>(begin + frames * wav.channel_count));
                            }
                            else {
                                // Replay preserves the ORIGINAL chunk
                                // boundaries, start samples, and timestamps —
                                // the same bytes through the same cuts.
                                const auto& record = replay_index[replay_cursor];
                                frames = record.frames;
                                chunk.start_sample = record.start_sample;
                                chunk.sample_rate_hz = wav.sample_rate_hz;
                                chunk.channel_count = wav.channel_count;
                                chunk.time_unix_ms = record.time_ms;
                                std::vector<char> bytes(record.byte_length);
                                replay_data.seekg(static_cast<std::streamoff>(record.byte_offset));
                                replay_data.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                                if (replay_data.gcount() != static_cast<std::streamsize>(bytes.size())) {
                                    throw std::runtime_error("archived audio data file is truncated");
                                }
                                const auto sample_count = record.byte_length / sizeof(float);
                                chunk.interleaved_pcm.resize(sample_count);
                                const auto* raw = reinterpret_cast<const unsigned char*>(bytes.data());
                                for (std::size_t i = 0; i < sample_count; ++i) {
                                    chunk.interleaved_pcm[i] = read_float_le(raw + i * sizeof(float));
                                }
                                ++replay_cursor;
                            }
                            const auto result = session.process(chunk);

                            if (!result_archive_dir.empty()) {
                                ResultJsonOptions archive_options;
                                archive_options.sample_rate_hz = config.sample_rate_hz;
                                archive_options.fft_length = config.detector.fft.fft_length;
                                archive_options.speed_of_sound_mps = config.array.speed_of_sound_mps;
                                archive_options.echo_detection_running = config.detector.click_echo_enabled;
                                auto archive_body = result_to_json(result, archive_options);
                                archive_body["sessionId"] = archive_session_id;
                                archive_body["sourceId"] = std::string("job:") + snapshot.wav_file;
                                archive_body["inputFrames"] = frames;
                                archive_body["startSample"] = frame_cursor;
                                archive_body["timeMs"] = chunk.time_unix_ms;
                                std::lock_guard archive_lock(archive_mutex);
                                append_result_archive(result_archive_dir, archive_session_id, archive_body);
                                append_detection_event_archive(result_archive_dir, archive_session_id, archive_body);
                            }

                            std::lock_guard lock(job_state.mutex);
                            auto& live = job_state.jobs[job_id];
                            live.processed_frames = frame_cursor + frames;
                            live.chunks += 1;
                            live.clicks += result.clicks.size();
                            live.click_trains += result.click_trains.size();
                            live.whistle_regions += result.whistle_regions.size();
                            frame_cursor += frames;
                        }

                        if (!cancelled) {
                            const auto flushed = session.flush();
                            if (!result_archive_dir.empty()) {
                                ResultJsonOptions archive_options;
                                archive_options.sample_rate_hz = config.sample_rate_hz;
                                archive_options.fft_length = config.detector.fft.fft_length;
                                archive_options.speed_of_sound_mps = config.array.speed_of_sound_mps;
                                archive_options.echo_detection_running = config.detector.click_echo_enabled;
                                auto archive_body = result_to_json(flushed, archive_options);
                                archive_body["sessionId"] = archive_session_id;
                                archive_body["sourceId"] = std::string("job:") + snapshot.wav_file;
                                archive_body["flush"] = true;
                                std::lock_guard archive_lock(archive_mutex);
                                append_result_archive(result_archive_dir, archive_session_id, archive_body);
                                append_detection_event_archive(result_archive_dir, archive_session_id, archive_body);
                            }
                            std::lock_guard lock(job_state.mutex);
                            auto& live = job_state.jobs[job_id];
                            live.clicks += flushed.clicks.size();
                            live.click_trains += flushed.click_trains.size();
                            live.whistle_regions += flushed.whistle_regions.size();
                        }

                        std::lock_guard lock(job_state.mutex);
                        auto& live = job_state.jobs[job_id];
                        live.state = cancelled ? "cancelled" : "completed";
                        live.finished_unix_ms = now_unix_ms();
                    }
                    catch (const std::exception& error) {
                        std::lock_guard lock(job_state.mutex);
                        auto& live = job_state.jobs[job_id];
                        live.state = "failed";
                        live.error = error.what();
                        live.finished_unix_ms = now_unix_ms();
                    }
                }
            });
        }
        std::cout << "Offline job queue enabled: audio dir " << job_audio_dir.string()
                  << ", " << job_workers << " worker(s)\n";
    }

    if (http_threads > 0) {
        std::cout << "HTTP worker thread pool set to " << http_threads << "\n";
    }
    const auto stop_job_workers = [&job_state, &job_worker_threads] {
        {
            std::lock_guard lock(job_state.mutex);
            job_state.shutting_down = true;
        }
        job_state.cv.notify_all();
        for (auto& thread : job_worker_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    };
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Failed to listen on port " << port << "\n";
        stop_job_workers();
        return 1;
    }
    stop_job_workers();
    return 0;
}
