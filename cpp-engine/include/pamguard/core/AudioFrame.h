#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pamguard::core {

struct AudioChunk {
    std::uint64_t start_sample = 0;
    std::int64_t time_unix_ms = 0;
    std::uint32_t sample_rate_hz = 0;
    std::size_t channel_count = 0;
    std::vector<double> interleaved_pcm;
    /**
     * Optional array attitude for this chunk, overriding the session's static
     * `array.orientation` — attitude sampled at chunk cadence, for a source
     * that measures heading between chunks. PAMGuard attaches GPS attitude
     * per data unit; a chunk is the engine's ingest granularity, and geometry
     * itself stays static either way.
     */
    bool orientation_declared = false;
    double orientation_heading_degrees = 0.0;
    double orientation_pitch_degrees = 0.0;
    double orientation_roll_degrees = 0.0;

    [[nodiscard]] std::size_t frame_count() const {
        return channel_count == 0 ? 0 : interleaved_pcm.size() / channel_count;
    }

    [[nodiscard]] double sample(std::size_t frame, std::size_t channel) const {
        return interleaved_pcm[frame * channel_count + channel];
    }
};

} // namespace pamguard::core

