#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "pamguard/core/AudioFrame.h"
#include "pamguard/dsp/SpectrogramEngine.h"

namespace {

pamguard::core::FftConfig test_config() {
    pamguard::core::FftConfig config;
    config.fft_length = 8;
    config.fft_hop = 4;
    config.channels = {0};
    config.window_type = pamguard::dsp::WindowType::Hann;
    return config;
}

pamguard::core::AudioChunk make_chunk(std::uint64_t start_sample, std::size_t offset, std::size_t count) {
    pamguard::core::AudioChunk chunk;
    chunk.start_sample = start_sample;
    chunk.time_unix_ms = 1000;
    chunk.sample_rate_hz = 8000;
    chunk.channel_count = 1;
    chunk.interleaved_pcm.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double sample = std::sin(static_cast<double>(offset + i) * 0.2) + 0.25 * std::cos(static_cast<double>(offset + i) * 0.7);
        chunk.interleaved_pcm.push_back(sample);
    }
    return chunk;
}

void compare_frames(const std::vector<pamguard::dsp::SpectrogramFrame>& one,
                    const std::vector<pamguard::dsp::SpectrogramFrame>& split) {
    if (one.size() != split.size()) {
        throw std::runtime_error("frame count mismatch");
    }

    constexpr double tolerance = 1e-12;
    for (std::size_t i = 0; i < one.size(); ++i) {
        if (one[i].start_sample != split[i].start_sample || one[i].fft_slice != split[i].fft_slice) {
            throw std::runtime_error("frame metadata mismatch");
        }
        if (one[i].bins.size() != split[i].bins.size()) {
            throw std::runtime_error("bin count mismatch");
        }
        for (std::size_t b = 0; b < one[i].bins.size(); ++b) {
            const double error = std::abs(one[i].bins[b] - split[i].bins[b]);
            if (error > tolerance) {
                throw std::runtime_error("bin value mismatch");
            }
        }
    }
}

} // namespace

int main() {
    try {
        pamguard::dsp::SpectrogramEngine one_engine(test_config());
        const auto one = one_engine.process(make_chunk(0, 0, 16));

        pamguard::dsp::SpectrogramEngine split_engine(test_config());
        std::vector<pamguard::dsp::SpectrogramFrame> split;
        auto first = split_engine.process(make_chunk(0, 0, 5));
        auto second = split_engine.process(make_chunk(5, 5, 11));
        split.insert(split.end(), first.begin(), first.end());
        split.insert(split.end(), second.begin(), second.end());

        compare_frames(one, split);
        std::cout << "Spectrogram chunking invariance passed with " << one.size() << " frames\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}

