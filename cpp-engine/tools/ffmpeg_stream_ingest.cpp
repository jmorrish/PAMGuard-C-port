#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <httplib.h>
#include <json.hpp>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace {

using json = nlohmann::json;

struct Options {
    std::string source_url;
    std::string engine_url = "http://127.0.0.1:8080";
    std::string session_id;
    std::string source_id;
    std::string ffmpeg = "ffmpeg";
    std::size_t sample_rate_hz = 48000;
    std::size_t channel_count = 1;
    std::size_t chunk_frames = 4096;
    std::size_t max_chunks = 0;
    bool restart = false;
    std::size_t max_restarts = 0;
    std::size_t restart_delay_ms = 5000;
    std::uint64_t start_sample = 0;
    std::string api_key;
    std::string api_key_env;
    std::string session_config_path;
    std::string owner_id;
    std::string tenant_id;
    std::string audio_filter;
    std::vector<std::string> ffmpeg_input_options;
    bool allow_existing_session = false;
    bool resume_from_engine = false;
    bool realtime = false;
    /** When enabled, PCM POSTs request spectrogram preview frames, which the
     * engine's result feed then carries to live viewers. preview_bins == 0
     * means the full spectrum at full resolution. */
    bool preview = false;
    std::size_t preview_bins = 0;
};

struct Endpoint {
    std::string host;
    int port = 80;
    std::string base_path;
    /** Extra query parameters appended to every PCM POST. */
    std::string extra_query;
};

struct ChunkPostResult {
    std::string sample_continuity = "unknown";
    std::int64_t sample_delta = 0;
    std::uint64_t next_expected_start_sample = 0;
};

void usage() {
    std::cerr
        << "Usage: ffmpeg_stream_ingest --source <url-or-file> --session <id> [options]\n"
        << "\n"
        << "Options:\n"
        << "  --engine <http://host:port>     Engine service URL (default http://127.0.0.1:8080)\n"
        << "  --source-id <id>                Overlay sourceId when posting --session-config\n"
        << "  --sample-rate <hz>              Output sample rate sent to the session (default 48000)\n"
        << "  --channels <n>                  Output channel count sent to the session (default 1)\n"
        << "  --chunk-frames <n>              Frames per POST chunk (default 4096)\n"
        << "  --ffmpeg <path>                 FFmpeg executable (default ffmpeg)\n"
        << "  --max-chunks <n>                Stop after n chunks, useful for smoke tests\n"
        << "  --restart                       Restart FFmpeg after EOF/error\n"
        << "  --max-restarts <n>              Maximum restarts when --restart is set (0 = unlimited)\n"
        << "  --restart-delay-ms <n>          Delay before restart (default 5000)\n"
        << "  --start-sample <n>              Initial startSample for the first posted chunk (default 0)\n"
        << "  --api-key <key>                 Send X-API-Key to a protected engine service\n"
        << "  --api-key-env <name>            Read X-API-Key from a named environment variable\n"
        << "  --session-config <json-file>    Create the engine session before ingest starts\n"
        << "  --owner-id <id>                 Overlay ownerId when posting --session-config\n"
        << "  --tenant-id <id>                Overlay tenantId when posting --session-config\n"
        << "  --audio-filter <ffmpeg-filter>  Optional FFmpeg -af filter for channel mapping/filtering\n"
        << "  --preview-bins <n>              Ask the engine for spectrogram preview frames per chunk\n"
        << "                                  (0 = full spectrum; feeds live viewers via the result feed)\n"
        << "  --ffmpeg-input-option <arg>     Extra FFmpeg input option token before -i; repeat as needed\n"
        << "  --allow-existing-session        Continue if --session-config finds the session already exists\n"
        << "  --resume-from-engine            Start from the engine session runtime.expectedStartSample\n"
        << "  --realtime                      Ask FFmpeg to read the input at native rate (-re)\n";
}

std::string require_value(int& index, int argc, char** argv, std::string_view name) {
    if (index + 1 >= argc) {
        throw std::invalid_argument(std::string(name) + " requires a value");
    }
    ++index;
    return argv[index];
}

std::uint64_t parse_uint64_value(const std::string& value, std::string_view name) {
    if (!value.empty() && value.front() == '-') {
        throw std::invalid_argument(std::string(name) + " must be non-negative");
    }
    return static_cast<std::uint64_t>(std::stoull(value));
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--source") {
            options.source_url = require_value(i, argc, argv, arg);
        }
        else if (arg == "--engine") {
            options.engine_url = require_value(i, argc, argv, arg);
        }
        else if (arg == "--session") {
            options.session_id = require_value(i, argc, argv, arg);
        }
        else if (arg == "--source-id") {
            options.source_id = require_value(i, argc, argv, arg);
        }
        else if (arg == "--sample-rate") {
            options.sample_rate_hz = static_cast<std::size_t>(std::stoull(require_value(i, argc, argv, arg)));
        }
        else if (arg == "--channels") {
            options.channel_count = static_cast<std::size_t>(std::stoull(require_value(i, argc, argv, arg)));
        }
        else if (arg == "--chunk-frames") {
            options.chunk_frames = static_cast<std::size_t>(std::stoull(require_value(i, argc, argv, arg)));
        }
        else if (arg == "--ffmpeg") {
            options.ffmpeg = require_value(i, argc, argv, arg);
        }
        else if (arg == "--max-chunks") {
            options.max_chunks = static_cast<std::size_t>(std::stoull(require_value(i, argc, argv, arg)));
        }
        else if (arg == "--restart") {
            options.restart = true;
        }
        else if (arg == "--max-restarts") {
            options.max_restarts = static_cast<std::size_t>(std::stoull(require_value(i, argc, argv, arg)));
        }
        else if (arg == "--restart-delay-ms") {
            options.restart_delay_ms = static_cast<std::size_t>(std::stoull(require_value(i, argc, argv, arg)));
        }
        else if (arg == "--start-sample") {
            options.start_sample = parse_uint64_value(require_value(i, argc, argv, arg), arg);
        }
        else if (arg == "--api-key") {
            options.api_key = require_value(i, argc, argv, arg);
        }
        else if (arg == "--api-key-env") {
            options.api_key_env = require_value(i, argc, argv, arg);
        }
        else if (arg == "--session-config") {
            options.session_config_path = require_value(i, argc, argv, arg);
        }
        else if (arg == "--owner-id") {
            options.owner_id = require_value(i, argc, argv, arg);
        }
        else if (arg == "--tenant-id") {
            options.tenant_id = require_value(i, argc, argv, arg);
        }
        else if (arg == "--audio-filter") {
            options.audio_filter = require_value(i, argc, argv, arg);
        }
        else if (arg == "--preview-bins") {
            options.preview = true;
            options.preview_bins = static_cast<std::size_t>(std::stoull(require_value(i, argc, argv, arg)));
        }
        else if (arg == "--ffmpeg-input-option") {
            options.ffmpeg_input_options.push_back(require_value(i, argc, argv, arg));
        }
        else if (arg == "--allow-existing-session") {
            options.allow_existing_session = true;
        }
        else if (arg == "--resume-from-engine") {
            options.resume_from_engine = true;
        }
        else if (arg == "--realtime") {
            options.realtime = true;
        }
        else if (arg == "--help" || arg == "-h") {
            usage();
            std::exit(0);
        }
        else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }

    if (options.source_url.empty()) {
        throw std::invalid_argument("--source is required");
    }
    if (options.session_id.empty()) {
        throw std::invalid_argument("--session is required");
    }
    if (options.sample_rate_hz == 0 || options.channel_count == 0 || options.chunk_frames == 0) {
        throw std::invalid_argument("--sample-rate, --channels, and --chunk-frames must be positive");
    }
    if (options.api_key.empty() && !options.api_key_env.empty()) {
        const char* raw_api_key = std::getenv(options.api_key_env.c_str());
        if (raw_api_key == nullptr || std::string(raw_api_key).empty()) {
            throw std::invalid_argument("--api-key-env was set but the named environment variable is empty or missing");
        }
        options.api_key = raw_api_key;
    }
    if (options.api_key.empty()) {
        const char* raw_api_key = std::getenv("PAMGUARD_API_KEY");
        if (raw_api_key != nullptr) {
            options.api_key = raw_api_key;
        }
    }
    return options;
}

Endpoint parse_http_endpoint(std::string url) {
    constexpr std::string_view prefix = "http://";
    if (url.rfind(prefix, 0) != 0) {
        throw std::invalid_argument("only http:// engine URLs are currently supported");
    }
    url.erase(0, prefix.size());

    Endpoint endpoint;
    const auto slash = url.find('/');
    std::string host_port = slash == std::string::npos ? url : url.substr(0, slash);
    endpoint.base_path = slash == std::string::npos ? "" : url.substr(slash);
    if (!endpoint.base_path.empty() && endpoint.base_path.back() == '/') {
        endpoint.base_path.pop_back();
    }

    const auto colon = host_port.rfind(':');
    if (colon == std::string::npos) {
        endpoint.host = host_port;
        endpoint.port = 80;
    }
    else {
        endpoint.host = host_port.substr(0, colon);
        endpoint.port = std::stoi(host_port.substr(colon + 1));
    }
    if (endpoint.host.empty() || endpoint.port <= 0) {
        throw std::invalid_argument("invalid engine URL");
    }
    return endpoint;
}

std::string shell_quote(const std::string& value) {
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        }
        else {
            quoted += ch;
        }
    }
    quoted += "\"";
    return quoted;
}

bool contains_shell_whitespace(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

std::string shell_quote_executable(const std::string& value) {
#ifdef _WIN32
    if (!contains_shell_whitespace(value)) {
        return value;
    }
#endif
    return shell_quote(value);
}

std::string url_path_escape(const std::string& value) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out << static_cast<char>(ch);
        }
        else {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return out.str();
}

std::string make_ffmpeg_command(const Options& options) {
    std::ostringstream command;
    command << shell_quote_executable(options.ffmpeg)
            << " -nostdin -hide_banner -loglevel error";
    if (options.realtime) {
        command << " -re";
    }
    if (options.source_url.rfind("http://", 0) == 0 || options.source_url.rfind("https://", 0) == 0) {
        command << " -reconnect 1 -reconnect_streamed 1 -reconnect_delay_max 5";
    }
    for (const auto& input_option : options.ffmpeg_input_options) {
        command << " " << shell_quote(input_option);
    }
    command << " -i " << shell_quote(options.source_url)
            << " -vn";
    if (!options.audio_filter.empty()) {
        command << " -af " << shell_quote(options.audio_filter);
    }
    command << " -ac " << options.channel_count
            << " -ar " << options.sample_rate_hz
            << " -f f32le pipe:1";
    return command.str();
}

void configure_client(httplib::Client& client, const Options& options) {
    client.set_connection_timeout(5);
    client.set_read_timeout(30);
    client.set_write_timeout(30);
    if (!options.api_key.empty()) {
        client.set_default_headers({{"X-API-Key", options.api_key}});
    }
}

std::string read_text_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open session config: " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void post_session_config(httplib::Client& client, const Endpoint& endpoint, const Options& options) {
    if (options.session_config_path.empty()) {
        return;
    }
    auto body_json = json::parse(read_text_file(options.session_config_path));
    if (!options.source_id.empty()) {
        body_json["sourceId"] = options.source_id;
    }
    if (!options.owner_id.empty()) {
        body_json["ownerId"] = options.owner_id;
    }
    if (!options.tenant_id.empty()) {
        body_json["tenantId"] = options.tenant_id;
    }
    const auto body = body_json.dump();
    auto response = client.Post(endpoint.base_path + "/sessions", body, "application/json");
    if (!response) {
        throw std::runtime_error("session create failed: no response");
    }
    if (options.allow_existing_session && response->status == 400 && response->body.find("session already exists") != std::string::npos) {
        std::cout << "session already exists; continuing because --allow-existing-session is set\n";
        return;
    }
    if (response->status < 200 || response->status >= 300) {
        throw std::runtime_error("session create failed with status " + std::to_string(response->status) + ": " + response->body);
    }
    std::cout << "created session from " << options.session_config_path << "\n";
}

json query_session_status(httplib::Client& client, const Endpoint& endpoint, const std::string& session_id) {
    const auto path = endpoint.base_path + "/sessions/" + url_path_escape(session_id);
    auto response = client.Get(path);
    if (!response) {
        throw std::runtime_error("session status query failed: no response");
    }
    if (response->status < 200 || response->status >= 300) {
        throw std::runtime_error("session status query failed with status " + std::to_string(response->status) + ": " + response->body);
    }
    return json::parse(response->body);
}

void verify_session_audio_shape(const json& body, const Options& options) {
    const auto sample_rate_hz = body.value("sampleRateHz", 0u);
    const auto channel_count = body.value("channelCount", 0u);
    if (sample_rate_hz != options.sample_rate_hz) {
        throw std::runtime_error(
            "session sampleRateHz " + std::to_string(sample_rate_hz) +
            " does not match ingest --sample-rate " + std::to_string(options.sample_rate_hz));
    }
    if (channel_count != options.channel_count) {
        throw std::runtime_error(
            "session channelCount " + std::to_string(channel_count) +
            " does not match ingest --channels " + std::to_string(options.channel_count));
    }
}

std::optional<std::uint64_t> expected_start_sample_from_status(const json& body) {
    if (!body.contains("runtime") || !body.at("runtime").is_object()) {
        return std::nullopt;
    }
    const auto& runtime = body.at("runtime");
    if (!runtime.contains("expectedStartSample")) {
        return std::nullopt;
    }
    return runtime.at("expectedStartSample").get<std::uint64_t>();
}

ChunkPostResult parse_chunk_post_result(const std::string& body, std::uint64_t fallback_next_expected_start_sample) {
    ChunkPostResult result;
    result.next_expected_start_sample = fallback_next_expected_start_sample;
    try {
        const auto parsed = json::parse(body);
        result.sample_continuity = parsed.value("sampleContinuity", result.sample_continuity);
        result.sample_delta = parsed.value("sampleDelta", result.sample_delta);
        result.next_expected_start_sample = parsed.value("nextExpectedStartSample", result.next_expected_start_sample);
    }
    catch (const std::exception&) {
        result.sample_continuity = "unparsed";
    }
    return result;
}

ChunkPostResult post_chunk(
    httplib::Client& client,
    const Endpoint& endpoint,
    const std::string& session_id,
    const char* data,
    std::size_t byte_count,
    std::uint64_t start_sample,
    std::size_t frame_count) {
    const auto path = endpoint.base_path + "/sessions/" + url_path_escape(session_id) +
        "/pcm-f32le?startSample=" + std::to_string(start_sample) + endpoint.extra_query;
    const std::string body(data, byte_count);
    auto response = client.Post(path, body, "application/octet-stream");
    if (!response) {
        throw std::runtime_error("engine POST failed: no response");
    }
    if (response->status < 200 || response->status >= 300) {
        throw std::runtime_error("engine POST failed with status " + std::to_string(response->status) + ": " + response->body);
    }
    return parse_chunk_post_result(response->body, start_sample + frame_count);
}

void post_flush(httplib::Client& client, const Endpoint& endpoint, const std::string& session_id) {
    const auto path = endpoint.base_path + "/sessions/" + url_path_escape(session_id) + "/flush";
    auto response = client.Post(path, "", "application/json");
    if (!response) {
        throw std::runtime_error("engine flush failed: no response");
    }
    if (response->status < 200 || response->status >= 300) {
        throw std::runtime_error("engine flush failed with status " + std::to_string(response->status) + ": " + response->body);
    }
    std::cout << "flushed session " << session_id << "\n";
}

bool run_stream_once(
    const Options& options,
    const Endpoint& endpoint,
    std::uint64_t& start_sample,
    std::size_t& chunks_posted) {
    const auto command = make_ffmpeg_command(options);
    const auto frame_bytes = options.channel_count * sizeof(float);
    const auto chunk_bytes = options.chunk_frames * frame_bytes;

    FILE* pipe = popen(command.c_str(), "rb");
    if (pipe == nullptr) {
        throw std::runtime_error("failed to start FFmpeg");
    }

    try {
        httplib::Client client(endpoint.host, endpoint.port);
        configure_client(client, options);

        std::vector<char> read_buffer(chunk_bytes);
        std::vector<char> pending;
        pending.reserve(chunk_bytes * 2);

        while (true) {
            const auto bytes_read = std::fread(read_buffer.data(), 1, read_buffer.size(), pipe);
            if (bytes_read > 0) {
                pending.insert(pending.end(), read_buffer.begin(), read_buffer.begin() + static_cast<std::ptrdiff_t>(bytes_read));
            }

            while (pending.size() >= chunk_bytes) {
                const auto post_result = post_chunk(client, endpoint, options.session_id, pending.data(), chunk_bytes, start_sample, options.chunk_frames);
                pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(chunk_bytes));
                std::cout << "posted chunk " << chunks_posted + 1
                          << " startSample=" << start_sample
                          << " continuity=" << post_result.sample_continuity
                          << " delta=" << post_result.sample_delta
                          << " nextExpected=" << post_result.next_expected_start_sample << "\n";
                start_sample += options.chunk_frames;
                ++chunks_posted;
                if (options.max_chunks > 0 && chunks_posted >= options.max_chunks) {
                    pclose(pipe);
                    pipe = nullptr;
                    return true;
                }
            }

            if (bytes_read == 0) {
                break;
            }
        }

        const auto whole_frames = pending.size() / frame_bytes;
        if (whole_frames > 0) {
            const auto whole_bytes = whole_frames * frame_bytes;
            const auto post_result = post_chunk(client, endpoint, options.session_id, pending.data(), whole_bytes, start_sample, whole_frames);
            ++chunks_posted;
            std::cout << "posted final chunk " << chunks_posted
                      << " startSample=" << start_sample
                      << " frames=" << whole_frames
                      << " continuity=" << post_result.sample_continuity
                      << " delta=" << post_result.sample_delta
                      << " nextExpected=" << post_result.next_expected_start_sample << "\n";
            start_sample += whole_frames;
        }

        const int exit_code = pclose(pipe);
        pipe = nullptr;
        if (exit_code != 0) {
            throw std::runtime_error("FFmpeg exited with code " + std::to_string(exit_code));
        }
        if (!options.restart) {
            post_flush(client, endpoint, options.session_id);
        }
        return false;
    }
    catch (...) {
        if (pipe != nullptr) {
            pclose(pipe);
        }
        throw;
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        auto endpoint = parse_http_endpoint(options.engine_url);
        if (options.preview) {
            endpoint.extra_query = "&includeSpectrogram=true";
            if (options.preview_bins > 0) {
                endpoint.extra_query += "&spectrogramMaxBins=" + std::to_string(options.preview_bins);
            }
        }
        std::uint64_t start_sample = options.start_sample;
        std::size_t chunks_posted = 0;
        std::size_t restart_count = 0;

        {
            httplib::Client client(endpoint.host, endpoint.port);
            configure_client(client, options);
            if (!options.session_config_path.empty()) {
                post_session_config(client, endpoint, options);
            }
            const auto session_status = query_session_status(client, endpoint, options.session_id);
            verify_session_audio_shape(session_status, options);
            if (options.resume_from_engine) {
                const auto expected = expected_start_sample_from_status(session_status);
                if (expected) {
                    std::cout << "resuming startSample from engine expectedStartSample="
                              << *expected << " (local initial=" << start_sample << ")\n";
                    start_sample = *expected;
                }
                else {
                    std::cout << "engine session has no runtime.expectedStartSample; using startSample="
                              << start_sample << "\n";
                }
            }
        }

        while (true) {
            try {
                const bool reached_max_chunks = run_stream_once(options, endpoint, start_sample, chunks_posted);
                if (reached_max_chunks || !options.restart) {
                    return 0;
                }
                std::cerr << "FFmpeg stream ended; restarting";
            }
            catch (const std::exception& error) {
                if (!options.restart) {
                    throw;
                }
                std::cerr << error.what() << "; restarting";
            }

            ++restart_count;
            if (options.max_restarts > 0 && restart_count > options.max_restarts) {
                throw std::runtime_error("maximum FFmpeg restarts exceeded");
            }
            std::cerr << " in " << options.restart_delay_ms << " ms"
                      << " (restart " << restart_count
                      << ", nextStartSample=" << start_sample << ")\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(options.restart_delay_ms));
        }
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        usage();
        return 1;
    }
}
