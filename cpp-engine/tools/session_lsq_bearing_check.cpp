#include <cmath>
#include <cstddef>
#include <iostream>

#include "pamguard/core/AnalysisSession.h"

namespace {

pamguard::core::AnalysisConfig base_config(std::size_t channel_count) {
    pamguard::core::AnalysisConfig config;
    config.session_id = "lsq-bearing-check";
    config.sample_rate_hz = 48000;
    config.channel_count = channel_count;

    config.detector.click_detector_enabled = true;
    config.detector.click_localisation_enabled = true;
    config.detector.click.channel_bitmap = (1u << channel_count) - 1u;
    config.detector.click.trigger_bitmap = (1u << channel_count) - 1u;
    config.detector.click.threshold_db = 10.0;
    config.detector.click.short_filter = 0.1;
    config.detector.click.long_filter = 0.00001;
    config.detector.click.pre_sample = 10;
    config.detector.click.post_sample = 12;
    config.detector.click.min_sep = 8;
    config.detector.click.max_length = 128;
    config.detector.click.min_trigger_channels = 1;

    config.array.speed_of_sound_mps = 1500.0;
    config.array.speed_of_sound_error_mps = 5.0;
    config.array.timing_error_seconds = 1.0e-5;
    config.array.spacing_error_m = 0.02;
    return config;
}

void add_tetrahedron_hydrophones(pamguard::core::AnalysisConfig& config) {
    config.array.hydrophones = {
        {0, 0.0, 0.0, 0.0, 0.0},
        {1, 2.5, 0.0, 0.0, 0.0},
        {2, 0.0, 2.5, 0.0, 0.0},
        {3, 0.0, 0.0, 2.5, 0.0},
    };
}

pamguard::core::AudioChunk synthetic_chunk(std::size_t channel_count) {
    pamguard::core::AudioChunk chunk;
    chunk.start_sample = 0;
    chunk.time_unix_ms = 0;
    chunk.sample_rate_hz = 48000;
    chunk.channel_count = channel_count;
    constexpr std::size_t frame_count = 1024;
    chunk.interleaved_pcm.assign(frame_count * channel_count, 0.0);
    for (std::size_t sample = 0; sample < frame_count; ++sample) {
        for (std::size_t channel = 0; channel < channel_count; ++channel) {
            const double background = 0.01 * std::sin(static_cast<double>(sample) * 0.13 + static_cast<double>(channel) * 0.31);
            double value = background;
            const std::size_t start = 80 + channel;
            if (sample >= start && sample <= start + 6) {
                const double sign = (sample & 1u) == 0 ? 1.0 : -1.0;
                value += sign * (1.0 - 0.05 * static_cast<double>(channel));
            }
            chunk.interleaved_pcm[sample * channel_count + channel] = value;
        }
    }
    return chunk;
}

} // namespace

int main() {
    try {
        {
            // Streamer offsets translate hydrophone coordinates, so a
            // streamer-relative layout must localise identically to the same
            // array expressed in absolute coordinates.
            auto absolute = base_config(4);
            add_tetrahedron_hydrophones(absolute);
            for (auto& hydrophone : absolute.array.hydrophones) {
                hydrophone.x_m += 100.0;
                hydrophone.y_m += 25.0;
            }
            pamguard::core::AnalysisSession absolute_session(absolute);
            const auto absolute_result = absolute_session.process(synthetic_chunk(4));

            auto streamed = base_config(4);
            add_tetrahedron_hydrophones(streamed);
            streamed.array.streamers = {{7, 100.0, 25.0, 0.0, 33.0}};
            for (auto& hydrophone : streamed.array.hydrophones) {
                hydrophone.streamer_id = 7;
            }
            pamguard::core::AnalysisSession streamed_session(streamed);
            const auto streamed_result = streamed_session.process(synthetic_chunk(4));

            if (absolute_result.click_localisations.empty() || streamed_result.click_localisations.empty()) {
                std::cerr << "Streamer offset comparison produced no localisations\n";
                return 1;
            }
            const auto& absolute_lsq = absolute_result.click_localisations.front().lsq_bearing;
            const auto& streamed_lsq = streamed_result.click_localisations.front().lsq_bearing;
            if (absolute_lsq.valid != streamed_lsq.valid ||
                std::abs(absolute_lsq.azimuth_radians - streamed_lsq.azimuth_radians) > 1e-12 ||
                std::abs(absolute_lsq.elevation_radians - streamed_lsq.elevation_radians) > 1e-12) {
                std::cerr << "Streamer-relative layout should localise identically to absolute coordinates\n";
                return 1;
            }
        }

        {
            auto config = base_config(4);
            add_tetrahedron_hydrophones(config);
            pamguard::core::AnalysisSession session(config);
            const auto result = session.process(synthetic_chunk(4));
            if (result.clicks.empty() || result.click_localisations.empty()) {
                std::cerr << "Four-channel session did not produce click localisations\n";
                return 1;
            }
            const auto& localisation = result.click_localisations.front();
            if (localisation.delays.size() != 6) {
                std::cerr << "Four-channel click should produce six delay pairs, got " << localisation.delays.size() << "\n";
                return 1;
            }
            for (const auto& delay : localisation.delays) {
                if (!delay.geometry_constrained || !delay.pair_bearing_valid) {
                    std::cerr << "Four-channel delay pair missing geometry constraint or pair bearing\n";
                    return 1;
                }
            }
            if (!localisation.lsq_bearing.valid || localisation.lsq_bearing.used_pairs != 6 ||
                !std::isfinite(localisation.lsq_bearing.azimuth_radians) ||
                !std::isfinite(localisation.lsq_bearing.elevation_radians)) {
                std::cerr << "Four-channel session did not produce a valid LSQ bearing\n";
                return 1;
            }
        }

        {
            auto config = base_config(4);
            add_tetrahedron_hydrophones(config);
            config.array.spacing_error_m = 0.0;
            pamguard::core::AnalysisSession session(config);
            const auto result = session.process(synthetic_chunk(4));
            if (result.click_localisations.empty() || result.click_localisations.front().lsq_bearing.valid) {
                std::cerr << "LSQ bearing should require spacingErrorM > 0 for PAMGuard fit weights\n";
                return 1;
            }
        }

        {
            auto config = base_config(2);
            config.array.hydrophones = {
                {0, 0.0, 0.0, 0.0, 0.0},
                {1, 2.5, 0.0, 0.0, 0.0},
            };
            pamguard::core::AnalysisSession session(config);
            const auto result = session.process(synthetic_chunk(2));
            if (result.click_localisations.empty()) {
                std::cerr << "Two-channel session did not produce click localisations\n";
                return 1;
            }
            const auto& localisation = result.click_localisations.front();
            if (localisation.lsq_bearing.valid) {
                std::cerr << "Two-channel session should not produce LSQ bearing output\n";
                return 1;
            }
            if (localisation.delays.empty() || !localisation.delays.front().pair_bearing_valid) {
                std::cerr << "Two-channel session should still produce pair bearing output\n";
                return 1;
            }
        }

        std::cout << "Session LSQ bearing wiring passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
