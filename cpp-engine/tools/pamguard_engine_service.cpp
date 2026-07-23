#include <cstdint>
#include <cstring>
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
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <httplib.h>
#include <json.hpp>

#include "pamguard/core/SessionManager.h"
#include "pamguard/io/WavReader.h"
#include "pamguard/detectors/CtSpectrumTemplates.h"
#include "pamguard/dsp/WindowFunction.h"

using json = nlohmann::json;

namespace {

constexpr std::size_t kMaxServiceChannelCount = 1024;
constexpr int kResultSchemaVersion = 24;

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

std::filesystem::path openapi_file_from_environment() {
    const char* raw = std::getenv("PAMGUARD_OPENAPI_FILE");
    if (raw == nullptr || std::string(raw).empty()) {
        return {};
    }
    return std::filesystem::path(raw);
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
        if (!std::isfinite(config.detector.click.short_filter) || !std::isfinite(config.detector.click.long_filter) ||
            config.detector.click.short_filter < 0.0 || config.detector.click.short_filter > 1.0 ||
            config.detector.click.long_filter < 0.0 || config.detector.click.long_filter > 1.0) {
            throw std::invalid_argument("click.shortFilter and click.longFilter must be in the range 0..1");
        }
        if (config.detector.click.max_length == 0) {
            throw std::invalid_argument("click.maxLength must be positive");
        }
        if (config.detector.click_localisation_enabled && bitmap_bit_count(config.detector.click.channel_bitmap) < 2) {
            throw std::invalid_argument("click.localisation requires at least two click detector channels");
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
            if (!std::isfinite(config.detector.click_train.max_ici_seconds) || config.detector.click_train.max_ici_seconds <= 0.0 ||
                config.detector.click_train.min_clicks == 0) {
                throw std::invalid_argument("click.train.maxIciSeconds and click.train.minClicks must be positive");
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
    else {
        throw std::invalid_argument("filter type must be none, butterworth, or chebyshev");
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
    if (params.order <= 0 || params.order > 32) {
        throw std::invalid_argument("filter order must be between 1 and 32");
    }
    if (!std::isfinite(params.high_pass_freq_hz) || !std::isfinite(params.low_pass_freq_hz) ||
        params.high_pass_freq_hz < 0.0F || params.low_pass_freq_hz < 0.0F) {
        throw std::invalid_argument("filter frequencies must be non-negative and finite");
    }
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
        const auto ltsa = body.value("ltsa", json::object());
        config.detector.ltsa.enabled = ltsa.value("enabled", false);
        if (config.detector.ltsa.enabled) {
            config.detector.ltsa.interval_seconds = ltsa.value("intervalSeconds", config.detector.ltsa.interval_seconds);
            if (config.detector.ltsa.interval_seconds <= 0) {
                throw std::invalid_argument("ltsa.intervalSeconds must be positive");
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

    const auto click = body.value("click", json::object());
    config.detector.click_detector_enabled = click.value("enabled", false);
    config.detector.click_localisation_enabled = click.value("localisation", false);
    if (config.detector.click_detector_enabled) {
        config.detector.click.channel_bitmap = click.value("channelBitmap", channel_bitmap(config.channel_count));
        config.detector.click.trigger_bitmap = click.value("triggerBitmap", config.detector.click.channel_bitmap);
        config.detector.click.min_trigger_channels = click.value("minTriggerChannels", config.detector.click.min_trigger_channels);
        config.detector.click.threshold_db = click.value("thresholdDb", config.detector.click.threshold_db);
        config.detector.click.long_filter = click.value("longFilter", config.detector.click.long_filter);
        config.detector.click.short_filter = click.value("shortFilter", config.detector.click.short_filter);
        config.detector.click.pre_sample = click.value("preSample", config.detector.click.pre_sample);
        config.detector.click.post_sample = click.value("postSample", config.detector.click.post_sample);
        config.detector.click.min_sep = click.value("minSep", config.detector.click.min_sep);
        config.detector.click.max_length = click.value("maxLength", config.detector.click.max_length);

        if (click.contains("preFilter")) {
            config.detector.click.pre_filter = parse_iir_filter(click.at("preFilter"));
        }
        if (click.contains("triggerFilter")) {
            config.detector.click.trigger_filter = parse_iir_filter(click.at("triggerFilter"));
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

        const auto basic_classifier = click.value("basicClassifier", json::object());
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

        const auto click_train = click.value("train", json::object());
        config.detector.click_train_tracker_enabled = click_train.value("enabled", false);
        if (config.detector.click_train_tracker_enabled) {
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
        config.detector.whistle_region.min_pixels = whistle.value("minPixels", config.detector.whistle_region.min_pixels);
        config.detector.whistle_region.min_length = whistle.value("minLength", config.detector.whistle_region.min_length);
        config.detector.whistle_region.connect_type = whistle.value("connectType", config.detector.whistle_region.connect_type);
        config.detector.whistle_region.keep_shape_stubs = whistle.value("keepShapeStubs", config.detector.whistle_region.keep_shape_stubs);
        config.detector.whistle_region.fragmentation_method = whistle.value("fragmentationMethod", config.detector.whistle_region.fragmentation_method);
        config.detector.whistle_region.max_cross_length = whistle.value("maxCrossLength", config.detector.whistle_region.max_cross_length);
        config.detector.whistle_region.reject_first_quarter_second = whistle.value("rejectFirstQuarterSecond", config.detector.whistle_region.reject_first_quarter_second);
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
        json item = {
            {"startSample", click.start_sample},
            {"durationSamples", click.duration_samples},
            {"timeMs", click.time_unix_ms},
            {"triggerBitmap", click.trigger_bitmap},
            {"signalExcessDb", click.signal_excess_db},
            {"waveformChannels", click.waveform.size()},
            {"waveformSamples", click.waveform.empty() ? 0 : click.waveform.front().size()},
        };
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
        {"shortFilter", config.detector.click.short_filter},
        {"preSample", config.detector.click.pre_sample},
        {"postSample", config.detector.click.post_sample},
        {"minSep", config.detector.click.min_sep},
        {"maxLength", config.detector.click.max_length},
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
    if (config.detector.click_train_mht) {
        const auto& chi2 = config.detector.click_train_mht_chi2;
        const auto& kernel = config.detector.click_train_mht_kernel;
        body["click"]["trainMht"] = {
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
        {"minPixels", config.detector.whistle_region.min_pixels},
        {"minLength", config.detector.whistle_region.min_length},
        {"connectType", config.detector.whistle_region.connect_type},
        {"keepShapeStubs", config.detector.whistle_region.keep_shape_stubs},
        {"fragmentationMethod", config.detector.whistle_region.fragmentation_method},
        {"maxCrossLength", config.detector.whistle_region.max_cross_length},
        {"rejectFirstQuarterSecond", config.detector.whistle_region.reject_first_quarter_second},
    };
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
    const auto openapi_file = openapi_file_from_environment();
    const auto cors_origin = cors_origin_from_environment();
    const auto api_key = api_key_from_environment();

    const auto job_audio_dir = job_audio_dir_from_environment();
    const auto audio_archive_dir = audio_archive_dir_from_environment();
    std::mutex audio_archive_mutex;
    const auto result_feed_depth = result_feed_depth_from_environment();
    std::mutex result_feed_mutex;
    std::unordered_map<std::string, SessionResultFeed> result_feeds;
    const auto job_workers = job_workers_from_environment();
    JobQueueState job_state;

    pamguard::core::SessionManager manager;
    std::mutex configs_mutex;
    std::mutex archive_mutex;
    std::mutex audit_mutex;
    std::unordered_map<std::string, pamguard::core::AnalysisConfig> configs;
    std::unordered_map<std::string, SessionRuntimeStats> runtime_stats;

    if (!session_config_dir.empty() && std::filesystem::exists(session_config_dir)) {
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
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-API-Key");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    });

    server.Options(R"(.*)", [&](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
        res.set_header("Access-Control-Allow-Origin", cors_origin);
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-API-Key");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    });

    server.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        json_response(res, {
            {"ok", true},
            {"sessions", manager.session_count()},
            {"maxSessions", max_sessions},
            {"resultSchemaVersion", kResultSchemaVersion},
            {"jobQueueEnabled", !job_audio_dir.empty()},
            {"audioArchiveEnabled", !audio_archive_dir.empty()},
            {"resultFeedDepth", result_feed_depth},
            {"jobWorkers", job_audio_dir.empty() ? 0 : job_workers},
            {"maxPcmBodyBytes", max_pcm_body_bytes},
            {"maxArchiveQueryRecords", max_archive_query_records},
            {"httpThreads", http_threads},
            {"authRequired", !api_key.empty()},
            {"corsOrigin", cors_origin},
            {"sessionMetadataRequired", require_session_metadata},
            {"auditLogEnabled", !audit_log_file.empty()},
            {"sessionConfigPersistenceEnabled", !session_config_dir.empty()},
            {"resultArchiveEnabled", !result_archive_dir.empty()},
            {"archiveEventIndexEnabled", !result_archive_dir.empty()},
            {"ingestStatusEnabled", !ingest_status_file.empty()},
            {"webUiEnabled", !web_ui_file.empty()},
            {"openApiEnabled", !openapi_file.empty()},
        });
    });

    server.Get("/ready", [&](const httplib::Request&, httplib::Response& res) {
        const auto sessions = manager.session_count();
        const bool capacity_available = max_sessions == 0 || sessions < max_sessions;
        json_response(res, {
            {"ok", capacity_available},
            {"ready", capacity_available},
            {"sessions", sessions},
            {"maxSessions", max_sessions},
            {"capacityAvailable", capacity_available},
        }, capacity_available ? 200 : 503);
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
        std::ostringstream metrics;
        metrics << "# HELP pamguard_sessions Active analysis sessions\n";
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

    server.Get(R"(/sessions/([^/]+)/results)", [&](const httplib::Request& req, httplib::Response& res) {
        if (!require_authorized(req, res, api_key)) {
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
    if (!session_config_dir.empty()) {
        std::cout << "Session config persistence enabled at " << session_config_dir.string() << "\n";
    }
    if (!result_archive_dir.empty()) {
        std::cout << "Result archiving enabled at " << result_archive_dir.string() << "\n";
    }
    if (!web_ui_file.empty()) {
        std::cout << "Web UI serving enabled from " << web_ui_file.string() << "\n";
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
    if (!job_audio_dir.empty()) {
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
