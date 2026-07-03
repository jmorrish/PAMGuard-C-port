#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "pamguard/core/AnalysisConfig.h"
#include "pamguard/core/AnalysisSession.h"
#include "pamguard/io/WavReader.h"

namespace {

std::size_t parse_size(const char* value, const char* name) {
    const auto parsed = std::stoull(value);
    if (parsed == 0) {
        throw std::invalid_argument(std::string(name) + " must be non-zero");
    }
    return static_cast<std::size_t>(parsed);
}

std::string json_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: spectrogram_file_cli <wavPath> <fftLength> <fftHop>\n";
        return 2;
    }

    try {
        const std::filesystem::path path(argv[1]);
        const auto fft_length = parse_size(argv[2], "fftLength");
        const auto fft_hop = parse_size(argv[3], "fftHop");

        pamguard::io::WavReader reader;
        auto chunk = reader.read_all_as_chunk(path);

        pamguard::core::AnalysisConfig config;
        config.session_id = "spectrogram-file-cli";
        config.source_id = path.string();
        config.sample_rate_hz = chunk.sample_rate_hz;
        config.channel_count = chunk.channel_count;
        config.detector.fft.fft_length = fft_length;
        config.detector.fft.fft_hop = fft_hop;
        config.detector.fft.window_type = pamguard::dsp::WindowType::Hann;

        for (std::size_t channel = 0; channel < chunk.channel_count; ++channel) {
            config.detector.fft.channels.push_back(channel);
        }

        pamguard::core::AnalysisSession session(config);
        const auto frames = session.process_audio(chunk);

        std::cout << "{\n";
        std::cout << "  \"path\": \"" << json_escape(path.string()) << "\",\n";
        std::cout << "  \"sampleRateHz\": " << chunk.sample_rate_hz << ",\n";
        std::cout << "  \"channelCount\": " << chunk.channel_count << ",\n";
        std::cout << "  \"frameCount\": " << frames.size() << ",\n";
        std::cout << "  \"fftLength\": " << fft_length << ",\n";
        std::cout << "  \"fftHop\": " << fft_hop << "\n";
        std::cout << "}\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
