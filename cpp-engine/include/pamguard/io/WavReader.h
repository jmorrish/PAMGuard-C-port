#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "pamguard/core/AudioFrame.h"

namespace pamguard::io {

struct WavData {
    std::uint32_t sample_rate_hz = 0;
    std::uint16_t channel_count = 0;
    std::uint16_t bits_per_sample = 0;
    std::uint16_t format_code = 0;
    std::vector<double> interleaved_pcm;
};

class WavReader {
public:
    WavData read_all(const std::filesystem::path& path) const;
    core::AudioChunk read_all_as_chunk(const std::filesystem::path& path, std::int64_t time_unix_ms = 0) const;
};

} // namespace pamguard::io

