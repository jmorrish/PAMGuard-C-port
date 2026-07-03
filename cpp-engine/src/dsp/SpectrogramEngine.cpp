#include "pamguard/dsp/SpectrogramEngine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "pamguard/dsp/WindowFunction.h"

namespace pamguard::dsp {

SpectrogramEngine::SpectrogramEngine(core::FftConfig config)
    : config_(std::move(config)),
      window_(make_window(config_.window_type, config_.fft_length)),
      window_gain_(pamguard::dsp::window_gain(window_)) {
    if (config_.fft_length == 0 || config_.fft_hop == 0) {
        throw std::invalid_argument("fft_length and fft_hop must be non-zero");
    }
    if (config_.fft_hop > config_.fft_length) {
        throw std::invalid_argument("fft_hop greater than fft_length is not yet supported in the parity scaffold");
    }
}

const core::FftConfig& SpectrogramEngine::config() const noexcept {
    return config_;
}

double SpectrogramEngine::window_gain() const noexcept {
    return window_gain_;
}

std::vector<SpectrogramFrame> SpectrogramEngine::process(const core::AudioChunk& chunk) {
    if (chunk.sample_rate_hz == 0 || chunk.channel_count == 0) {
        throw std::invalid_argument("audio chunk must include sample rate and channel count");
    }

    std::vector<SpectrogramFrame> frames;
    const std::size_t n_frames = chunk.frame_count();

    for (std::size_t channel = 0; channel < chunk.channel_count; ++channel) {
        if (!wants_channel(channel)) {
            continue;
        }

        auto& state = channel_states_[channel];
        if (!state.initialized) {
            state.next_start_sample = static_cast<std::int64_t>(chunk.start_sample) - 1;
            state.initialized = true;
        }

        for (std::size_t i = 0; i < n_frames; ++i) {
            state.samples.push_back(chunk.sample(i, channel));
        }

        while (state.samples.size() >= config_.fft_length) {
            std::vector<double> block(config_.fft_length);
            for (std::size_t i = 0; i < config_.fft_length; ++i) {
                block[i] = state.samples[i] * window_[i];
            }

            SpectrogramFrame frame;
            frame.channel = channel;
            frame.start_sample = state.next_start_sample;
            frame.time_unix_ms = estimate_frame_time_ms(chunk, state.next_start_sample);
            frame.fft_slice = state.fft_slice++;
            frame.bins = fft_.forward(block, config_.fft_length);
            frames.push_back(std::move(frame));

            for (std::size_t i = 0; i < config_.fft_hop; ++i) {
                state.samples.pop_front();
            }
            state.next_start_sample += config_.fft_hop;
        }
    }

    return frames;
}

bool SpectrogramEngine::wants_channel(std::size_t channel) const {
    if (config_.channels.empty()) {
        return true;
    }
    return std::find(config_.channels.begin(), config_.channels.end(), channel) != config_.channels.end();
}

std::int64_t SpectrogramEngine::estimate_frame_time_ms(const core::AudioChunk& chunk, std::int64_t start_sample) const {
    if (chunk.sample_rate_hz == 0) {
        return chunk.time_unix_ms;
    }
    const auto delta_samples = static_cast<double>(start_sample - static_cast<std::int64_t>(chunk.start_sample));
    const auto delta_ms = static_cast<std::int64_t>(delta_samples * 1000.0 / chunk.sample_rate_hz);
    return chunk.time_unix_ms + delta_ms;
}

} // namespace pamguard::dsp
