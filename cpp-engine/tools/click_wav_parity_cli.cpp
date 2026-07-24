#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/core/AudioFrame.h"
#include "pamguard/detectors/ClickDetectorEngine.h"
#include "pamguard/io/WavReader.h"

namespace {

void write_clicks(
    const std::filesystem::path& output,
    const std::vector<pamguard::detectors::ClickDetectionResult>& clicks,
    std::uint32_t sample_rate,
    std::size_t block_samples) {
    std::ofstream stream(output);
    if (!stream) {
        throw std::runtime_error("could not open click output: " + output.string());
    }
    stream << std::setprecision(17);
    stream << "index,startSample,duration,channelBitmap,triggerBitmap,"
              "signalExcessDb,sampleRate,blockSamples\n";
    for (std::size_t i = 0; i < clicks.size(); ++i) {
        const auto& click = clicks[i];
        stream << i << ','
               << click.start_sample << ','
               << click.duration_samples << ','
               << click.channel_bitmap << ','
               << click.trigger_bitmap << ','
               << click.signal_excess_db << ','
               << sample_rate << ','
               << block_samples << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 4 && argc != 5) {
            std::cerr
                << "Usage: click_wav_parity_cli <input.wav> <clicks.csv> "
                   "[trace.csv] [blockSamples]\n";
            return 2;
        }

        const std::filesystem::path input_path(argv[1]);
        const std::filesystem::path click_output(argv[2]);
        const bool write_trace = argc >= 4;

        pamguard::io::WavReader reader;
        auto wav = reader.read_all(input_path);
        if (wav.channel_count != 1) {
            throw std::invalid_argument("parity CLI currently requires a mono WAV");
        }
        const std::size_t block_samples = argc == 5
            ? static_cast<std::size_t>(std::stoull(argv[4]))
            : std::max<std::size_t>(wav.sample_rate_hz / 10, 500);
        if (block_samples == 0) {
            throw std::invalid_argument("blockSamples must be non-zero");
        }

        pamguard::detectors::ClickDetectorConfig config;
        config.channel_bitmap = 1;
        config.trigger_bitmap = 1;
        config.sample_noise = false;
        config.store_background = false;
        config.publish_trigger_function = write_trace;
        pamguard::detectors::ClickDetectorEngine detector(config);

        std::ofstream trace;
        if (write_trace) {
            trace.open(argv[3]);
            if (!trace) {
                throw std::runtime_error(
                    "could not open trace output: " + std::string(argv[3]));
            }
            trace << std::setprecision(17);
            trace << "sample,signalExcessDb,overThreshold\n";
        }

        const auto frame_total = wav.interleaved_pcm.size();
        std::vector<pamguard::detectors::ClickDetectionResult> clicks;
        for (std::size_t start = 0; start < frame_total; start += block_samples) {
            const auto count = std::min(block_samples, frame_total - start);
            pamguard::core::AudioChunk chunk;
            chunk.start_sample = start;
            chunk.time_unix_ms = static_cast<std::int64_t>(
                static_cast<double>(start) * 1000.0 / wav.sample_rate_hz);
            chunk.sample_rate_hz = wav.sample_rate_hz;
            chunk.channel_count = 1;
            chunk.interleaved_pcm.assign(
                wav.interleaved_pcm.begin() + static_cast<std::ptrdiff_t>(start),
                wav.interleaved_pcm.begin()
                    + static_cast<std::ptrdiff_t>(start + count));

            auto chunk_clicks = detector.process(chunk);
            clicks.insert(
                clicks.end(),
                std::make_move_iterator(chunk_clicks.begin()),
                std::make_move_iterator(chunk_clicks.end()));

            if (write_trace) {
                const auto& products = detector.trigger_function();
                if (products.size() != 1
                    || products[0].signal_excess_db.size() != 1
                    || products[0].signal_excess_db[0].size() != count) {
                    throw std::runtime_error(
                        "click detector returned an unexpected trigger trace shape");
                }
                const auto& values = products[0].signal_excess_db[0];
                for (std::size_t i = 0; i < values.size(); ++i) {
                    trace << start + i << ','
                          << values[i] << ','
                          << (values[i] > config.threshold_db ? 1 : 0) << '\n';
                }
            }
        }

        write_clicks(click_output, clicks, wav.sample_rate_hz, block_samples);
        std::cout << "clicks=" << clicks.size()
                  << " frames=" << frame_total
                  << " sampleRate=" << wav.sample_rate_hz
                  << " blockSamples=" << block_samples << '\n';
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
