#include <cmath>
#include <iostream>
#include <stdexcept>

#include "pamguard/core/SessionManager.h"

namespace {

double synthetic_sample(std::size_t channel, std::size_t sample) {
    const double background = 0.01 * std::sin(static_cast<double>(sample) * 0.13 + static_cast<double>(channel) * 0.31);
    if (sample >= 80 && sample <= 86) {
        const double sign = (sample & 1u) == 0 ? 1.0 : -1.0;
        const double scale = channel == 0 ? 1.0 : 0.82;
        return background + sign * scale;
    }
    return background;
}

} // namespace

int main() {
    try {
        pamguard::core::AnalysisConfig config;
        config.session_id = "session-a";
        config.source_id = "synthetic-stream";
        config.sample_rate_hz = 48000;
        config.channel_count = 2;
        config.detector.fft.fft_length = 64;
        config.detector.fft.fft_hop = 32;
        config.detector.fft.channels = {0, 1};
        config.detector.click_detector_enabled = true;
        config.detector.click_localisation_enabled = true;
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

        pamguard::core::AudioChunk chunk;
        chunk.start_sample = 0;
        chunk.time_unix_ms = 0;
        chunk.sample_rate_hz = 48000;
        chunk.channel_count = 2;
        chunk.interleaved_pcm.resize(256 * chunk.channel_count);
        for (std::size_t sample = 0; sample < 256; ++sample) {
            for (std::size_t channel = 0; channel < chunk.channel_count; ++channel) {
                chunk.interleaved_pcm[sample * chunk.channel_count + channel] = synthetic_sample(channel, sample);
            }
        }

        pamguard::core::SessionManager manager;
        manager.create_session(config);
        const auto result = manager.process_audio("session-a", chunk);

        if (manager.session_count() != 1 || !manager.has_session("session-a")) {
            std::cerr << "Session registry state is incorrect\n";
            return 1;
        }
        if (result.spectrogram_frames.empty()) {
            std::cerr << "Expected spectrogram frames\n";
            return 1;
        }
        if (result.clicks.size() != 1 || result.clicks[0].start_sample != 71 || result.clicks[0].duration_samples != 43) {
            std::cerr << "Expected one PAMGuard-parity click at sample 71 duration 43\n";
            return 1;
        }
        if (result.click_localisations.size() != 1 || result.click_localisations[0].delays.size() != 1) {
            std::cerr << "Expected one click localisation with one channel-pair delay\n";
            return 1;
        }
        const auto flushed = manager.flush_session("session-a");
        if (!flushed.spectrogram_frames.empty() || !flushed.clicks.empty()) {
            std::cerr << "Flush should not synthesize spectrogram frames or clicks\n";
            return 1;
        }
        if (!manager.remove_session("session-a") || manager.session_count() != 0) {
            std::cerr << "Session removal failed\n";
            return 1;
        }

        std::cout << "Session manager check passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
