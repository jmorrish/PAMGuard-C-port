#include <cmath>
#include <cstddef>
#include <iostream>

#include "pamguard/core/AnalysisSession.h"

namespace {

constexpr double sample_rate_hz = 48000.0;

pamguard::core::AnalysisConfig base_config(bool mht) {
    pamguard::core::AnalysisConfig config;
    config.session_id = "mht-train-check";
    config.sample_rate_hz = static_cast<std::uint32_t>(sample_rate_hz);
    config.channel_count = 2;

    config.detector.click_detector_enabled = true;
    config.detector.click.channel_bitmap = 0x3;
    config.detector.click.trigger_bitmap = 0x3;
    config.detector.click.threshold_db = 10.0;
    config.detector.click.short_filter = 0.1;
    config.detector.click.long_filter = 0.00001;
    config.detector.click.pre_sample = 10;
    config.detector.click.post_sample = 12;
    config.detector.click.min_sep = 8;
    config.detector.click.max_length = 128;
    config.detector.click.min_trigger_channels = 1;

    config.detector.click_train_tracker_enabled = true;
    config.detector.click_train_mht = mht;
    config.detector.click_train.max_ici_seconds = 0.5;
    config.detector.click_train.min_clicks = 3;
    return config;
}

pamguard::core::AudioChunk click_train_chunk() {
    // Eight clicks 100 ms apart: transients at 4800*k + 4000 for k = 0..7.
    constexpr std::size_t frame_count = 48000;
    pamguard::core::AudioChunk chunk;
    chunk.start_sample = 0;
    chunk.time_unix_ms = 0;
    chunk.sample_rate_hz = static_cast<std::uint32_t>(sample_rate_hz);
    chunk.channel_count = 2;
    chunk.interleaved_pcm.assign(frame_count * 2, 0.0);
    for (std::size_t i = 0; i < frame_count; ++i) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            double value = 0.01 * std::sin(static_cast<double>(i) * 0.13 + static_cast<double>(channel) * 0.31);
            const std::size_t position = (i >= 4000) ? (i - 4000) % 4800 : 4800;
            if (position <= 6 && i >= 4000 && i < 4000 + 8 * 4800) {
                const double sign = (i & 1u) == 0 ? 1.0 : -1.0;
                value += sign * (channel == 0 ? 1.0 : 0.82);
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
            auto result = session.process(click_train_chunk());
            if (result.clicks.size() != 8) {
                std::cerr << "Expected eight detected clicks, got " << result.clicks.size() << "\n";
                return 1;
            }
            if (!result.click_trains.empty()) {
                std::cerr << "MHT mode should not produce ICI-tracker clickTrains\n";
                return 1;
            }
            const auto flushed = session.flush();
            result.mht_click_trains.insert(result.mht_click_trains.end(),
                                           flushed.mht_click_trains.begin(), flushed.mht_click_trains.end());
            if (result.mht_click_trains.empty()) {
                std::cerr << "MHT mode produced no confirmed click trains after flush\n";
                return 1;
            }
            std::size_t best_count = 0;
            for (const auto& train : result.mht_click_trains) {
                best_count = std::max(best_count, train.click_count);
                if (train.click_count < 3 || train.channel_bitmap != 0x3 ||
                    train.click_start_samples.size() != train.click_count) {
                    std::cerr << "Malformed MHT click train output\n";
                    return 1;
                }
            }
            if (best_count < 6) {
                std::cerr << "Best MHT train should contain most of the eight clicks, got " << best_count << "\n";
                return 1;
            }
        }

        {
            pamguard::core::AnalysisSession session(base_config(false));
            auto result = session.process(click_train_chunk());
            const auto flushed = session.flush();
            result.click_trains.insert(result.click_trains.end(),
                                       flushed.click_trains.begin(), flushed.click_trains.end());
            result.mht_click_trains.insert(result.mht_click_trains.end(),
                                           flushed.mht_click_trains.begin(), flushed.mht_click_trains.end());
            if (!result.mht_click_trains.empty()) {
                std::cerr << "ICI mode should not produce MHT click trains\n";
                return 1;
            }
            if (result.click_trains.empty()) {
                std::cerr << "ICI mode should still produce clickTrains\n";
                return 1;
            }
        }

        std::cout << "Session MHT click train wiring passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
