#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "pamguard/core/AnalysisConfig.h"
#include "pamguard/core/AnalysisSession.h"

namespace {

std::size_t parse_size(const char* value, const char* name) {
    const auto parsed = std::stoull(value);
    if (parsed == 0) {
        throw std::invalid_argument(std::string(name) + " must be non-zero");
    }
    return static_cast<std::size_t>(parsed);
}

std::uint32_t channel_bitmap(std::size_t channel_count) {
    if (channel_count == 0 || channel_count > 32) {
        throw std::invalid_argument("channel count must be in the range 1..32 for bitmap-based detectors");
    }
    if (channel_count == 32) {
        return 0xFFFFFFFFu;
    }
    return (1u << channel_count) - 1u;
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

void print_usage() {
    std::cerr << "Usage: raw_pcm_stream_cli <sampleRateHz> <channelCount> <chunkFrames> <fftLength> <fftHop> [--click]\n";
    std::cerr << "Input format: interleaved little-endian 32-bit float PCM on stdin\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 6 && argc != 7) {
        print_usage();
        return 2;
    }

    try {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_TEXT);
#endif

        const auto sample_rate = static_cast<std::uint32_t>(parse_size(argv[1], "sampleRateHz"));
        const auto channels = parse_size(argv[2], "channelCount");
        const auto chunk_frames = parse_size(argv[3], "chunkFrames");
        const auto fft_length = parse_size(argv[4], "fftLength");
        const auto fft_hop = parse_size(argv[5], "fftHop");
        const bool enable_click = argc == 7 && std::string(argv[6]) == "--click";
        if (argc == 7 && !enable_click) {
            throw std::invalid_argument("unknown option: " + std::string(argv[6]));
        }

        pamguard::core::AnalysisConfig config;
        config.session_id = "raw-pcm-stdin";
        config.source_id = "stdin-f32le";
        config.sample_rate_hz = sample_rate;
        config.channel_count = channels;
        config.detector.fft.fft_length = fft_length;
        config.detector.fft.fft_hop = fft_hop;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            config.detector.fft.channels.push_back(channel);
        }
        config.detector.click_detector_enabled = enable_click;
        if (enable_click) {
            config.detector.click.channel_bitmap = channel_bitmap(channels);
            config.detector.click.trigger_bitmap = config.detector.click.channel_bitmap;
        }

        pamguard::core::AnalysisSession session(config);
        const auto bytes_per_frame = channels * sizeof(float);
        std::vector<unsigned char> byte_buffer(chunk_frames * bytes_per_frame);
        std::uint64_t start_sample = 0;

        while (std::cin) {
            std::cin.read(reinterpret_cast<char*>(byte_buffer.data()), static_cast<std::streamsize>(byte_buffer.size()));
            const auto bytes_read = static_cast<std::size_t>(std::cin.gcount());
            if (bytes_read == 0) {
                break;
            }
            const auto complete_frames = bytes_read / bytes_per_frame;
            if (complete_frames == 0) {
                break;
            }

            pamguard::core::AudioChunk chunk;
            chunk.start_sample = start_sample;
            chunk.time_unix_ms = static_cast<std::int64_t>(static_cast<double>(start_sample) * 1000.0 / sample_rate);
            chunk.sample_rate_hz = sample_rate;
            chunk.channel_count = channels;
            chunk.interleaved_pcm.resize(complete_frames * channels);

            for (std::size_t frame = 0; frame < complete_frames; ++frame) {
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    const auto offset = (frame * channels + channel) * sizeof(float);
                    chunk.interleaved_pcm[frame * channels + channel] = read_float_le(byte_buffer.data() + offset);
                }
            }

            const auto result = session.process(chunk);
            std::cout << "{\"startSample\":" << start_sample
                      << ",\"inputFrames\":" << complete_frames
                      << ",\"spectrogramFrames\":" << result.spectrogram_frames.size()
                      << ",\"clicks\":" << result.clicks.size()
                      << "}\n";
            start_sample += complete_frames;

            if (bytes_read != byte_buffer.size()) {
                break;
            }
        }

        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
