#include <cmath>
#include <cstddef>
#include <iostream>
#include <numbers>

#include "pamguard/core/AnalysisSession.h"

namespace {

constexpr double sample_rate_hz = 48000.0;
constexpr std::size_t fft_length = 256;
constexpr std::size_t fft_hop = 128;
constexpr double channel_1_delay_samples = 3.0;

pamguard::core::AnalysisConfig base_config(bool with_hydrophones) {
    pamguard::core::AnalysisConfig config;
    config.session_id = "whistle-delay-check";
    config.sample_rate_hz = static_cast<std::uint32_t>(sample_rate_hz);
    config.channel_count = 2;
    config.detector.fft.fft_length = fft_length;
    config.detector.fft.fft_hop = fft_hop;
    config.detector.whistle_peak_detector_enabled = true;
    config.detector.whistle_region_detector_enabled = true;

    config.array.speed_of_sound_mps = 1500.0;
    config.array.spacing_error_m = 0.02;
    config.array.timing_error_seconds = 1.0e-5;
    if (with_hydrophones) {
        config.array.hydrophones = {
            {0, 0.0, 0.0, 0.0, 0.0},
            {1, 0.0, 3.0, 0.0, 0.0},
        };
    }
    return config;
}

pamguard::core::AudioChunk synthetic_chunk() {
    // 110 warmup slices of background, 30 slices of a three-bin tone burst
    // (bins 29-31), then 20 quiet slices so the region completes in-stream.
    constexpr std::size_t total_slices = 160;
    constexpr std::size_t frame_count = total_slices * fft_hop + fft_length;
    constexpr std::size_t tone_start_sample = 110 * fft_hop;
    constexpr std::size_t tone_end_sample = tone_start_sample + 30 * fft_hop;

    pamguard::core::AudioChunk chunk;
    chunk.start_sample = 0;
    chunk.time_unix_ms = 0;
    chunk.sample_rate_hz = static_cast<std::uint32_t>(sample_rate_hz);
    chunk.channel_count = 2;
    chunk.interleaved_pcm.assign(frame_count * 2, 0.0);
    for (std::size_t i = 0; i < frame_count; ++i) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            // Keep the background tiny and far from the burst band: a coherent
            // in-band background with an inter-channel phase offset reads as a
            // spurious pseudo-delay region.
            double value = 0.0005 * std::sin(static_cast<double>(i) * 2.4 + static_cast<double>(channel) * 0.9);
            if (i >= tone_start_sample && i < tone_end_sample) {
                const double delayed = static_cast<double>(i) - (channel == 1 ? channel_1_delay_samples : 0.0);
                for (std::size_t bin = 29; bin <= 31; ++bin) {
                    const double frequency_hz = static_cast<double>(bin) * sample_rate_hz / static_cast<double>(fft_length);
                    value += 0.3 * std::sin(2.0 * std::numbers::pi * frequency_hz * delayed / sample_rate_hz);
                }
            }
            chunk.interleaved_pcm[i * 2 + channel] = value;
        }
    }
    return chunk;
}

} // namespace

int main() {
    try {
        {
            pamguard::core::AnalysisSession session(base_config(true));
            auto result = session.process(synthetic_chunk());
            const auto flushed = session.flush();
            result.whistle_regions.insert(result.whistle_regions.end(),
                                          flushed.whistle_regions.begin(), flushed.whistle_regions.end());
            result.whistle_delays.insert(result.whistle_delays.end(),
                                         flushed.whistle_delays.begin(), flushed.whistle_delays.end());

            if (result.whistle_regions.empty()) {
                std::cerr << "Whistle delay check produced no whistle regions\n";
                return 1;
            }
            if (result.whistle_delays.empty()) {
                std::cerr << "Whistle delay check produced no whistle delays\n";
                return 1;
            }

            // Small leakage-skirt fragments at burst onset/offset can land on
            // a narrowband ambiguity lobe (pinned by the whistle-delay
            // fixture), so the delay value is asserted on each channel's
            // best-scoring region while structure is asserted on all.
            double best_delay[2] = {0.0, 0.0};
            double best_score[2] = {-1.0, -1.0};
            for (const auto& whistle_delay : result.whistle_delays) {
                if (whistle_delay.delays.size() != 1) {
                    std::cerr << "Two-channel whistle region should carry exactly one pair delay\n";
                    return 1;
                }
                const auto& delay = whistle_delay.delays.front();
                if (!delay.geometry_constrained || delay.audio_channel_a != 0 || delay.audio_channel_b != 1) {
                    std::cerr << "Whistle delay pair missing geometry constraint or channel mapping\n";
                    return 1;
                }
                if (!delay.pair_bearing_valid) {
                    std::cerr << "Whistle delay pair missing pair bearing output\n";
                    return 1;
                }
                if (!whistle_delay.bearing_valid || !whistle_delay.bearing_ambiguity ||
                    whistle_delay.bearing_pair_count != whistle_delay.delays.size() ||
                    whistle_delay.bearing_radians != delay.pair_bearing_radians) {
                    std::cerr << "Whistle region bearing summary missing or inconsistent\n";
                    return 1;
                }
                if (whistle_delay.channel < 2 && delay.delay.delay_score > best_score[whistle_delay.channel]) {
                    best_score[whistle_delay.channel] = delay.delay.delay_score;
                    best_delay[whistle_delay.channel] = delay.delay.delay_samples;
                }
            }
            for (std::size_t channel = 0; channel < 2; ++channel) {
                if (best_score[channel] < 0.0) {
                    std::cerr << "Channel " << channel << " produced no whistle delays\n";
                    return 1;
                }
                if (std::abs(best_delay[channel] - channel_1_delay_samples) > 0.5) {
                    std::cerr << "Best whistle delay mismatch on channel " << channel << ": expected about "
                              << channel_1_delay_samples << " samples, got " << best_delay[channel] << "\n";
                    return 1;
                }
            }
        }

        {
            pamguard::core::AnalysisSession session(base_config(false));
            auto result = session.process(synthetic_chunk());
            const auto flushed = session.flush();
            result.whistle_regions.insert(result.whistle_regions.end(),
                                          flushed.whistle_regions.begin(), flushed.whistle_regions.end());
            result.whistle_delays.insert(result.whistle_delays.end(),
                                         flushed.whistle_delays.begin(), flushed.whistle_delays.end());
            if (result.whistle_regions.empty()) {
                std::cerr << "Geometry-free whistle session should still detect regions\n";
                return 1;
            }
            if (!result.whistle_delays.empty()) {
                std::cerr << "Whistle delays should require hydrophone geometry\n";
                return 1;
            }
        }

        std::cout << "Session whistle delay wiring passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
