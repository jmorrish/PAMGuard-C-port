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
#include "pamguard/dsp/WindowFunction.h"

using json = nlohmann::json;

namespace {

constexpr std::size_t kMaxServiceChannelCount = 1024;
constexpr int kResultSchemaVersion = 9;

struct ResultJsonOptions {
    bool include_spectrogram = false;
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
    if (array.contains("hydrophones")) {
        for (const auto& hydrophone : array.at("hydrophones")) {
            pamguard::core::ArrayHydrophone item;
            item.channel = hydrophone.at("channel").get<std::size_t>();
            item.x_m = hydrophone.value("xM", 0.0);
            item.y_m = hydrophone.value("yM", 0.0);
            item.z_m = hydrophone.value("zM", 0.0);
            item.sensitivity_db = hydrophone.value("sensitivityDb", 0.0);
            config.array.hydrophones.push_back(item);
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
            loc["lsqBearing"] = std::move(lsq_item);
        }
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
            }
            item["delays"].push_back(std::move(delay_item));
        }
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
        {"basicClassifierEnabled", config.detector.click_basic_classifier_enabled},
        {"basicClassifierTypeCount", config.detector.click_basic_classifier.click_types.size()},
        {"trainEnabled", config.detector.click_train_tracker_enabled},
        {"trainMaxIciSeconds", config.detector.click_train.max_ici_seconds},
        {"trainMinClicks", config.detector.click_train.min_clicks},
    };
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

bool request_authorized(const httplib::Request& req, const std::string& api_key) {
    if (api_key.empty()) {
        return true;
    }
    if (req.has_header("X-API-Key") && req.get_header_value("X-API-Key") == api_key) {
        return true;
    }
    if (req.has_header("Authorization")) {
        const std::string authorization = req.get_header_value("Authorization");
        constexpr std::string_view bearer = "Bearer ";
        if (authorization.rfind(bearer, 0) == 0 && authorization.substr(bearer.size()) == api_key) {
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
    if (http_threads > 0) {
        std::cout << "HTTP worker thread pool set to " << http_threads << "\n";
    }
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Failed to listen on port " << port << "\n";
        return 1;
    }
    return 0;
}
