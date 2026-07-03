#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/core/AudioFrame.h"
#include "pamguard/detectors/ClickDetectorEngine.h"

namespace {

struct ClickRow {
    std::size_t index = 0;
    std::int64_t start_sample = 0;
    std::size_t duration = 0;
    std::uint32_t channel_bitmap = 0;
    std::uint32_t trigger_bitmap = 0;
    double signal_excess_db = 0.0;
    std::int64_t time_ms = 0;
};

double transient_sample(std::size_t sample, std::size_t start_sample, std::size_t end_sample, double scale) {
    if (sample < start_sample || sample > end_sample || scale == 0.0) {
        return 0.0;
    }
    const double sign = (sample & 1u) == 0 ? 1.0 : -1.0;
    return sign * scale;
}

double synthetic_sample(const std::string& scenario, std::size_t channel, std::size_t sample) {
    const double background = 0.01 * std::sin(static_cast<double>(sample) * 0.13 + static_cast<double>(channel) * 0.31);
    if (scenario == "single-transient") {
        return background + transient_sample(sample, 80, 86, channel == 0 ? 1.0 : 0.82);
    }
    if (scenario == "double-transient") {
        if (sample >= 60 && sample <= 66) {
            return background + transient_sample(sample, 60, 66, channel == 0 ? 1.0 : 0.82);
        }
        return background + transient_sample(sample, 90, 96, channel == 0 ? 1.0 : 0.82);
    }
    if (scenario == "long-transient") {
        return background + transient_sample(sample, 60, 140, channel == 0 ? 1.0 : 0.82);
    }
    if (scenario == "single-channel-transient") {
        return background + transient_sample(sample, 80, 86, channel == 0 ? 1.0 : 0.0);
    }
    throw std::invalid_argument("unknown scenario: " + scenario);
}

std::vector<ClickRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<ClickRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.find("index,startSample,duration,channelBitmap,triggerBitmap,signalExcessDb,timeMs") != std::string::npos) {
            continue;
        }

        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 7) {
            continue;
        }

        ClickRow row;
        row.index = static_cast<std::size_t>(std::stoull(cells[0]));
        row.start_sample = std::stoll(cells[1]);
        row.duration = static_cast<std::size_t>(std::stoull(cells[2]));
        row.channel_bitmap = static_cast<std::uint32_t>(std::stoul(cells[3]));
        row.trigger_bitmap = static_cast<std::uint32_t>(std::stoul(cells[4]));
        row.signal_excess_db = std::stod(cells[5]);
        row.time_ms = std::stoll(cells[6]);
        rows.push_back(row);
    }
    return rows;
}

std::vector<ClickRow> to_rows(const std::vector<pamguard::detectors::ClickDetectionResult>& detections) {
    std::vector<ClickRow> rows;
    rows.reserve(detections.size());
    for (std::size_t i = 0; i < detections.size(); ++i) {
        const auto& detection = detections[i];
        ClickRow row;
        row.index = i;
        row.start_sample = detection.start_sample;
        row.duration = detection.duration_samples;
        row.channel_bitmap = detection.channel_bitmap;
        row.trigger_bitmap = detection.trigger_bitmap;
        row.signal_excess_db = detection.signal_excess_db;
        row.time_ms = detection.time_unix_ms;
        rows.push_back(row);
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 15 && argc != 16) {
        std::cerr << "Usage: click_trigger_fixture_check <fixture.csv>"
                  << " [<channelBitmap> <triggerBitmap> <thresholdDb> <shortFilter> <longFilter>"
                  << " <preSample> <postSample> <minSep> <maxLength> <minTriggerChannels>"
                  << " <sampleRate> <chunkLength> <scenario> [expectedDetections]]\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);

        pamguard::detectors::ClickDetectorConfig config;
        config.channel_bitmap = 0x3;
        config.trigger_bitmap = 0x3;
        config.threshold_db = 10.0;
        config.short_filter = 0.1;
        config.long_filter = 0.00001;
        config.pre_sample = 10;
        config.post_sample = 12;
        config.min_sep = 8;
        config.max_length = 128;
        config.min_trigger_channels = 1;

        std::uint32_t sample_rate = 48000;
        std::size_t chunk_length = 256;
        std::string scenario = "single-transient";
        std::int64_t expected_detections = -1;
        if (argc >= 15) {
            int arg = 2;
            config.channel_bitmap = static_cast<std::uint32_t>(std::stoul(argv[arg++], nullptr, 0));
            config.trigger_bitmap = static_cast<std::uint32_t>(std::stoul(argv[arg++], nullptr, 0));
            config.threshold_db = std::stod(argv[arg++]);
            config.short_filter = std::stod(argv[arg++]);
            config.long_filter = std::stod(argv[arg++]);
            config.pre_sample = static_cast<std::size_t>(std::stoull(argv[arg++]));
            config.post_sample = static_cast<std::size_t>(std::stoull(argv[arg++]));
            config.min_sep = static_cast<std::size_t>(std::stoull(argv[arg++]));
            config.max_length = static_cast<std::size_t>(std::stoull(argv[arg++]));
            config.min_trigger_channels = static_cast<std::size_t>(std::stoull(argv[arg++]));
            sample_rate = static_cast<std::uint32_t>(std::stoul(argv[arg++]));
            chunk_length = static_cast<std::size_t>(std::stoull(argv[arg++]));
            scenario = argv[arg++];
            if (argc == 16) {
                expected_detections = std::stoll(argv[arg]);
            }
        }

        if (expected_detections >= 0 && fixture.size() != static_cast<std::size_t>(expected_detections)) {
            std::cerr << "Fixture does not encode the expected scenario behaviour: expected "
                      << expected_detections << " detections, fixture has " << fixture.size() << "\n";
            return 1;
        }
        for (const auto& row : fixture) {
            if (row.duration > config.max_length) {
                std::cerr << "Fixture violates max_length invariant at detection " << row.index << "\n";
                return 1;
            }
        }

        bool rejected_empty_bitmap = false;
        try {
            auto bad_config = config;
            bad_config.channel_bitmap = 0;
            pamguard::detectors::ClickDetectorEngine bad_engine(bad_config);
            (void)bad_engine;
        }
        catch (const std::invalid_argument&) {
            rejected_empty_bitmap = true;
        }
        if (!rejected_empty_bitmap) {
            std::cerr << "Click detector should reject empty channel bitmap\n";
            return 1;
        }

        bool rejected_bad_filter = false;
        try {
            auto bad_config = config;
            bad_config.short_filter = 1.5;
            pamguard::detectors::ClickDetectorEngine bad_engine(bad_config);
            (void)bad_engine;
        }
        catch (const std::invalid_argument&) {
            rejected_bad_filter = true;
        }
        if (!rejected_bad_filter) {
            std::cerr << "Click detector should reject bad filter alpha\n";
            return 1;
        }

        bool rejected_zero_max_length = false;
        try {
            auto bad_config = config;
            bad_config.max_length = 0;
            pamguard::detectors::ClickDetectorEngine bad_engine(bad_config);
            (void)bad_engine;
        }
        catch (const std::invalid_argument&) {
            rejected_zero_max_length = true;
        }
        if (!rejected_zero_max_length) {
            std::cerr << "Click detector should reject zero max length\n";
            return 1;
        }

        pamguard::core::AudioChunk chunk;
        chunk.start_sample = 0;
        chunk.time_unix_ms = 0;
        chunk.sample_rate_hz = sample_rate;
        chunk.channel_count = 2;
        chunk.interleaved_pcm.resize(chunk_length * chunk.channel_count);
        for (std::size_t sample = 0; sample < chunk_length; ++sample) {
            for (std::size_t channel = 0; channel < chunk.channel_count; ++channel) {
                chunk.interleaved_pcm[sample * chunk.channel_count + channel] = synthetic_sample(scenario, channel, sample);
            }
        }

        pamguard::detectors::ClickDetectorEngine engine(config);
        bool rejected_bad_chunk = false;
        try {
            pamguard::core::AudioChunk bad_chunk = chunk;
            bad_chunk.sample_rate_hz = 0;
            (void)engine.process(bad_chunk);
        }
        catch (const std::invalid_argument&) {
            rejected_bad_chunk = true;
        }
        if (!rejected_bad_chunk) {
            std::cerr << "Click detector should reject chunk without sample rate\n";
            return 1;
        }

        bool rejected_missing_channel = false;
        try {
            auto missing_config = config;
            missing_config.channel_bitmap = 0x4;
            pamguard::detectors::ClickDetectorEngine missing_engine(missing_config);
            (void)missing_engine.process(chunk);
        }
        catch (const std::invalid_argument&) {
            rejected_missing_channel = true;
        }
        if (!rejected_missing_channel) {
            std::cerr << "Click detector should reject chunks missing configured detector channels\n";
            return 1;
        }

        auto gated_config = config;
        gated_config.trigger_bitmap = 0x1;
        gated_config.min_trigger_channels = 2;
        pamguard::detectors::ClickDetectorEngine gated_engine(gated_config);
        if (!gated_engine.process(chunk).empty()) {
            std::cerr << "Click detector trigger gate should suppress detections below min trigger channels\n";
            return 1;
        }

        const auto detections = engine.process(chunk);
        if (!detections.empty()) {
            const auto& first = detections.front();
            if (first.channels.size() != 2 || first.waveform.size() != 2 ||
                first.waveform[0].size() != first.duration_samples ||
                first.waveform[1].size() != first.duration_samples) {
                std::cerr << "Click waveform capture failed\n";
                return 1;
            }
        }

        const auto actual = to_rows(detections);
        engine.reset();
        const auto after_reset = to_rows(engine.process(chunk));
        if (after_reset.size() != actual.size()) {
            std::cerr << "Click detector reset changed detection count\n";
            return 1;
        }
        if (actual.size() != fixture.size()) {
            std::cerr << "Detection count mismatch: fixture=" << fixture.size() << " actual=" << actual.size() << "\n";
            return 1;
        }

        double max_abs_error = 0.0;
        std::size_t max_index = 0;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const auto& got = actual[i];
            const auto& expected = fixture[i];
            if (after_reset[i].start_sample != got.start_sample || after_reset[i].duration != got.duration ||
                after_reset[i].trigger_bitmap != got.trigger_bitmap) {
                std::cerr << "Click detector reset reproducibility mismatch at detection " << i << "\n";
                return 1;
            }
            if (got.index != expected.index || got.start_sample != expected.start_sample || got.duration != expected.duration ||
                got.channel_bitmap != expected.channel_bitmap || got.trigger_bitmap != expected.trigger_bitmap || got.time_ms != expected.time_ms) {
                std::cerr << "Metadata mismatch at detection " << i << "\n";
                std::cerr << "expected index/start/duration/channel/trigger/time="
                          << expected.index << "/" << expected.start_sample << "/" << expected.duration << "/"
                          << expected.channel_bitmap << "/" << expected.trigger_bitmap << "/" << expected.time_ms << "\n";
                std::cerr << "actual   index/start/duration/channel/trigger/time="
                          << got.index << "/" << got.start_sample << "/" << got.duration << "/"
                          << got.channel_bitmap << "/" << got.trigger_bitmap << "/" << got.time_ms << "\n";
                return 1;
            }

            const double error = std::abs(got.signal_excess_db - expected.signal_excess_db);
            if (error > max_abs_error) {
                max_abs_error = error;
                max_index = i;
            }
        }

        constexpr double tolerance = 1e-10;
        if (max_abs_error > tolerance) {
            std::cerr << "Click trigger parity failed\n";
            std::cerr << "max_abs_error=" << max_abs_error << " at detection " << max_index << "\n";
            return 1;
        }

        std::cout << "Click trigger parity passed\n";
        std::cout << "scenario=" << scenario << " detections=" << actual.size() << "\n";
        std::cout << "max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
