#include <any>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "pamguard/core/SignalNodes.h"

namespace {

constexpr std::uint32_t kInputRate = 48000;
constexpr std::uint32_t kOutputRate = 12000;
constexpr std::uint32_t kFractionalOutputRate = 19200;
constexpr std::uint32_t kUpsampleInputRate = 12000;
constexpr std::uint32_t kUpsampleOutputRate = 48000;
constexpr std::size_t kChannels = 2;
constexpr std::size_t kChunkSamples = 32;

using Key = std::tuple<std::string, int, int>;

struct ExpectedChunk {
    int interpolation = 0;
    std::uint64_t start_sample = 0;
    std::vector<double> samples;
};

std::map<Key, ExpectedChunk> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }
    std::map<Key, ExpectedChunk> expected;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.rfind("case,", 0) == 0) {
            continue;
        }
        std::stringstream stream(line);
        std::vector<std::string> fields;
        std::string field;
        while (std::getline(stream, field, ',')) {
            fields.push_back(field);
        }
        if (fields.size() != 7) {
            throw std::runtime_error("invalid decimator fixture row");
        }
        const auto key = Key{
            fields[0],
            std::stoi(fields[2]),
            std::stoi(fields[3]),
        };
        auto& chunk = expected[key];
        chunk.interpolation = std::stoi(fields[1]);
        chunk.start_sample = std::stoull(fields[4]);
        chunk.samples.push_back(std::stod(fields[6]));
    }
    return expected;
}

struct FixtureCase {
    std::string name;
    std::uint32_t output_rate = kOutputRate;
    int interpolation = 0;
    pamguard::dsp::IirFilterParams filter;
    std::uint32_t input_rate = kInputRate;
    std::size_t chunks = 2;
    bool stream_only = false;
};

std::vector<FixtureCase> fixture_cases() {
    using Band = pamguard::dsp::IirFilterBand;
    using Type = pamguard::dsp::IirFilterType;
    std::vector<FixtureCase> cases;
    for (int interpolation = 0; interpolation <= 2; ++interpolation) {
        cases.push_back({
            "default-" + std::to_string(interpolation),
            kOutputRate,
            interpolation,
            pamguard::core::default_decimator_filter_params(kOutputRate),
        });
    }
    for (int interpolation = 0; interpolation <= 2; ++interpolation) {
        cases.push_back({
            "fractional-" + std::to_string(interpolation),
            kFractionalOutputRate,
            interpolation,
            pamguard::core::default_decimator_filter_params(
                kFractionalOutputRate),
        });
    }

    auto chebyshev =
        pamguard::core::default_decimator_filter_params(kOutputRate);
    chebyshev.type = Type::Chebyshev;
    chebyshev.order = 4;
    chebyshev.low_pass_freq_hz = 4200.0F;
    chebyshev.pass_band_ripple_db = 0.75;
    chebyshev.stop_band_ripple_db = 9.5;
    chebyshev.cheby_gamma = 4.25;
    cases.push_back({
        "chebyshev-custom",
        kOutputRate,
        1,
        std::move(chebyshev),
    });

    auto fir_window =
        pamguard::core::default_decimator_filter_params(kOutputRate);
    fir_window.type = Type::FirWindow;
    fir_window.band = Band::BandPass;
    fir_window.order = 5;
    fir_window.high_pass_freq_hz = 1200.0F;
    fir_window.low_pass_freq_hz = 4800.0F;
    fir_window.pass_band_ripple_db = 1.25;
    fir_window.stop_band_ripple_db = 11.0;
    fir_window.cheby_gamma = 4.0;
    cases.push_back({
        "fir-window-custom",
        kOutputRate,
        0,
        std::move(fir_window),
    });

    auto fir_arbitrary =
        pamguard::core::default_decimator_filter_params(kOutputRate);
    fir_arbitrary.type = Type::FirArbitrary;
    fir_arbitrary.band = Band::BandPass;
    fir_arbitrary.order = 5;
    fir_arbitrary.pass_band_ripple_db = 1.5;
    fir_arbitrary.stop_band_ripple_db = 12.0;
    fir_arbitrary.cheby_gamma = 3.5;
    fir_arbitrary.arbitrary_frequencies_hz =
        {0.0, 1500.0, 3000.0, 12000.0, 15000.0, 24000.0};
    fir_arbitrary.arbitrary_gains_db =
        {-60.0, -60.0, 0.0, 0.0, -60.0, -60.0};
    cases.push_back({
        "fir-arbitrary-custom",
        kOutputRate,
        2,
        std::move(fir_arbitrary),
    });

    auto fft =
        pamguard::core::default_decimator_filter_params(kOutputRate);
    fft.type = Type::Fft;
    fft.band = Band::BandPass;
    fft.order = 4;
    fft.high_pass_freq_hz = 1500.0F;
    fft.low_pass_freq_hz = 5000.0F;
    fft.pass_band_ripple_db = 1.75;
    fft.stop_band_ripple_db = 13.0;
    fft.cheby_gamma = 4.5;
    cases.push_back({
        "fft-custom",
        kOutputRate,
        1,
        std::move(fft),
    });

    auto none =
        pamguard::core::default_decimator_filter_params(kOutputRate);
    none.type = Type::None;
    none.stop_band_ripple_db = 14.0;
    cases.push_back({
        "none-custom",
        kOutputRate,
        2,
        std::move(none),
    });

    auto upsample = pamguard::core::default_decimator_filter_params(
        kUpsampleOutputRate);
    upsample.type = Type::Chebyshev;
    upsample.order = 4;
    upsample.low_pass_freq_hz = 5000.0F;
    upsample.pass_band_ripple_db = 0.75;
    upsample.stop_band_ripple_db = 8.5;
    upsample.cheby_gamma = 4.75;
    cases.push_back({
        "upsample-chebyshev",
        kUpsampleOutputRate,
        2,
        std::move(upsample),
        kUpsampleInputRate,
        3,
        true,
    });
    return cases;
}

double signal(
    std::size_t chunk,
    std::size_t channel,
    std::size_t index,
    std::uint32_t input_rate) {
    const auto sample = chunk * kChunkSamples + index;
    constexpr double pi = 3.141592653589793238462643383279502884;
    return 0.6 * std::sin(2.0 * pi * 900.0 * static_cast<double>(sample) / input_rate) +
           0.25 * std::sin(2.0 * pi * 9000.0 * static_cast<double>(sample) / input_rate) +
           (sample == 11 + channel * 3 ? 0.8 : 0.0) +
           static_cast<double>(channel) * 0.1;
}

std::shared_ptr<pamguard::core::DataBlock> audio_block(
    std::string id,
    std::string producer,
    std::uint32_t sample_rate) {
    return std::make_shared<pamguard::core::DataBlock>(
        pamguard::core::DataBlockDescriptor{
            std::move(id),
            "Decimator fixture audio",
            std::move(producer),
            "audio",
            pamguard::core::kRawAudioDataType,
            1,
            static_cast<double>(sample_rate),
            0x3,
        });
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: decimator_fixture_check <fixture.csv>\n";
        return 2;
    }
    try {
        const auto expected = read_fixture(argv[1]);
        double maximum_error = 0.0;
        std::size_t checked = 0;
        for (const auto& fixture_case : fixture_cases()) {
            auto input = audio_block(
                "raw",
                "source",
                fixture_case.input_rate);
            auto output = audio_block(
                "decimated",
                "decimator",
                fixture_case.output_rate);
            pamguard::core::AudioSourceNode source(
                "source",
                {false, 1.0},
                input);
            pamguard::core::DecimatorNodeConfig config;
            config.output_sample_rate_hz =
                static_cast<double>(fixture_case.output_rate);
            config.filter = fixture_case.filter;
            config.interpolation = fixture_case.interpolation;
            config.channel_bitmap = 0x3;
            pamguard::core::DecimatorNode decimator(
                "decimator",
                std::move(config),
                input,
                output);
            int output_chunk = 0;
            std::vector<std::vector<double>> actual_stream(kChannels);
            std::uint64_t actual_stream_start_sample = 0;
            bool actual_stream_started = false;
            auto observer = output->subscribe([&](const pamguard::core::DataUnit& unit) {
                const auto* audio = std::any_cast<pamguard::core::AudioChunk>(&unit.payload);
                if (audio == nullptr) {
                    throw std::runtime_error("decimator output payload is not audio");
                }
                if (!audio->navigation_origin_declared ||
                    audio->navigation_origin_east_metres !=
                        100.0 + output_chunk ||
                    audio->navigation_origin_north_metres != 200.0 ||
                    audio->navigation_origin_height_metres != -4.0 ||
                    audio->navigation_reference_id !=
                        "decimator-navigation") {
                    throw std::runtime_error(
                        "decimator did not preserve navigation metadata");
                }
                if (fixture_case.stream_only) {
                    if (!actual_stream_started) {
                        actual_stream_start_sample = audio->start_sample;
                        actual_stream_started = true;
                    }
                    for (std::size_t channel = 0;
                         channel < kChannels;
                         ++channel) {
                        for (std::size_t frame = 0;
                             frame < audio->frame_count();
                             ++frame) {
                            actual_stream[channel].push_back(
                                audio->sample(frame, channel));
                        }
                    }
                    ++output_chunk;
                    return;
                }
                for (std::size_t channel = 0; channel < kChannels; ++channel) {
                    const auto found = expected.find(
                        Key{
                            fixture_case.name,
                            output_chunk,
                            static_cast<int>(channel)});
                    if (found == expected.end()) {
                        throw std::runtime_error("fixture is missing an expected output channel");
                    }
                    if (found->second.interpolation !=
                        fixture_case.interpolation) {
                        throw std::runtime_error(
                            "fixture interpolation metadata is inconsistent");
                    }
                    if (audio->start_sample != found->second.start_sample ||
                        audio->frame_count() != found->second.samples.size()) {
                        throw std::runtime_error("decimator output timing or length differs from Java");
                    }
                    for (std::size_t frame = 0; frame < audio->frame_count(); ++frame) {
                        const auto error = std::abs(
                            audio->sample(frame, channel) -
                            found->second.samples[frame]);
                        maximum_error = std::max(maximum_error, error);
                        if (error > 1e-8) {
                            throw std::runtime_error(
                                "decimator sample differs from Java fixture");
                        }
                        ++checked;
                    }
                }
                ++output_chunk;
            });
            source.prepare();
            decimator.prepare();
            decimator.start();
            source.start();
            for (std::size_t chunk = 0;
                 chunk < fixture_case.chunks;
                 ++chunk) {
                pamguard::core::AudioChunk audio;
                audio.start_sample = chunk * kChunkSamples;
                audio.time_unix_ms = 1000 +
                    static_cast<std::int64_t>(
                        audio.start_sample * 1000 /
                        fixture_case.input_rate);
                audio.sample_rate_hz = fixture_case.input_rate;
                audio.channel_count = kChannels;
                audio.navigation_origin_declared = true;
                audio.navigation_origin_east_metres =
                    100.0 + static_cast<double>(chunk);
                audio.navigation_origin_north_metres = 200.0;
                audio.navigation_origin_height_metres = -4.0;
                audio.navigation_reference_id =
                    "decimator-navigation";
                for (std::size_t frame = 0; frame < kChunkSamples; ++frame) {
                    for (std::size_t channel = 0; channel < kChannels; ++channel) {
                        audio.interleaved_pcm.push_back(signal(
                            chunk,
                            channel,
                            frame,
                            fixture_case.input_rate));
                    }
                }
                source.ingest(std::move(audio));
            }
            if (!fixture_case.stream_only &&
                output_chunk != static_cast<int>(fixture_case.chunks)) {
                throw std::runtime_error(
                    "decimator did not emit both fixture chunks for " +
                    fixture_case.name);
            }
            if (fixture_case.stream_only) {
                for (std::size_t channel = 0;
                     channel < kChannels;
                     ++channel) {
                    std::vector<double> expected_stream;
                    std::uint64_t expected_start_sample = 0;
                    bool expected_started = false;
                    for (std::size_t chunk = 0;
                         chunk < fixture_case.chunks;
                         ++chunk) {
                        const auto found = expected.find(Key{
                            fixture_case.name,
                            static_cast<int>(chunk),
                            static_cast<int>(channel)});
                        if (found == expected.end()) {
                            continue;
                        }
                        if (!expected_started) {
                            expected_start_sample =
                                found->second.start_sample;
                            expected_started = true;
                        }
                        expected_stream.insert(
                            expected_stream.end(),
                            found->second.samples.begin(),
                            found->second.samples.end());
                    }
                    if (!expected_started ||
                        !actual_stream_started ||
                        actual_stream_start_sample != expected_start_sample ||
                        actual_stream[channel].size() <
                            expected_stream.size()) {
                        throw std::runtime_error(
                            "upsample output stream timing or length differs from Java");
                    }
                    for (std::size_t i = 0;
                         i < expected_stream.size();
                         ++i) {
                        const auto error = std::abs(
                            actual_stream[channel][i] -
                            expected_stream[i]);
                        maximum_error =
                            std::max(maximum_error, error);
                        if (error > 1e-8) {
                            throw std::runtime_error(
                                "upsample stream sample differs from Java fixture");
                        }
                        ++checked;
                    }
                }
            }
            source.stop();
            decimator.stop();
        }
        std::cout << "Decimator Java parity passed: " << checked
                  << " samples, max abs error " << maximum_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
