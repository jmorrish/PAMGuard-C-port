#include "pamguard/io/WavReader.h"

#include <array>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace pamguard::io {

namespace {

template <typename T>
T read_le(std::istream& input) {
    T value{};
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!input) {
        throw std::runtime_error("unexpected end of WAV file");
    }
    return value;
}

std::string read_tag(std::istream& input) {
    std::array<char, 4> tag{};
    input.read(tag.data(), tag.size());
    if (!input) {
        throw std::runtime_error("unexpected end of WAV file while reading chunk tag");
    }
    return std::string(tag.data(), tag.size());
}

void skip_bytes(std::istream& input, std::uint32_t count) {
    input.seekg(count, std::ios::cur);
    if (!input) {
        throw std::runtime_error("failed to skip WAV chunk bytes");
    }
}

double read_pcm_sample(const std::vector<unsigned char>& data, std::size_t offset, std::uint16_t bits_per_sample) {
    switch (bits_per_sample) {
    case 8: {
        const auto value = static_cast<int>(data[offset]);
        return (static_cast<double>(value) - 128.0) / 128.0;
    }
    case 16: {
        std::int16_t value = 0;
        std::memcpy(&value, data.data() + offset, sizeof(value));
        return static_cast<double>(value) / 32768.0;
    }
    case 24: {
        std::int32_t value = static_cast<std::int32_t>(data[offset]) |
                             (static_cast<std::int32_t>(data[offset + 1]) << 8) |
                             (static_cast<std::int32_t>(data[offset + 2]) << 16);
        if ((value & 0x00800000) != 0) {
            value |= static_cast<std::int32_t>(0xFF000000);
        }
        return static_cast<double>(value) / 8388608.0;
    }
    case 32: {
        std::int32_t value = 0;
        std::memcpy(&value, data.data() + offset, sizeof(value));
        return static_cast<double>(value) / 2147483648.0;
    }
    default:
        throw std::runtime_error("unsupported PCM bit depth");
    }
}

double read_float_sample(const std::vector<unsigned char>& data, std::size_t offset, std::uint16_t bits_per_sample) {
    switch (bits_per_sample) {
    case 32: {
        float value = 0.0F;
        std::memcpy(&value, data.data() + offset, sizeof(value));
        return static_cast<double>(value);
    }
    case 64: {
        double value = 0.0;
        std::memcpy(&value, data.data() + offset, sizeof(value));
        return value;
    }
    default:
        throw std::runtime_error("unsupported IEEE float bit depth");
    }
}

} // namespace

WavData WavReader::read_all(const std::filesystem::path& path) const {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not open WAV file: " + path.string());
    }

    if (read_tag(input) != "RIFF") {
        throw std::runtime_error("WAV file is missing RIFF tag");
    }
    (void)read_le<std::uint32_t>(input);
    if (read_tag(input) != "WAVE") {
        throw std::runtime_error("WAV file is missing WAVE tag");
    }

    bool have_fmt = false;
    bool have_data = false;
    std::uint16_t audio_format = 0;
    std::uint16_t channel_count = 0;
    std::uint32_t sample_rate_hz = 0;
    std::uint16_t block_align = 0;
    std::uint16_t bits_per_sample = 0;
    std::vector<unsigned char> raw_data;

    while (input.peek() != std::char_traits<char>::eof()) {
        const auto tag = read_tag(input);
        const auto chunk_size = read_le<std::uint32_t>(input);

        if (tag == "fmt ") {
            audio_format = read_le<std::uint16_t>(input);
            channel_count = read_le<std::uint16_t>(input);
            sample_rate_hz = read_le<std::uint32_t>(input);
            (void)read_le<std::uint32_t>(input);
            block_align = read_le<std::uint16_t>(input);
            bits_per_sample = read_le<std::uint16_t>(input);
            if (chunk_size > 16) {
                skip_bytes(input, chunk_size - 16);
            }
            have_fmt = true;
        }
        else if (tag == "data") {
            raw_data.resize(chunk_size);
            input.read(reinterpret_cast<char*>(raw_data.data()), raw_data.size());
            if (!input) {
                throw std::runtime_error("failed to read WAV data chunk");
            }
            have_data = true;
        }
        else {
            skip_bytes(input, chunk_size);
        }

        if ((chunk_size & 1U) != 0U) {
            skip_bytes(input, 1);
        }
    }

    if (!have_fmt || !have_data) {
        throw std::runtime_error("WAV file must contain fmt and data chunks");
    }
    if (channel_count == 0 || sample_rate_hz == 0 || block_align == 0) {
        throw std::runtime_error("invalid WAV format metadata");
    }
    if (audio_format != 1 && audio_format != 3) {
        throw std::runtime_error("only PCM and IEEE float WAV files are supported");
    }

    const std::size_t bytes_per_sample = bits_per_sample / 8;
    const std::size_t expected_block_align = bytes_per_sample * channel_count;
    if (bytes_per_sample == 0 || expected_block_align != block_align) {
        throw std::runtime_error("unsupported or inconsistent WAV block alignment");
    }

    const std::size_t sample_count = raw_data.size() / bytes_per_sample;
    WavData wav;
    wav.sample_rate_hz = sample_rate_hz;
    wav.channel_count = channel_count;
    wav.bits_per_sample = bits_per_sample;
    wav.format_code = audio_format;
    wav.interleaved_pcm.reserve(sample_count);

    for (std::size_t offset = 0; offset + bytes_per_sample <= raw_data.size(); offset += bytes_per_sample) {
        if (audio_format == 1) {
            wav.interleaved_pcm.push_back(read_pcm_sample(raw_data, offset, bits_per_sample));
        }
        else {
            wav.interleaved_pcm.push_back(read_float_sample(raw_data, offset, bits_per_sample));
        }
    }

    return wav;
}

core::AudioChunk WavReader::read_all_as_chunk(const std::filesystem::path& path, std::int64_t time_unix_ms) const {
    const auto wav = read_all(path);
    core::AudioChunk chunk;
    chunk.start_sample = 0;
    chunk.time_unix_ms = time_unix_ms;
    chunk.sample_rate_hz = wav.sample_rate_hz;
    chunk.channel_count = wav.channel_count;
    chunk.interleaved_pcm = std::move(wav.interleaved_pcm);
    return chunk;
}

} // namespace pamguard::io

