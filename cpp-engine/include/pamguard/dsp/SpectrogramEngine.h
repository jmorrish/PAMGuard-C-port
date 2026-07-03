#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include "pamguard/core/AnalysisConfig.h"
#include "pamguard/core/AudioFrame.h"
#include "pamguard/dsp/ComplexSpectrum.h"
#include "pamguard/dsp/RealFft.h"

namespace pamguard::dsp {

struct SpectrogramFrame {
    std::size_t channel = 0;
    std::int64_t start_sample = 0;
    std::int64_t time_unix_ms = 0;
    std::size_t fft_slice = 0;
    ComplexSpectrum bins;
};

class SpectrogramEngine {
public:
    explicit SpectrogramEngine(core::FftConfig config);

    [[nodiscard]] const core::FftConfig& config() const noexcept;
    [[nodiscard]] double window_gain() const noexcept;

    std::vector<SpectrogramFrame> process(const core::AudioChunk& chunk);

private:
    struct ChannelState {
        std::deque<double> samples;
        std::int64_t next_start_sample = 0;
        std::size_t fft_slice = 0;
        bool initialized = false;
    };

    core::FftConfig config_;
    std::vector<double> window_;
    double window_gain_ = 1.0;
    RealFft fft_;
    std::unordered_map<std::size_t, ChannelState> channel_states_;

    bool wants_channel(std::size_t channel) const;
    std::int64_t estimate_frame_time_ms(const core::AudioChunk& chunk, std::int64_t start_sample) const;
};

} // namespace pamguard::dsp
