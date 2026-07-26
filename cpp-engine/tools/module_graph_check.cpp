#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <json.hpp>

#include "pamguard/core/DataModel.h"
#include "pamguard/core/ModuleGraph.h"
#include "pamguard/core/ModuleGraphJson.h"
#include "pamguard/core/ModuleRuntime.h"
#include "pamguard/core/SignalNodes.h"
#include "pamguard/core/SignalRoutingSettings.h"
#include "pamguard/core/BuiltInModules.h"
#include "pamguard/core/FftDetectorNodes.h"
#include "pamguard/core/DetectorNodes.h"
#include "pamguard/core/IshmaelSettings.h"
#include "pamguard/core/LocalisationData.h"
#include "pamguard/core/OperatorNodes.h"
#include "pamguard/detectors/ClickAngleVeto.h"
#include "pamguard/localisation/DelayGroupEstimator.h"

namespace {

using Json = nlohmann::json;
using pamguard::core::ModuleTypeDescriptor;
using pamguard::core::PortDescriptor;
using pamguard::core::PortDirection;

class ScopedTemporaryDirectory {
public:
    explicit ScopedTemporaryDirectory(std::string prefix)
        : path_(
              std::filesystem::temp_directory_path() /
              (std::move(prefix) + "-" +
               std::to_string(
                   std::chrono::steady_clock::now()
                       .time_since_epoch()
                       .count()))) {}

    ~ScopedTemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    ScopedTemporaryDirectory(const ScopedTemporaryDirectory&) = delete;
    ScopedTemporaryDirectory& operator=(
        const ScopedTemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::string json_string(const std::string& value) {
    std::string encoded{"\""};
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            encoded.push_back('\\');
        }
        encoded.push_back(character);
    }
    encoded.push_back('"');
    return encoded;
}

double synthetic_sample(std::size_t channel, std::size_t sample) {
    const double background =
        0.01 * std::sin(
            static_cast<double>(sample) * 0.13 +
            static_cast<double>(channel) * 0.31);
    if (sample >= 80 && sample <= 86) {
        const double sign = (sample & 1u) == 0 ? 1.0 : -1.0;
        const double scale = channel == 0 ? 1.0 : 0.82;
        return background + sign * scale;
    }
    return background;
}

Json click_runtime_settings(
    std::uint32_t channel_bitmap,
    std::string grouping_type,
    std::vector<int> channel_groups,
    bool run_echo_online = false,
    bool discard_echoes = false,
    double echo_max_interval_seconds = 0.1) {
    return {
        {"channelBitmap", channel_bitmap},
        {"triggerBitmap", channel_bitmap},
        {"groupingType", std::move(grouping_type)},
        {"channelGroups", std::move(channel_groups)},
        {"minTriggerChannels", 1},
        {"thresholdDb", 10.0},
        {"longFilter", 0.00001},
        {"longFilter2", 0.000001},
        {"shortFilter", 0.1},
        {"preSample", 4},
        {"postSample", 4},
        {"minSep", 8},
        {"maxLength", 64},
        {"sampleNoise", false},
        {"storeBackground", false},
        {"publishTriggerFunction", false},
        {"preFilter", {{"type", "none"}}},
        {"triggerFilter", {{"type", "none"}}},
        {"echo", {
            {"runOnline", run_echo_online},
            {"discardEchoes", discard_echoes},
            {"maxIntervalSeconds", echo_max_interval_seconds},
        }},
    };
}

Json click_train_runtime_settings(bool enabled) {
    return {
        {"enabled", enabled},
        {"minIciSeconds", 0.1},
        {"maxIciSeconds", 2.0},
        {"maxIciChange", 1.2},
        {"okAngleErrorDegrees", 1.0},
        {"initialPerpendicularDistanceM", 100.0},
        {"minClicks", 6},
        {"minAngleChangeDegrees", 5.0},
        {"iciUpdateRatio", 0.5},
        {"minUpdateGapSeconds", 5.0},
    };
}

pamguard::core::AudioChunk click_runtime_chunk(
    std::size_t channel_count,
    bool include_echo_candidate) {
    pamguard::core::AudioChunk chunk;
    chunk.start_sample = 0;
    chunk.sample_rate_hz = 48000;
    chunk.channel_count = channel_count;
    const std::size_t frame_count =
        include_echo_candidate ? 6144 : 1024;
    chunk.interleaved_pcm.assign(
        frame_count * channel_count,
        0.0);
    for (std::size_t sample = 0;
         sample < frame_count;
         ++sample) {
        const bool in_first = sample >= 200 && sample <= 206;
        const bool in_second =
            include_echo_candidate &&
            sample >= 5000 && sample <= 5006;
        for (std::size_t channel = 0;
             channel < channel_count;
             ++channel) {
            double value =
                0.01 *
                (static_cast<double>((sample + channel) % 7) /
                     7.0 -
                 0.5);
            if (in_first || in_second) {
                value +=
                    (sample & 1u) == 0 ? 1.0 : -1.0;
            }
            chunk.interleaved_pcm[
                sample * channel_count + channel] = value;
        }
    }
    return chunk;
}

pamguard::core::ModuleGraphDocument click_only_document(
    std::size_t channel_count,
    const Json& click_settings) {
    return {
        1,
        1,
        {
            {"source", "pamguard.acquisition", "Input", true,
             Json{
                 {"sourceId", "click-runtime-test"},
                 {"sampleRateHz", 48000},
                 {"channelCount", channel_count},
                 {"subtractDC", false},
                 {"dcTimeConstantSeconds", 1.0},
             }.dump()},
            {"clicks", "pamguard.click-detector", "Clicks", true,
             click_settings.dump()},
        },
        {
            {"click-input", {"source", "audio"}, {"clicks", "input"}},
        },
    };
}

std::vector<pamguard::detectors::ClickDetectionResult>
run_click_only_runtime(
    std::size_t channel_count,
    const Json& click_settings,
    bool include_echo_candidate = false) {
    pamguard::core::ModuleRuntime runtime;
    runtime.configure(
        click_only_document(channel_count, click_settings));
    const auto clicks = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "clicks",
            "clicks"));
    if (!clicks) {
        throw std::runtime_error(
            "Click runtime did not expose its click block");
    }
    std::vector<pamguard::detectors::ClickDetectionResult>
        detections;
    auto subscription = clicks->subscribe([&](const auto& unit) {
        const auto* click =
            std::any_cast<
                pamguard::detectors::ClickDetectionResult>(
                &unit.payload);
        if (!click) {
            throw std::runtime_error(
                "Click runtime produced the wrong payload type");
        }
        detections.push_back(*click);
    });
    runtime.start();
    runtime.ingest(
        "source",
        click_runtime_chunk(
            channel_count,
            include_echo_candidate));
    runtime.stop();
    return detections;
}

PortDescriptor input(
    std::string id,
    std::string type,
    std::vector<std::string> capabilities = {}) {
    return {
        std::move(id),
        "Input",
        PortDirection::Input,
        std::move(type),
        true,
        false,
        std::move(capabilities),
    };
}

PortDescriptor output(
    std::string id,
    std::string type,
    std::vector<std::string> capabilities = {}) {
    return {
        std::move(id),
        "Output",
        PortDirection::Output,
        std::move(type),
        false,
        false,
        std::move(capabilities),
    };
}

bool has_issue(
    const std::vector<pamguard::core::GraphIssue>& issues,
    const std::string& code) {
    for (const auto& issue : issues) {
        if (issue.code == code) {
            return true;
        }
    }
    return false;
}

pamguard::core::ModuleRegistry make_registry() {
    pamguard::core::ModuleRegistry registry;
    registry.register_type({
        "acquisition",
        "Acquisition",
        "Sources",
        "Produces raw audio",
        1,
        1,
        {output("audio", "pamguard.raw-audio", {"sampled"})},
    });
    registry.register_type({
        "decimator",
        "Decimator",
        "Signal processing",
        "Changes sample rate",
        0,
        {},
        {
            input("input", "pamguard.raw-audio", {"sampled"}),
            output("output", "pamguard.raw-audio", {"sampled"}),
        },
    });
    registry.register_type({
        "fft",
        "FFT",
        "Signal processing",
        "Produces FFT frames",
        0,
        {},
        {
            input("input", "pamguard.raw-audio", {"sampled"}),
            output("fft", "pamguard.fft"),
        },
    });
    registry.register_type({
        "spectrogram",
        "Spectrogram",
        "Displays",
        "Consumes an independently selected FFT source",
        0,
        {},
        {input("fft", "pamguard.fft")},
    });
    registry.register_type({
        "playback",
        "Sound Output",
        "Output",
        "Plays a selected raw source",
        0,
        {},
        {input("audio", "pamguard.raw-audio", {"sampled"})},
    });
    return registry;
}

pamguard::core::ModuleGraphDocument valid_document() {
    return {
        1,
        0,
        {
            {"source-1", "acquisition", "Hydrophone input", true, R"({"device":"default"})"},
            {"decimator-1", "decimator", "Low-frequency branch", true, R"({"factor":4})"},
            {"fft-full", "fft", "Full-band FFT", true, R"({"length":2048})"},
            {"fft-low", "fft", "Low-band FFT", true, R"({"length":4096})"},
            {"display-full", "spectrogram", "Full-band display", true, "{}"},
            {"display-low", "spectrogram", "Low-band display", true, "{}"},
            {"playback-1", "playback", "Monitor output", true, "{}"},
        },
        {
            {"c1", {"source-1", "audio"}, {"fft-full", "input"}},
            {"c2", {"source-1", "audio"}, {"decimator-1", "input"}},
            {"c3", {"decimator-1", "output"}, {"fft-low", "input"}},
            {"c4", {"fft-full", "fft"}, {"display-full", "fft"}},
            {"c5", {"fft-low", "fft"}, {"display-low", "fft"}},
            {"c6", {"source-1", "audio"}, {"playback-1", "audio"}},
        },
    };
}

void check_data_block() {
    pamguard::core::DataBlock block({
        "raw-1",
        "Raw input",
        "source-1",
        "audio",
        "pamguard.raw-audio",
        1,
        48000.0,
        0x3,
        0,
        {},
        {},
        {},
        {"sampled"},
        2,
    });

    std::uint64_t first_observer_count = 0;
    std::uint64_t second_observer_count = 0;
    auto first = block.subscribe([&](const auto&) { ++first_observer_count; });
    auto second = block.subscribe([&](const auto&) { ++second_observer_count; });
    for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
        pamguard::core::DataUnitMetadata metadata;
        metadata.sequence = sequence;
        block.publish(pamguard::core::make_data_unit(std::move(metadata), sequence));
    }
    if (first_observer_count != 3 || second_observer_count != 3) {
        throw std::runtime_error("Data block did not fan out to both observers");
    }
    const auto history = block.recent_history();
    if (history.size() != 2 ||
        history.front().metadata.sequence != 2 ||
        history.back().metadata.sequence != 3) {
        throw std::runtime_error("Data block history is not bounded or ordered");
    }
    first.cancel();
    pamguard::core::DataUnitMetadata metadata;
    metadata.sequence = 4;
    block.publish(pamguard::core::make_data_unit(std::move(metadata), 4));
    if (first_observer_count != 3 || second_observer_count != 4) {
        throw std::runtime_error("Data block unsubscribe did not isolate the observer");
    }

    bool type_rejected = false;
    try {
        pamguard::core::DataUnitMetadata invalid;
        invalid.type_id = "pamguard.fft";
        block.publish(pamguard::core::make_data_unit(std::move(invalid), 5));
    }
    catch (const std::invalid_argument&) {
        type_rejected = true;
    }
    if (!type_rejected) {
        throw std::runtime_error("Data block accepted an incompatible data unit");
    }

    pamguard::core::DataBlockDescriptor constrained_descriptor;
    constrained_descriptor.id = "constrained";
    constrained_descriptor.name = "Constrained";
    constrained_descriptor.producer_module_id = "source";
    constrained_descriptor.producer_port_id = "audio";
    constrained_descriptor.data_type = "pamguard.raw-audio";
    constrained_descriptor.channel_bitmap = 0x1;
    constrained_descriptor.clock_domain_id = "clock-a";
    pamguard::core::DataBlock constrained(
        std::move(constrained_descriptor));
    bool metadata_rejected = false;
    try {
        pamguard::core::DataUnitMetadata invalid;
        invalid.channel_bitmap = 0x2;
        invalid.clock_domain_id = "clock-b";
        constrained.publish(
            pamguard::core::make_data_unit(std::move(invalid), 1));
    }
    catch (const std::invalid_argument&) {
        metadata_rejected = true;
    }
    if (!metadata_rejected) {
        throw std::runtime_error(
            "Data block accepted incompatible channel/clock metadata");
    }
}

void check_non_blocking_presentation_delivery() {
    pamguard::core::DataBlock block({
        "display-source",
        "Display source",
        "fft-1",
        "fft",
        "pamguard.fft",
        1,
    });
    std::mutex mutex;
    std::condition_variable condition;
    bool callback_started = false;
    bool release_callback = false;
    auto subscription = block.subscribe(
        [&](const auto&) {
            std::unique_lock lock(mutex);
            callback_started = true;
            condition.notify_all();
            condition.wait(lock, [&] { return release_callback; });
        },
        {pamguard::core::DeliveryMode::QueuedDropOldest, 2});

    block.publish(pamguard::core::make_data_unit(
        pamguard::core::DataUnitMetadata{},
        0));
    {
        std::unique_lock lock(mutex);
        if (!condition.wait_for(
                lock,
                std::chrono::seconds(2),
                [&] { return callback_started; })) {
            throw std::runtime_error("Queued data block observer did not start");
        }
    }

    const auto start = std::chrono::steady_clock::now();
    for (int value = 1; value <= 10; ++value) {
        block.publish(pamguard::core::make_data_unit(
            pamguard::core::DataUnitMetadata{},
            value));
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto pressure = block.stats();
    if (elapsed > std::chrono::milliseconds(250) ||
        pressure.dropped < 8 ||
        pressure.queued_units != 2 ||
        pressure.maximum_queued_units != 2) {
        throw std::runtime_error("Slow presentation observer blocked or failed to shed backlog");
    }
    {
        std::lock_guard lock(mutex);
        release_callback = true;
    }
    condition.notify_all();
    subscription.cancel();
}

void check_observer_failure_isolation() {
    pamguard::core::DataBlock synchronous({
        "sync-errors",
        "Synchronous observer errors",
        "test",
        "output",
        "pamguard.test",
    });
    auto sync_subscription = synchronous.subscribe(
        [](const auto&) { throw std::runtime_error("expected observer failure"); });
    bool sync_error_propagated = false;
    try {
        synchronous.publish(pamguard::core::make_data_unit(
            pamguard::core::DataUnitMetadata{},
            1));
    }
    catch (const std::runtime_error&) {
        sync_error_propagated = true;
    }
    if (!sync_error_propagated ||
        synchronous.stats().observer_errors != 1) {
        throw std::runtime_error(
            "Synchronous observer failure was not propagated and recorded");
    }

    pamguard::core::DataBlock queued({
        "queued-errors",
        "Queued observer errors",
        "test",
        "output",
        "pamguard.test",
    });
    std::atomic<std::uint64_t> attempts = 0;
    auto queued_subscription = queued.subscribe(
        [&](const auto&) {
            ++attempts;
            throw std::runtime_error("expected queued observer failure");
        },
        {pamguard::core::DeliveryMode::QueuedDropOldest, 8});
    queued.publish(pamguard::core::make_data_unit(
        pamguard::core::DataUnitMetadata{},
        1));
    queued.publish(pamguard::core::make_data_unit(
        pamguard::core::DataUnitMetadata{},
        2));

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while ((attempts.load() < 2 ||
            queued.stats().observer_errors < 2) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (attempts.load() != 2 ||
        queued.stats().observer_errors != 2) {
        throw std::runtime_error(
            "Queued observer worker did not survive and record repeated failures");
    }
    queued_subscription.cancel();
}

void check_queued_self_unsubscribe() {
    pamguard::core::DataBlock block({
        "self-unsubscribe",
        "Self-unsubscribing observer",
        "test",
        "output",
        "pamguard.test",
    });
    std::mutex mutex;
    std::condition_variable condition;
    std::size_t callbacks = 0;
    pamguard::core::Subscription subscription;
    subscription = block.subscribe(
        [&](const auto&) {
            subscription.cancel();
            {
                std::lock_guard lock(mutex);
                ++callbacks;
            }
            condition.notify_all();
        },
        {pamguard::core::DeliveryMode::QueuedDropOldest, 2});
    block.publish(pamguard::core::make_data_unit(
        pamguard::core::DataUnitMetadata{},
        1));
    {
        std::unique_lock lock(mutex);
        if (!condition.wait_for(
                lock,
                std::chrono::seconds(2),
                [&] { return callbacks == 1; })) {
            throw std::runtime_error(
                "Queued observer did not complete self-unsubscribe");
        }
    }
    block.publish(pamguard::core::make_data_unit(
        pamguard::core::DataUnitMetadata{},
        2));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (callbacks != 1 ||
        block.stats().subscriber_count != 0) {
        throw std::runtime_error(
            "Queued self-unsubscribe did not safely remove the observer");
    }
}

std::shared_ptr<pamguard::core::DataBlock> audio_block(
    std::string id,
    std::string producer,
    std::string port,
    std::uint32_t channels = 0x3) {
    return std::make_shared<pamguard::core::DataBlock>(
        pamguard::core::DataBlockDescriptor{
            std::move(id),
            "Audio",
            std::move(producer),
            std::move(port),
            pamguard::core::kRawAudioDataType,
            1,
            48000.0,
            channels,
        });
}

void check_executable_signal_branches() {
    auto raw = audio_block("raw", "source", "audio");
    auto amplified = audio_block("amplified", "amp", "audio");
    auto patched = audio_block("patched", "patch", "audio");
    auto filtered = audio_block("filtered", "filter", "audio", 0x1);
    auto full_fft = std::make_shared<pamguard::core::DataBlock>(
        pamguard::core::DataBlockDescriptor{
            "fft-full",
            "Full FFT",
            "fft-1",
            "fft",
            pamguard::core::kFftDataType,
            1,
            48000.0,
            0x3,
        });
    auto alternate_fft = std::make_shared<pamguard::core::DataBlock>(
        pamguard::core::DataBlockDescriptor{
            "fft-alternate",
            "Alternate FFT",
            "fft-2",
            "fft",
            pamguard::core::kFftDataType,
            1,
            48000.0,
            0x3,
        });

    pamguard::core::AudioSourceNode source(
        "source",
        {false, 1.0},
        raw);
    pamguard::core::AmplifierNode amp("amp", {{2.0, -1.0}}, raw, amplified);
    pamguard::core::PatchPanelNode patch(
        "patch",
        {{{1.0, 1.0}, {1.0, -1.0}}},
        raw,
        patched);
    pamguard::core::FilterNode filter(
        "filter",
        {0x1, {}},
        raw,
        filtered);
    pamguard::core::FftConfig fft_config;
    fft_config.fft_length = 4;
    fft_config.fft_hop = 2;
    fft_config.channels = {0, 1};
    pamguard::core::FftNode fft_one("fft-1", fft_config, amplified, full_fft);
    pamguard::core::FftNode fft_two("fft-2", fft_config, patched, alternate_fft);

    const pamguard::core::AudioChunk* amplified_result = nullptr;
    const pamguard::core::AudioChunk* patched_result = nullptr;
    pamguard::core::AudioChunk amplified_copy;
    pamguard::core::AudioChunk patched_copy;
    pamguard::core::AudioChunk filtered_copy;
    std::uint32_t filtered_channel_bitmap = 0;
    std::size_t filtered_count = 0;
    std::size_t full_fft_count = 0;
    std::size_t alternate_fft_count = 0;
    auto amp_observer = amplified->subscribe([&](const auto& unit) {
        amplified_result = std::any_cast<pamguard::core::AudioChunk>(&unit.payload);
        if (amplified_result) {
            amplified_copy = *amplified_result;
        }
    });
    auto patch_observer = patched->subscribe([&](const auto& unit) {
        patched_result = std::any_cast<pamguard::core::AudioChunk>(&unit.payload);
        if (patched_result) {
            patched_copy = *patched_result;
        }
    });
    auto filter_observer = filtered->subscribe([&](const auto& unit) {
        ++filtered_count;
        filtered_copy =
            std::any_cast<pamguard::core::AudioChunk>(unit.payload);
        filtered_channel_bitmap = unit.metadata.channel_bitmap;
    });
    auto fft_one_observer = full_fft->subscribe([&](const auto&) { ++full_fft_count; });
    auto fft_two_observer = alternate_fft->subscribe([&](const auto&) { ++alternate_fft_count; });

    source.prepare();
    amp.prepare();
    patch.prepare();
    filter.prepare();
    fft_one.prepare();
    fft_two.prepare();
    amp.start();
    patch.start();
    filter.start();
    fft_one.start();
    fft_two.start();
    source.start();

    pamguard::core::AudioChunk audio;
    audio.start_sample = 100;
    audio.time_unix_ms = 2000;
    audio.sample_rate_hz = 48000;
    audio.channel_count = 2;
    for (int frame = 0; frame < 8; ++frame) {
        audio.interleaved_pcm.push_back(static_cast<double>(frame + 1));
        audio.interleaved_pcm.push_back(static_cast<double>(10 + frame));
    }
    source.ingest(audio);

    if (!amplified_result ||
        amplified_copy.interleaved_pcm[0] != 2.0 ||
        amplified_copy.interleaved_pcm[1] != -10.0) {
        throw std::runtime_error("Executable amplifier branch changed Java linear-gain semantics");
    }
    if (!patched_result ||
        patched_copy.interleaved_pcm[0] != 11.0 ||
        patched_copy.interleaved_pcm[1] != -9.0) {
        throw std::runtime_error("Executable patch-panel branch changed Java matrix semantics");
    }
    if (filtered_count != 1 || full_fft_count != 6 || alternate_fft_count != 6) {
        throw std::runtime_error("Independent signal branches or multiple FFT nodes did not execute");
    }
    if (filtered_channel_bitmap != 0x1 ||
        filtered_copy.interleaved_pcm[1] != 0.0) {
        throw std::runtime_error(
            "Filter output leaked or advertised an unselected channel");
    }
    source.stop();
    amp.stop();
    patch.stop();
    filter.stop();
    fft_one.stop();
    fft_two.stop();

    auto continuity_block =
        audio_block("continuity", "continuity-source", "audio", 0x1);
    pamguard::core::AudioSourceNode continuity_source(
        "continuity-source",
        {false, 1.0},
        continuity_block);
    std::vector<bool> discontinuities;
    auto continuity_subscription = continuity_block->subscribe(
        [&](const auto& unit) {
            discontinuities.push_back(
                unit.metadata.discontinuity);
        });
    continuity_source.prepare();
    continuity_source.start();
    pamguard::core::AudioChunk first_chunk;
    first_chunk.sample_rate_hz = 48000;
    first_chunk.channel_count = 1;
    first_chunk.start_sample = 0;
    first_chunk.interleaved_pcm.assign(4, 0.0);
    continuity_source.ingest(first_chunk);
    first_chunk.start_sample = 10;
    continuity_source.ingest(first_chunk);
    continuity_source.stop();
    if (discontinuities != std::vector<bool>{false, true}) {
        throw std::runtime_error(
            "Acquisition did not expose a sample-position discontinuity");
    }
}

void check_acquisition_dc_filter() {
    const auto document = [](
                              Json subtract_dc,
                              Json time_constant_seconds) {
        return pamguard::core::ModuleGraphDocument{
            1,
            1,
            {
                {
                    "source",
                    "pamguard.acquisition",
                    "DC-filtered input",
                    true,
                    Json{
                        {"sourceId", "dc-filter-test"},
                        {"sampleRateHz", 10},
                        {"channelCount", 2},
                        {"subtractDC", std::move(subtract_dc)},
                        {"dcTimeConstantSeconds",
                         std::move(time_constant_seconds)},
                        {"calibrationDbOffsetByChannel",
                         Json::array()},
                    }.dump(),
                },
            },
            {},
        };
    };
    const auto chunk = [](
                           std::uint64_t start_sample,
                           std::vector<double> samples) {
        pamguard::core::AudioChunk value;
        value.start_sample = start_sample;
        value.sample_rate_hz = 10;
        value.channel_count = 2;
        value.interleaved_pcm = std::move(samples);
        return value;
    };
    const auto require_samples = [](
                                     const auto& actual,
                                     const auto& expected,
                                     const std::string& context) {
        if (actual.size() != expected.size()) {
            throw std::runtime_error(
                context + " emitted the wrong sample count");
        }
        for (std::size_t index = 0;
             index < actual.size();
             ++index) {
            if (std::abs(actual[index] - expected[index]) >
                1e-12) {
                throw std::runtime_error(
                    context +
                    " diverged from Acquisition.DCFilter");
            }
        }
    };

    pamguard::core::ModuleRuntime filtered;
    filtered.configure(document(true, 2.0));
    const auto output = filtered.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "source",
            "audio"));
    if (!output) {
        throw std::runtime_error(
            "Acquisition DC fixture did not expose raw audio");
    }
    std::vector<pamguard::core::AudioChunk> emitted;
    auto subscription = output->subscribe(
        [&](const auto& unit) {
            emitted.push_back(
                std::any_cast<pamguard::core::AudioChunk>(
                    unit.payload));
        });

    // Java alpha = 1 - 1/(10 * 2) = 0.95. Each channel keeps an
    // independent background across RawDataUnit/chunk boundaries.
    filtered.start();
    filtered.ingest(
        "source",
        chunk(0, {1.0, 10.0, 2.0, 12.0}));
    filtered.ingest(
        "source",
        chunk(2, {3.0, 14.0, 4.0, 16.0}));
    filtered.stop();
    if (emitted.size() != 2) {
        throw std::runtime_error(
            "Acquisition DC fixture emitted the wrong chunk count");
    }
    require_samples(
        emitted[0].interleaved_pcm,
        std::vector<double>{1.0, 10.0, 1.95, 11.5},
        "First acquisition DC chunk");
    require_samples(
        emitted[1].interleaved_pcm,
        std::vector<double>{
            2.8525,
            12.925,
            3.709875,
            14.27875,
        },
        "Chunk-boundary acquisition DC state");

    filtered.reset();
    filtered.start();
    filtered.ingest(
        "source",
        chunk(0, {1.0, 10.0, 2.0, 12.0}));
    filtered.stop();
    if (emitted.size() != 3) {
        throw std::runtime_error(
            "Reset acquisition DC fixture emitted the wrong chunk count");
    }
    require_samples(
        emitted[2].interleaved_pcm,
        std::vector<double>{1.0, 10.0, 1.95, 11.5},
        "Reset acquisition DC state");

    pamguard::core::ModuleRuntime passthrough;
    passthrough.configure(document(false, 2.0));
    const auto passthrough_output = passthrough.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "source",
            "audio"));
    std::vector<double> passthrough_samples;
    auto passthrough_subscription =
        passthrough_output->subscribe([&](const auto& unit) {
            passthrough_samples =
                std::any_cast<pamguard::core::AudioChunk>(
                    unit.payload)
                    .interleaved_pcm;
        });
    const std::vector<double> original{
        1.0,
        10.0,
        2.0,
        12.0,
    };
    passthrough.start();
    passthrough.ingest(
        "source",
        chunk(0, original));
    passthrough.stop();
    if (passthrough_samples != original) {
        throw std::runtime_error(
            "Disabled Acquisition DC subtraction changed samples");
    }

    const auto require_rejected = [&](const auto& invalid,
                                      const std::string& context) {
        bool rejected = false;
        try {
            pamguard::core::ModuleRuntime runtime;
            runtime.configure(invalid);
        }
        catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (!rejected) {
            throw std::runtime_error(context);
        }
    };
    require_rejected(
        document(false, 0.0),
        "Acquisition accepted a non-positive DC time constant");
    require_rejected(
        document("false", 1.0),
        "Acquisition accepted non-boolean subtractDC");
    require_rejected(
        document(false, "1.0"),
        "Acquisition accepted a non-numeric DC time constant");

    pamguard::core::ModuleRegistry registry;
    pamguard::core::register_builtin_module_types(registry);
    const auto* acquisition =
        registry.find("pamguard.acquisition");
    if (!acquisition) {
        throw std::runtime_error(
            "Acquisition runtime descriptor is absent");
    }
    const auto defaults =
        Json::parse(acquisition->default_settings_json);
    if (defaults.at("subtractDC") != true ||
        defaults.at("dcTimeConstantSeconds") != 1) {
        throw std::runtime_error(
            "Acquisition runtime DC defaults diverged from Java");
    }
}

void check_filter_params_runtime_contract() {
    const auto document = [](
                              const std::string& type_id,
                              const Json& settings) {
        return pamguard::core::ModuleGraphDocument{
            1,
            1,
            {
                {"source", "pamguard.acquisition", "Input", true,
                 R"({"sourceId":"filter-contract","sampleRateHz":48000,"channelCount":1,"subtractDC":false,"dcTimeConstantSeconds":1,"calibrationDbOffsetByChannel":[]})"},
                {"processor", type_id, "Processor", true, settings.dump()},
            },
            {
                {"route", {"source", "audio"}, {"processor", "input"}},
            },
        };
    };
    const auto require_configures = [&](const std::string& type_id,
                                        const Json& settings,
                                        const std::string& context) {
        try {
            pamguard::core::ModuleRuntime runtime;
            runtime.configure(document(type_id, settings));
        }
        catch (const std::exception& error) {
            throw std::runtime_error(
                context + ": " + error.what());
        }
    };
    const auto require_rejected = [&](const std::string& type_id,
                                      const Json& settings,
                                      const std::string& context) {
        bool rejected = false;
        try {
            pamguard::core::ModuleRuntime runtime;
            runtime.configure(document(type_id, settings));
        }
        catch (const std::exception&) {
            rejected = true;
        }
        if (!rejected) {
            throw std::runtime_error(context);
        }
    };

    Json arbitrary_filter{
        {"channelBitmap", 1},
        {"type", "firArbitrary"},
        {"band", "bandPass"},
        {"order", 5},
        {"lowPassFreqHz", 6000},
        {"highPassFreqHz", 2000},
        {"passBandRippleDb", 1.25},
        {"stopBandRippleDb", 9.5},
        {"chebyGamma", 3.5},
        {"arbitraryFrequenciesHz",
         Json::array({0, 1500, 3000, 12000, 15000, 24000})},
        {"arbitraryGainsDb",
         Json::array({-60, -60, 0, 0, -60, -60})},
    };
    require_configures(
        "pamguard.filter",
        arbitrary_filter,
        "Standalone filter rejected complete arbitrary FilterParams");

    Json decimator{
        {"outputSampleRateHz", 12000},
        {"filter", arbitrary_filter},
        {"interpolation", 2},
        {"channelBitmap", 1},
    };
    decimator["filter"].erase("channelBitmap");
    require_configures(
        "pamguard.decimator",
        decimator,
        "Decimator rejected its complete configured anti-alias FilterParams");

    auto clamped_decimator = decimator;
    clamped_decimator["filter"]["type"] = "fft";
    clamped_decimator["filter"]["band"] = "lowPass";
    clamped_decimator["filter"]["lowPassFreqHz"] = 30000;
    clamped_decimator["filter"]["arbitraryFrequenciesHz"] =
        Json::array();
    clamped_decimator["filter"]["arbitraryGainsDb"] = Json::array();
    require_configures(
        "pamguard.decimator",
        clamped_decimator,
        "Decimator did not apply Java's source-Nyquist lowPassFreq clamp");

    auto invalid = arbitrary_filter;
    invalid["arbitraryGainsDb"] = Json::array({0, -60});
    require_rejected(
        "pamguard.filter",
        invalid,
        "Filter accepted unequal arbitrary frequency/gain arrays");

    invalid = arbitrary_filter;
    invalid["arbitraryFrequenciesHz"][5] = 25000;
    require_rejected(
        "pamguard.filter",
        invalid,
        "Filter accepted an arbitrary response above Nyquist");

    invalid = arbitrary_filter;
    invalid["arbitraryFrequenciesHz"][3] = 1000;
    require_rejected(
        "pamguard.filter",
        invalid,
        "Filter accepted unordered arbitrary response frequencies");

    invalid = arbitrary_filter;
    invalid["stopBandRippleDb"] = -0.5;
    require_rejected(
        "pamguard.filter",
        invalid,
        "Filter did not parse and validate stopBandRippleDb");

    invalid = arbitrary_filter;
    invalid["chebyGamma"] = 0;
    require_rejected(
        "pamguard.filter",
        invalid,
        "Filter did not parse and validate chebyGamma");

    invalid = arbitrary_filter;
    invalid["type"] = "notAFilter";
    require_rejected(
        "pamguard.filter",
        invalid,
        "Filter accepted an unknown FilterType");

    auto invalid_decimator = decimator;
    invalid_decimator["interpolation"] = 3;
    require_rejected(
        "pamguard.decimator",
        invalid_decimator,
        "Decimator accepted an interpolation mode outside 0/1/2");

    pamguard::core::ModuleRegistry registry;
    pamguard::core::register_builtin_module_types(registry);
    const auto* filter_descriptor = registry.find("pamguard.filter");
    const auto* decimator_descriptor =
        registry.find("pamguard.decimator");
    if (!filter_descriptor || !decimator_descriptor) {
        throw std::runtime_error(
            "Filter/decimator runtime descriptors are absent");
    }
    const auto filter_defaults =
        Json::parse(filter_descriptor->default_settings_json);
    const auto decimator_defaults =
        Json::parse(decimator_descriptor->default_settings_json);
    if (filter_defaults.at("type") != "butterworth" ||
        filter_defaults.at("band") != "bandPass" ||
        filter_defaults.at("order") != 4 ||
        filter_defaults.at("lowPassFreqHz") != 20000 ||
        filter_defaults.at("highPassFreqHz") != 2000 ||
        filter_defaults.at("passBandRippleDb") != 2 ||
        filter_defaults.at("stopBandRippleDb") != 2 ||
        filter_defaults.at("chebyGamma") != 3 ||
        !filter_defaults.at("arbitraryFrequenciesHz").empty() ||
        !filter_defaults.at("arbitraryGainsDb").empty()) {
        throw std::runtime_error(
            "Standalone FilterParams defaults diverged from Java");
    }
    const auto& decimator_filter = decimator_defaults.at("filter");
    if (decimator_defaults.at("outputSampleRateHz") != 2000 ||
        decimator_defaults.at("interpolation") != 0 ||
        decimator_filter.at("type") != "butterworth" ||
        decimator_filter.at("band") != "lowPass" ||
        decimator_filter.at("order") != 6 ||
        decimator_filter.at("lowPassFreqHz") != 1000 ||
        decimator_filter.at("highPassFreqHz") != 2000 ||
        decimator_filter.at("passBandRippleDb") != 2 ||
        decimator_filter.at("stopBandRippleDb") != 2 ||
        decimator_filter.at("chebyGamma") != 3 ||
        !decimator_filter.at("arbitraryFrequenciesHz").empty() ||
        !decimator_filter.at("arbitraryGainsDb").empty()) {
        throw std::runtime_error(
            "Decimator defaults diverged from Java DecimatorParams");
    }
}

void check_runtime_graph_factory() {
    pamguard::core::ModuleRegistry registry;
    pamguard::core::register_builtin_module_types(registry);
    pamguard::core::ModuleGraphDocument document{
        1,
        7,
        {
            {"source", "pamguard.acquisition", "Input", true,
             R"({"sourceId":"test","sampleRateHz":48000,"channelCount":2,"subtractDC":false,"dcTimeConstantSeconds":1})"},
            {"decimator", "pamguard.decimator", "Low branch", true,
             R"({"outputSampleRateHz":12000,"filter":{"type":"butterworth","band":"lowPass","order":6,"lowPassFreqHz":6000,"highPassFreqHz":2000,"passBandRippleDb":2,"stopBandRippleDb":2,"chebyGamma":3,"arbitraryFrequenciesHz":[],"arbitraryGainsDb":[]},"interpolation":0,"channelBitmap":3})"},
            {"fft-full", "pamguard.fft", "Full FFT", true,
             R"({"fftLength":8,"fftHop":4,"windowType":"Blackman-Harris","channels":[0,1],"clickRemoval":true,"clickThreshold":5.0,"clickPower":6})"},
            {"fft-low", "pamguard.fft", "Low FFT", true,
             R"({"fftLength":8,"fftHop":4,"windowType":"Hann","channels":[0,1],"clickRemoval":false,"clickThreshold":5.0,"clickPower":6})"},
            {"noise", "pamguard.spectrogram-noise", "Reduced FFT", true,
             R"({"medianFilter":true,"medianFilterLength":3,"averageSubtraction":false,"updateConstant":0.02,"kernelSmoothing":false,"threshold":false,"thresholdDb":8.0,"finalOutput":2})"},
            {"display-full", "pamguard.spectrogram-display", "Full display", true, "{}"},
            {"display-low", "pamguard.spectrogram-display", "Low display", true, "{}"},
        },
        {
            {"r1", {"source", "audio"}, {"fft-full", "input"}},
            {"r2", {"source", "audio"}, {"decimator", "input"}},
            {"r3", {"decimator", "output"}, {"fft-low", "input"}},
            {"r4", {"fft-full", "fft"}, {"noise", "input"}},
            {"r5", {"fft-low", "fft"}, {"display-low", "fft"}},
            {"r6", {"noise", "output"}, {"display-full", "fft"}},
        },
    };
    pamguard::core::ModuleGraph graph(registry);
    if (!graph.validate(document).valid()) {
        throw std::runtime_error("Built-in executable graph failed registry validation");
    }
    std::reverse(
        document.modules.begin(),
        document.modules.end());

    pamguard::core::ModuleRuntime runtime;
    runtime.configure(document);
    const auto prepared_statuses = runtime.module_statuses();
    if (runtime.running() ||
        runtime.revision() != 7 ||
        runtime.data_blocks().size() != 5 ||
        std::any_of(
            prepared_statuses.begin(),
            prepared_statuses.end(),
            [](const auto& status) {
                return status.state !=
                    pamguard::core::ModuleState::Prepared;
            })) {
        throw std::runtime_error(
            "Configured runtime did not remain idle with prepared modules");
    }
    const auto full = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("fft-full", "fft"));
    const auto low = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("fft-low", "fft"));
    const auto reduced = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("noise", "output"));
    if (!full || !low || !reduced) {
        throw std::runtime_error("Runtime block lookup did not preserve module/port identity");
    }
    if (full->descriptor().channel_bitmap != 0x3 ||
        low->descriptor().channel_bitmap != 0x3 ||
        full->descriptor().clock_domain_id != "source" ||
        low->descriptor().clock_domain_id != "source" ||
        reduced->descriptor().clock_domain_id != "source" ||
        full->descriptor().minimum_frequency_hz != 0.0 ||
        full->descriptor().maximum_frequency_hz != 24000.0 ||
        low->descriptor().maximum_frequency_hz != 6000.0) {
        throw std::runtime_error(
            "Runtime did not propagate FFT channel, clock, or frequency metadata");
    }
    std::size_t full_frames = 0;
    std::size_t low_frames = 0;
    std::size_t reduced_frames = 0;
    pamguard::dsp::ComplexSpectrum full_first;
    pamguard::dsp::ComplexSpectrum reduced_first;
    auto full_subscription = full->subscribe([&](const auto& unit) {
        ++full_frames;
        if (full_first.empty()) {
            full_first = std::any_cast<pamguard::dsp::SpectrogramFrame>(
                unit.payload).bins;
        }
    });
    auto low_subscription = low->subscribe([&](const auto&) { ++low_frames; });
    auto reduced_subscription = reduced->subscribe([&](const auto& unit) {
        ++reduced_frames;
        if (reduced_first.empty()) {
            reduced_first = std::any_cast<pamguard::dsp::SpectrogramFrame>(
                unit.payload).bins;
        }
    });
    runtime.start();
    bool running_reconfiguration_rejected = false;
    try {
        runtime.configure(document);
    }
    catch (const std::logic_error&) {
        running_reconfiguration_rejected = true;
    }
    if (!running_reconfiguration_rejected || !runtime.running()) {
        throw std::runtime_error(
            "Runtime allowed graph reconfiguration while running");
    }

    pamguard::core::AudioChunk audio;
    audio.sample_rate_hz = 48000;
    audio.channel_count = 2;
    for (std::size_t frame = 0; frame < 64; ++frame) {
        audio.interleaved_pcm.push_back(std::sin(frame * 0.2));
        audio.interleaved_pcm.push_back(std::cos(frame * 0.13));
    }
    runtime.ingest("source", std::move(audio));
    runtime.stop();
    const auto stopped_statuses = runtime.module_statuses();
    if (runtime.running() ||
        std::any_of(
            stopped_statuses.begin(),
            stopped_statuses.end(),
            [](const auto& status) {
                return status.state !=
                    pamguard::core::ModuleState::Stopped;
            })) {
        throw std::runtime_error(
            "Runtime stop did not leave every module cleanly stopped");
    }
    if (full_frames != 30 || low_frames != 6 ||
        reduced_frames != full_frames) {
        throw std::runtime_error("Runtime did not execute independent full and decimated FFT branches");
    }
    if (full_first == reduced_first) {
        throw std::runtime_error(
            "Reusable spectrogram-noise node did not transform its FFT stream");
    }

    auto invalid = document;
    for (auto& module : invalid.modules) {
        if (module.id == "fft-full") {
            module.settings_json =
                R"({"fftLength":8,"fftHop":4,"windowType":"Hann","channels":[0],"clickRemoval":true,"clickThreshold":5.0,"clickPower":3})";
        }
    }
    bool rejected = false;
    try {
        pamguard::core::ModuleRuntime invalid_runtime;
        invalid_runtime.configure(invalid);
    }
    catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected) {
        throw std::runtime_error(
            "FFT runtime accepted Java-invalid odd click-removal power");
    }

    invalid = document;
    for (auto& module : invalid.modules) {
        if (module.id == "fft-full") {
            module.settings_json =
                R"({"fftLength":8,"fftHop":4,"windowType":"Hann","channels":[2],"clickRemoval":false,"clickThreshold":5.0,"clickPower":6})";
        }
    }
    rejected = false;
    try {
        pamguard::core::ModuleRuntime invalid_runtime;
        invalid_runtime.configure(invalid);
    }
    catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected) {
        throw std::runtime_error(
            "FFT runtime accepted a channel unavailable from its input");
    }

    pamguard::core::ModuleGraphDocument clocks{
        1,
        1,
        {
            {"source-a", "pamguard.acquisition", "Source A", true,
             R"({"sourceId":"a","sampleRateHz":48000,"channelCount":1,"subtractDC":false,"dcTimeConstantSeconds":1})"},
            {"source-b", "pamguard.acquisition", "Source B", true,
             R"({"sourceId":"b","sampleRateHz":48000,"channelCount":1,"subtractDC":false,"dcTimeConstantSeconds":1})"},
            {"clicks", "pamguard.click-detector", "Clicks", true,
             R"({"channelBitmap":1,"triggerBitmap":1,"minTriggerChannels":1,"thresholdDb":10.0,"longFilter":0.00001,"longFilter2":0.000001,"shortFilter":0.1,"preSample":10,"postSample":12,"minSep":8,"maxLength":128,"sampleNoise":false,"storeBackground":false,"publishTriggerFunction":false,"preFilter":{"type":"none"},"triggerFilter":{"type":"none"}})"},
            {"clips", "pamguard.clip-generator", "Clips", true,
             R"({"preTriggerSeconds":0.1,"postTriggerSeconds":0.1,"maximumBufferSeconds":2})"},
        },
        {
            {"clock-click", {"source-b", "audio"}, {"clicks", "input"}},
            {"clock-audio", {"source-a", "audio"}, {"clips", "audio"}},
            {"clock-trigger", {"clicks", "clicks"}, {"clips", "triggers"}},
        },
    };
    rejected = false;
    try {
        pamguard::core::ModuleRuntime invalid_runtime;
        invalid_runtime.configure(clocks);
    }
    catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected) {
        throw std::runtime_error(
            "Runtime accepted a multi-input module spanning clock domains");
    }

    auto amplifier_settings = Json::parse(
        pamguard::core::signal_amplifier_default_settings_json());
    amplifier_settings["channelSettings"][0]["gainDb"] =
        20.0 * std::log10(2.0);
    amplifier_settings["channelSettings"][1]["gainDb"] =
        20.0 * std::log10(0.5);
    pamguard::core::ModuleGraphDocument calibrated{
        1,
        1,
        {
            {"source", "pamguard.acquisition", "Calibrated source", true,
            R"({"sourceId":"calibrated","sampleRateHz":48000,"channelCount":2,"subtractDC":false,"dcTimeConstantSeconds":1,"calibrationDbOffsetByChannel":[100.0,101.0]})"},
            {"amp", "pamguard.amplifier", "Amplifier", true,
             amplifier_settings.dump()},
        },
        {
            {"calibration", {"source", "audio"}, {"amp", "input"}},
        },
    };
    pamguard::core::ModuleRuntime calibrated_runtime;
    calibrated_runtime.configure(calibrated);
    const auto amplified = calibrated_runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("amp", "output"));
    if (!amplified ||
        amplified->descriptor().clock_domain_id != "source" ||
        amplified->descriptor()
                .calibration_db_offset_by_channel.size() != 2 ||
        std::abs(
            amplified->descriptor()
                    .calibration_db_offset_by_channel[0] -
            (100.0 - 20.0 * std::log10(2.0))) > 1e-12 ||
        std::abs(
            amplified->descriptor()
                    .calibration_db_offset_by_channel[1] -
            (101.0 - 20.0 * std::log10(0.5))) > 1e-12) {
        throw std::runtime_error(
            "Amplifier did not preserve calibrated physical amplitude metadata");
    }
}

void check_click_detector_graph_node() {
    pamguard::core::ModuleGraphDocument document{
        1,
        3,
        {
            {"source", "pamguard.acquisition", "Input", true,
             R"({"sourceId":"test","sampleRateHz":48000,"channelCount":2,"subtractDC":false,"dcTimeConstantSeconds":1})"},
            {"clicks", "pamguard.click-detector", "Clicks", true,
             R"({"channelBitmap":3,"triggerBitmap":3,"minTriggerChannels":1,"thresholdDb":10.0,"longFilter":0.00001,"longFilter2":0.000001,"shortFilter":0.1,"preSample":10,"postSample":12,"minSep":8,"maxLength":128,"sampleNoise":false,"storeBackground":false,"publishTriggerFunction":false,"preFilter":{"type":"none"},"triggerFilter":{"type":"none"}})"},
            {"features", "pamguard.click-features", "Features", true,
             R"({"fftLength":64,"lengthEnergyFraction":90.0,"widthEnergyFraction":90.0,"energyBandsHz":[[1000,6000],[6000,14000]],"peakFrequencySearchHz":[500,20000],"meanFrequencyRangeHz":[500,20000]})"},
            {"localiser", "pamguard.click-localiser", "Localiser", true,
             R"({"preSample":10,"speedOfSoundMps":1500,"hydrophones":[{"channel":0,"xM":0,"yM":0,"zM":0},{"channel":1,"xM":1,"yM":0,"zM":0}],"delayMeasurement":{"filterBearings":false,"filterBand":"highPass","filterHighPassHz":0,"filterLowPassHz":0,"envelopeBearings":false,"useLeadingEdge":false,"upSample":1,"useRestrictedBins":false,"restrictedBins":80,"typeSettings":[{"clickType":7,"filterBearings":false,"filterBand":"highPass","filterHighPassHz":0,"filterLowPassHz":0,"envelopeBearings":false,"useLeadingEdge":false,"upSample":2,"useRestrictedBins":false,"restrictedBins":80}]},"angleVetoes":[]})"},
            {"classifier", "pamguard.click-classifier", "Classifier", true,
             R"({"enabled":true,"mode":"sweep","discardUnclassified":false,"checkAllClassifiers":true,"types":[{"name":"first","speciesCode":7,"enableLength":false},{"name":"second","speciesCode":8,"enableLength":false}]})"},
            {"noise-bands", "pamguard.noise-band-monitor", "Noise bands", true,
             R"({"bandType":"thirdOctave","minimumFrequencyHz":100,"maximumFrequencyHz":20000,"referenceFrequencyHz":1000,"iirOrder":6,"outputIntervalSeconds":0.004,"calibrationDbOffsetByChannel":[]})"},
            {"matched-template", "pamguard.matched-template-classifier", "Matched template", true,
             R"({"clickType":101,"normalisationType":2,"peakSearch":false,"peakSmoothing":5,"lengthDb":6,"restrictedBins":128,"channelClassification":0,"classifiers":[{"thresholdToAccept":5000,"normalisation":2,"matchTemplate":{"name":"match","sampleRateHz":48000,"waveform":[1,-1]},"rejectTemplate":{"name":"reject","sampleRateHz":48000,"waveform":[0,0]}}]})"},
            {"trains", "pamguard.click-train", "Trains", true,
             R"({"enabled":true,"maxIciSeconds":0.5,"minClicks":1})"},
            {"mht", "pamguard.mht-click-train", "MHT trains", true,
             R"({"minClicks":1,"classifier":{"enabled":true,"averageSpectrumFftLength":64,"pre":{"chi2Threshold":0,"minClicks":1,"minTimeSeconds":0,"speciesFlag":1}}})"},
            {"clips", "pamguard.clip-generator", "Clips", true,
             R"({"storageMode":"binary","datedSubFolders":true,"requiredHistorySeconds":0,"triggerPolicies":[]})"},
            {"alarm", "pamguard.alarm-event-counter", "Alarm", true,
             R"({"countThreshold":1,"windowSeconds":1,"message":"Click detected"})"},
        },
        {
            {"click-input", {"source", "audio"}, {"clicks", "input"}},
            {"classifier-input", {"clicks", "clicks"}, {"classifier", "clicks"}},
            {"localiser-input", {"classifier", "accepted"}, {"localiser", "clicks"}},
            {"feature-input", {"localiser", "accepted"}, {"features", "clicks"}},
            {"noise-band-input", {"source", "audio"}, {"noise-bands", "input"}},
            {"matched-template-input", {"clicks", "clicks"}, {"matched-template", "clicks"}},
            {"train-input", {"localiser", "accepted"}, {"trains", "clicks"}},
            {"mht-input", {"clicks", "clicks"}, {"mht", "clicks"}},
            {"clip-audio", {"source", "audio"}, {"clips", "audio"}},
            {"alarm-input", {"clicks", "clicks"}, {"alarm", "input"}},
        },
    };
    pamguard::core::ModuleRegistry registry;
    pamguard::core::register_builtin_module_types(registry);
    pamguard::core::ModuleGraph graph(registry);
    const auto validation = graph.validate(document);
    if (!validation.valid()) {
        std::string message = "Click detector node graph failed validation:";
        for (const auto& issue : validation.issues) {
            message += " [" + issue.code + "] " + issue.message;
        }
        throw std::runtime_error(message);
    }
    pamguard::core::ModuleRuntime runtime;
    runtime.configure(document);
    const auto click_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("clicks", "clicks"));
    const auto feature_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("features", "features"));
    const auto train_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("trains", "trains"));
    const auto localisation_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "localiser",
            "localisations"));
    const auto localised_click_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "localiser",
            "accepted"));
    const auto mht_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("mht", "trains"));
    const auto mht_classification_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "mht",
            "classifications"));
    const auto accepted_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("classifier", "accepted"));
    const auto classification_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "classifier",
            "classifications"));
    const auto noise_band_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "noise-bands",
            "measurements"));
    const auto matched_template_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "matched-template",
            "classifications"));
    const auto clip_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("clips", "clips"));
    const auto alarm_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("alarm", "alarms"));
    if (!click_block || !feature_block || !train_block ||
        !localisation_block || !localised_click_block || !mht_block ||
        !mht_classification_block ||
        !accepted_block || !classification_block ||
        !noise_band_block ||
        !matched_template_block || !clip_block || !alarm_block ||
        runtime.data_blocks().size() != 19) {
        throw std::runtime_error("Click detector node did not publish its typed output blocks");
    }
    const auto& accepted_capabilities =
        accepted_block->descriptor().capabilities;
    for (const auto* capability :
         {"detections", "waveform", "overlay", "classified"}) {
        if (std::find(
                accepted_capabilities.begin(),
                accepted_capabilities.end(),
                capability) ==
            accepted_capabilities.end()) {
            throw std::runtime_error(
                "Runtime accepted-click block lost click display "
                "capability " +
                std::string(capability));
        }
    }
    const auto& completed_capabilities =
        localised_click_block->descriptor().capabilities;
    for (const auto* capability :
         {"detections", "waveform", "overlay", "classified", "localised"}) {
        if (std::find(
                completed_capabilities.begin(),
                completed_capabilities.end(),
                capability) ==
            completed_capabilities.end()) {
            throw std::runtime_error(
                "Post-localiser accepted-click block lost capability " +
                std::string(capability));
        }
    }
    std::vector<pamguard::detectors::ClickDetectionResult> detections;
    std::vector<pamguard::detectors::ClickFeatureResult> features;
    std::vector<pamguard::detectors::ClickTrainSummary> trains;
    std::vector<pamguard::core::ClickLocalisationResult> localisations;
    std::vector<pamguard::core::GraphMhtClickTrainResult> mht_trains;
    std::vector<
        pamguard::core::GraphClickTrainClassificationResult>
        mht_classifications;
    std::vector<pamguard::detectors::ClickDetectionResult> accepted;
    std::vector<pamguard::detectors::ClickDetectionResult>
        completed_clicks;
    std::vector<pamguard::detectors::ClickClassificationResult> classifications;
    std::vector<pamguard::core::NoiseBandMeasurement> noise_bands;
    std::vector<pamguard::core::MatchedTemplateClassificationResult>
        matched_templates;
    std::vector<pamguard::core::GraphAudioClip> clips;
    std::vector<pamguard::core::GraphAlarmState> alarms;
    auto subscription = click_block->subscribe([&](const auto& unit) {
        const auto* click =
            std::any_cast<pamguard::detectors::ClickDetectionResult>(&unit.payload);
        if (!click) {
            throw std::runtime_error("Click block payload has the wrong runtime type");
        }
        detections.push_back(*click);
    });
    auto feature_subscription = feature_block->subscribe([&](const auto& unit) {
        const auto* feature =
            std::any_cast<pamguard::detectors::ClickFeatureResult>(&unit.payload);
        if (!feature) {
            throw std::runtime_error("Click feature block payload has the wrong type");
        }
        features.push_back(*feature);
    });
    auto train_subscription = train_block->subscribe([&](const auto& unit) {
        const auto* train =
            std::any_cast<pamguard::detectors::ClickTrainSummary>(&unit.payload);
        if (!train) {
            throw std::runtime_error("Click train block payload has the wrong type");
        }
        trains.push_back(*train);
    });
    auto localisation_subscription =
        localisation_block->subscribe([&](const auto& unit) {
            const auto* localisation =
                std::any_cast<
                    pamguard::core::ClickLocalisationResult>(
                    &unit.payload);
            if (!localisation) {
                throw std::runtime_error(
                    "Click localisation block payload has the wrong type");
            }
            localisations.push_back(*localisation);
        });
    auto mht_subscription = mht_block->subscribe([&](const auto& unit) {
        const auto* train =
            std::any_cast<
                pamguard::core::GraphMhtClickTrainResult>(
                &unit.payload);
        if (!train) {
            throw std::runtime_error(
                "MHT train block payload has the wrong type");
        }
        mht_trains.push_back(*train);
    });
    auto mht_classification_subscription =
        mht_classification_block->subscribe([&](const auto& unit) {
            const auto* classification =
                std::any_cast<
                    pamguard::core::
                        GraphClickTrainClassificationResult>(
                    &unit.payload);
            if (!classification) {
                throw std::runtime_error(
                    "MHT classification payload has the wrong type");
            }
            mht_classifications.push_back(*classification);
        });
    auto accepted_subscription = accepted_block->subscribe([&](const auto& unit) {
        const auto* click =
            std::any_cast<pamguard::detectors::ClickDetectionResult>(
                &unit.payload);
        if (!click) {
            throw std::runtime_error(
                "Accepted-click block payload has the wrong type");
        }
        accepted.push_back(*click);
    });
    auto completed_click_subscription =
        localised_click_block->subscribe([&](const auto& unit) {
            const auto* click =
                std::any_cast<
                    pamguard::detectors::ClickDetectionResult>(
                    &unit.payload);
            if (!click) {
                throw std::runtime_error(
                    "Post-localiser click block payload has the wrong type");
            }
            completed_clicks.push_back(*click);
        });
    auto classification_subscription =
        classification_block->subscribe([&](const auto& unit) {
            const auto* classification =
                std::any_cast<
                    pamguard::detectors::ClickClassificationResult>(
                    &unit.payload);
            if (!classification) {
                throw std::runtime_error(
                    "Classification block payload has the wrong type");
            }
            classifications.push_back(*classification);
        });
    auto noise_band_subscription =
        noise_band_block->subscribe([&](const auto& unit) {
            const auto* measurement =
                std::any_cast<pamguard::core::NoiseBandMeasurement>(
                    &unit.payload);
            if (!measurement) {
                throw std::runtime_error(
                    "Noise-band block payload has the wrong type");
            }
            noise_bands.push_back(*measurement);
        });
    auto matched_template_subscription =
        matched_template_block->subscribe([&](const auto& unit) {
            const auto* classification =
                std::any_cast<
                    pamguard::core::MatchedTemplateClassificationResult>(
                    &unit.payload);
            if (!classification) {
                throw std::runtime_error(
                    "Matched-template block payload has the wrong type");
            }
            matched_templates.push_back(*classification);
        });
    auto clip_subscription = clip_block->subscribe([&](const auto& unit) {
        const auto* clip =
            std::any_cast<pamguard::core::GraphAudioClip>(&unit.payload);
        if (!clip) {
            throw std::runtime_error(
                "Clip block payload has the wrong runtime type");
        }
        clips.push_back(*clip);
    });
    auto alarm_subscription = alarm_block->subscribe([&](const auto& unit) {
        const auto* alarm =
            std::any_cast<pamguard::core::GraphAlarmState>(&unit.payload);
        if (!alarm) {
            throw std::runtime_error(
                "Alarm block payload has the wrong runtime type");
        }
        alarms.push_back(*alarm);
    });
    runtime.start();
    pamguard::core::AudioChunk chunk;
    chunk.sample_rate_hz = 48000;
    chunk.channel_count = 2;
    chunk.interleaved_pcm.resize(256 * chunk.channel_count);
    for (std::size_t sample = 0; sample < 256; ++sample) {
        for (std::size_t channel = 0; channel < chunk.channel_count; ++channel) {
            chunk.interleaved_pcm[sample * chunk.channel_count + channel] =
                synthetic_sample(channel, sample);
        }
    }
    runtime.ingest("source", std::move(chunk));
    runtime.stop();
    if (detections.size() != 1 ||
        detections[0].start_sample != 71 ||
        detections[0].duration_samples != 43) {
        throw std::runtime_error(
            "Click detector graph node changed the existing PAMGuard-parity result");
    }
    if (accepted.size() != 1 ||
        accepted[0].click_type != 7 ||
        accepted[0].classifiers_passed !=
            std::vector<int>{7, 8} ||
        !accepted[0].delays_in_samples.empty() ||
        accepted[0].bearing_radians.has_value() ||
        completed_clicks.size() != 1 ||
        completed_clicks[0].click_type != 7 ||
        completed_clicks[0].classifiers_passed !=
            std::vector<int>{7, 8} ||
        completed_clicks[0].delays_in_samples.size() != 1 ||
        !completed_clicks[0].bearing_radians.has_value()) {
        throw std::runtime_error(
            "completeClick annotations were not ordered classifier then "
            "delay/localisation before accepted-click publication");
    }
    pamguard::localisation::DelayGroupEstimator delay_estimator;
    const auto default_delay = delay_estimator.estimate_delays(
        accepted[0].waveform,
        {33.0},
        48000.0);
    pamguard::localisation::DelayMeasurementConfig type_override;
    type_override.up_sample = 2;
    const auto overridden_delay = delay_estimator.estimate_delays(
        accepted[0].waveform,
        {33.0},
        48000.0,
        type_override);
    if (default_delay.size() != 1 ||
        overridden_delay.size() != 1 ||
        std::abs(
            completed_clicks[0].delays_in_samples[0] -
            overridden_delay[0].delay.delay_samples) > 1e-10 ||
        std::abs(
            default_delay[0].delay.delay_samples -
            overridden_delay[0].delay.delay_samples) < 1e-8) {
        throw std::runtime_error(
            "Click localiser did not select delayMeasurement.typeSettings "
            "from the accepted click type");
    }
    if (features.size() != 1 ||
        features[0].click_start_sample != 71 ||
        accepted[0].start_sample != 71 ||
        classifications.size() != 1 ||
        classifications[0].click_start_sample != 71 ||
        classifications[0].click_type != 7 ||
        classifications[0].classifiers_passed !=
            std::vector<int>{7, 8} ||
        noise_bands.size() != 2 ||
        noise_bands[0].rms_db.empty() ||
        matched_templates.size() != 1 ||
        matched_templates[0].click_start_sample != 71 ||
        !clips.empty() ||
        alarms.size() != 1 ||
        !alarms[0].active ||
        alarms[0].event_count != 1 ||
        localisations.size() != 1 ||
        localisations[0].click_start_sample != 71 ||
        localisations[0].delays.size() != 1 ||
        !localisations[0].delays[0].geometry_constrained ||
        mht_trains.size() != 1 ||
        mht_trains[0].click_count != 1 ||
        !mht_trains[0].classified ||
        mht_classifications.size() != 1 ||
        trains.size() != 1 ||
        trains[0].click_count != 1 ||
        !trains[0].completed) {
        throw std::runtime_error(
            "Click feature/classifier/train graph branches changed existing detector semantics");
    }
}

void check_post_localiser_simple_train_bearing_gate() {
    const auto run = [](double second_bearing_degrees) {
        auto completed_clicks =
            std::make_shared<pamguard::core::DataBlock>(
                pamguard::core::DataBlockDescriptor{
                    "complete-clicks",
                    "Post-localiser accepted clicks",
                    "localiser",
                    "accepted",
                    pamguard::core::kClickDataType,
                    1,
                    48000.0,
                    0x3,
                });
        auto train_output =
            std::make_shared<pamguard::core::DataBlock>(
                pamguard::core::DataBlockDescriptor{
                    "simple-trains",
                    "Simple click trains",
                    "train",
                    "trains",
                    pamguard::core::kClickTrainDataType,
                    1,
                    48000.0,
                    0x3,
                });
        pamguard::core::ClickTrainNodeConfig config;
        config.enabled = true;
        config.tracker.sample_rate_hz = 48000.0;
        config.tracker.min_ici_seconds = 0.05;
        config.tracker.max_ici_seconds = 0.2;
        config.tracker.max_ici_change = 3.0;
        config.tracker.ok_angle_error_degrees = 1.0;
        config.tracker.min_clicks = 2;
        config.tracker.min_update_gap_seconds = 0.0;

        pamguard::core::ClickTrainNode node(
            "train",
            config,
            completed_clicks,
            train_output);
        std::vector<pamguard::detectors::ClickTrainSummary> trains;
        auto subscription = train_output->subscribe(
            [&](const auto& unit) {
                trains.push_back(
                    std::any_cast<
                        pamguard::detectors::ClickTrainSummary>(
                        unit.payload));
            });
        node.prepare();
        node.start();
        const auto publish_click =
            [&](std::uint64_t uid,
                std::int64_t start_sample,
                std::int64_t time_ms,
                double bearing_degrees) {
                pamguard::detectors::ClickDetectionResult click;
                click.channel_bitmap = 0x3;
                click.trigger_bitmap = 0x3;
                click.start_sample = start_sample;
                click.time_unix_ms = time_ms;
                click.bearing_radians =
                    bearing_degrees *
                    std::numbers::pi / 180.0;
                pamguard::core::DataUnitMetadata metadata;
                metadata.uid = uid;
                metadata.sequence = uid;
                metadata.start_sample = start_sample;
                metadata.time_unix_ms = time_ms;
                metadata.channel_bitmap = 0x3;
                completed_clicks->publish(
                    pamguard::core::make_data_unit(
                        std::move(metadata),
                        std::move(click)));
            };
        publish_click(1, 0, 0, 0.0);
        publish_click(
            2,
            4800,
            100,
            second_bearing_degrees);
        node.stop();
        return trains;
    };

    const auto rejected = run(60.0);
    const auto accepted = run(50.0);
    if (!rejected.empty() ||
        accepted.size() != 2 ||
        accepted.front().click_count != 2 ||
        accepted.front().completed ||
        accepted.back().click_count != 2 ||
        !accepted.back().completed) {
        throw std::runtime_error(
            "Simple Click Train Identification did not apply its Java "
            "bearing gate to post-localiser click annotations");
    }
}

void check_matched_template_click_annotations() {
    const auto run =
        [](double threshold, int incoming_click_type) {
            using namespace pamguard::core;
            namespace detectors = pamguard::detectors;
            auto input = std::make_shared<DataBlock>(
                DataBlockDescriptor{
                    "mt-input",
                    "Detected clicks",
                    "clicks",
                    "clicks",
                    kClickDataType,
                    1,
                    48000.0,
                    1,
                });
            auto accepted = std::make_shared<DataBlock>(
                DataBlockDescriptor{
                    "mt-accepted",
                    "Classified clicks",
                    "matched",
                    "accepted",
                    kClickDataType,
                    1,
                    48000.0,
                    1,
                });
            auto classifications =
                std::make_shared<DataBlock>(
                    DataBlockDescriptor{
                        "mt-classifications",
                        "Matched-template classifications",
                        "matched",
                        "classifications",
                        kMatchedTemplateClassificationDataType,
                        1,
                        48000.0,
                        1,
                    });

            MatchedTemplateNodeConfig config;
            config.click_type = 101;
            config.classifier.enabled = true;
            config.classifier.normalisation_type = 2;
            config.classifier.peak_search = false;
            detectors::MtTemplatePair pair;
            pair.threshold_to_accept = threshold;
            pair.normalisation_type = 2;
            pair.match_template = {
                "match",
                48000.0,
                {1.0, -1.0, 0.5, -0.5},
            };
            pair.reject_template = {
                "reject",
                48000.0,
                {0.0, 0.0, 0.0, 0.0},
            };
            config.classifier.classifiers.push_back(
                std::move(pair));

            MatchedTemplateNode node(
                "matched",
                48000.0,
                std::move(config),
                input,
                ClickClassifierNodeOutputs{
                    accepted,
                    classifications,
                });
            std::vector<detectors::ClickDetectionResult>
                annotated_clicks;
            std::vector<MatchedTemplateClassificationResult>
                annotation_results;
            std::uint64_t accepted_uid = 0;
            std::uint64_t classification_uid = 0;
            auto accepted_subscription =
                accepted->subscribe([&](const auto& unit) {
                    accepted_uid = unit.metadata.uid;
                    annotated_clicks.push_back(
                        std::any_cast<
                            detectors::ClickDetectionResult>(
                            unit.payload));
                });
            auto classification_subscription =
                classifications->subscribe(
                    [&](const auto& unit) {
                        classification_uid =
                            unit.metadata.uid;
                        annotation_results.push_back(
                            std::any_cast<
                                MatchedTemplateClassificationResult>(
                                unit.payload));
                    });
            node.prepare();
            node.start();
            detectors::ClickDetectionResult click;
            click.channel_bitmap = 1;
            click.trigger_bitmap = 1;
            click.start_sample = 42;
            click.duration_samples = 4;
            click.click_type = incoming_click_type;
            click.waveform = {
                {1.0, -1.0, 0.5, -0.5},
            };
            DataUnitMetadata metadata;
            metadata.uid = 77;
            metadata.sequence = 77;
            metadata.start_sample = 42;
            metadata.duration_samples = 4;
            metadata.channel_bitmap = 1;
            input->publish(make_data_unit(
                std::move(metadata),
                std::move(click)));
            node.stop();
            if (annotated_clicks.size() != 1 ||
                annotation_results.size() != 1 ||
                accepted_uid != 77 ||
                classification_uid != 77) {
                throw std::runtime_error(
                    "Matched Template did not preserve every click and "
                    "its UID join to the classification result");
            }
            return std::pair{
                std::move(annotated_clicks.front()),
                std::move(annotation_results.front()),
            };
        };

    const auto positive = run(-5000.0, 7);
    const auto preserve = run(5000.0, 7);
    const auto reset = run(5000.0, 101);
    const auto annotation_is_complete =
        [](const auto& outcome, bool classified) {
            const auto& click = outcome.first;
            const auto& result = outcome.second;
            return
                result.classifier_instance_id == "matched" &&
                result.click_type == 101 &&
                result.classification.classified == classified &&
                click.matched_template_annotations.size() == 1 &&
                click.matched_template_annotations[0]
                        .classifier_instance_id == "matched" &&
                click.matched_template_annotations[0].click_type ==
                    101 &&
                click.matched_template_annotations[0].classified ==
                    classified &&
                click.matched_template_annotations[0]
                        .best_results.size() == 1;
        };
    if (positive.first.click_type != 101 ||
        !annotation_is_complete(positive, true) ||
        preserve.first.click_type != 7 ||
        !annotation_is_complete(preserve, false) ||
        reset.first.click_type != 0 ||
        !annotation_is_complete(reset, false)) {
        throw std::runtime_error(
            "Matched Template did not reproduce Java positive override, "
            "negative preserve, stale-type reset, and click annotation "
            "semantics");
    }
}

void check_click_detector_grouping_and_echo_runtime() {
    pamguard::core::ModuleRegistry registry;
    pamguard::core::register_builtin_module_types(registry);
    const auto* descriptor =
        registry.find("pamguard.click-detector");
    if (!descriptor) {
        throw std::runtime_error(
            "Built-in click detector descriptor is missing");
    }
    const auto schema =
        Json::parse(descriptor->settings_schema_json);
    const auto defaults =
        Json::parse(descriptor->default_settings_json);
    const auto& properties = schema.at("properties");
    const auto& required = schema.at("required");
    if (!properties.contains("groupingType") ||
        !properties.contains("channelGroups") ||
        !properties.contains("echo") ||
        std::find(
            required.begin(),
            required.end(),
            "groupingType") == required.end() ||
        std::find(
            required.begin(),
            required.end(),
            "channelGroups") == required.end() ||
        std::find(
            required.begin(),
            required.end(),
            "echo") == required.end() ||
        defaults.at("channelBitmap").get<std::uint32_t>() != 3 ||
        defaults.at("groupingType").get<std::string>() != "all" ||
        !defaults.at("channelGroups").empty() ||
        defaults.at("echo").at("runOnline").get<bool>() ||
        defaults.at("echo").at("discardEchoes").get<bool>() ||
        std::abs(
            defaults.at("echo")
                    .at("maxIntervalSeconds")
                    .get<double>() -
            0.1) > 1e-12) {
        throw std::runtime_error(
            "Click detector descriptor does not expose Java grouping "
            "and echo defaults");
    }
    const auto* classifier_descriptor =
        registry.find("pamguard.click-classifier");
    const auto classifier_schema = classifier_descriptor
        ? Json::parse(classifier_descriptor->settings_schema_json)
        : Json::object();
    const auto classifier_defaults = classifier_descriptor
        ? Json::parse(classifier_descriptor->default_settings_json)
        : Json::object();
    if (!classifier_descriptor ||
        !classifier_schema.at("properties").contains("enabled") ||
        std::find(
            classifier_schema.at("required").begin(),
            classifier_schema.at("required").end(),
            "enabled") ==
            classifier_schema.at("required").end() ||
        classifier_defaults.at("enabled").get<bool>()) {
        throw std::runtime_error(
            "Click classifier descriptor does not expose the "
            "Java runOnline=false default");
    }
    const auto* train_descriptor =
        registry.find("pamguard.click-train");
    const auto train_schema = train_descriptor
        ? Json::parse(train_descriptor->settings_schema_json)
        : Json::object();
    const auto train_defaults = train_descriptor
        ? Json::parse(train_descriptor->default_settings_json)
        : Json::object();
    const auto expected_train_defaults =
        click_train_runtime_settings(false);
    if (!train_descriptor ||
        train_defaults != expected_train_defaults) {
        throw std::runtime_error(
            "Click train descriptor changed the Java "
            "ClickTrainIdParams defaults");
    }
    for (const auto& [key, _] :
         expected_train_defaults.items()) {
        if (!train_schema.at("properties").contains(key) ||
            std::find(
                train_schema.at("required").begin(),
                train_schema.at("required").end(),
                key) ==
                train_schema.at("required").end()) {
            throw std::runtime_error(
                "Click train descriptor omits projected setting " +
                key);
        }
    }
    const auto* click_display =
        registry.find("pamguard.click-display");
    const auto click_display_defaults = click_display
        ? Json::parse(click_display->default_settings_json)
        : Json::object();
    if (!click_display ||
        click_display->ports.size() != 1 ||
        click_display->ports[0].id != "clicks" ||
        click_display->ports[0].direction !=
            pamguard::core::PortDirection::Input ||
        click_display->ports[0].data_type !=
            pamguard::core::kClickDataType ||
        click_display->ports[0].capabilities !=
            std::vector<std::string>{
                "detections",
                "waveform",
                "overlay"} ||
        click_display_defaults.at("timeWindowSeconds")
                .get<double>() != 20.0 ||
        click_display_defaults.at("channelBitmap")
                .get<std::uint32_t>() != 3 ||
        !click_display_defaults.at("showEchoes").get<bool>() ||
        click_display_defaults.at("bearingLimitsDegrees") !=
            Json::array({0, 180}) ||
        click_display_defaults.at("amplitudeLimitsDb") !=
            Json::array({0, 30}) ||
        click_display_defaults.at("iciLimitsSeconds") !=
            Json::array({0.001, 3})) {
        throw std::runtime_error(
            "Built-in click display does not expose its "
            "input-only click compatibility contract");
    }
    auto display_graph_document = click_only_document(
        2,
        click_runtime_settings(3, "all", {}));
    display_graph_document.modules.push_back({
        "classifier",
        "pamguard.click-classifier",
        "Classifier",
        true,
        R"({"enabled":false,"mode":"basic","discardUnclassified":false,"types":[]})",
    });
    display_graph_document.modules.push_back({
        "display",
        "pamguard.click-display",
        "Click display",
        true,
        click_display->default_settings_json,
    });
    display_graph_document.connections.push_back({
        "classifier-input",
        {"clicks", "clicks"},
        {"classifier", "clicks"},
    });
    display_graph_document.connections.push_back({
        "click-display-input",
        {"classifier", "accepted"},
        {"display", "clicks"},
    });
    pamguard::core::ModuleGraph display_graph(registry);
    if (!display_graph.validate(display_graph_document).valid()) {
        throw std::runtime_error(
            "Click display rejected the classifier accepted-click "
            "detections/waveform/overlay output contract");
    }

    auto passthrough_document = click_only_document(
        1,
        click_runtime_settings(1, "all", {}));
    passthrough_document.modules.push_back({
        "classifier",
        "pamguard.click-classifier",
        "Disabled classifier",
        true,
        R"({"enabled":false,"mode":"basic","discardUnclassified":true,"types":[]})",
    });
    passthrough_document.connections.push_back({
        "classifier-input",
        {"clicks", "clicks"},
        {"classifier", "clicks"},
    });
    pamguard::core::ModuleRuntime passthrough_runtime;
    passthrough_runtime.configure(passthrough_document);
    const auto raw_clicks = passthrough_runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "clicks",
            "clicks"));
    const auto accepted_clicks =
        passthrough_runtime.find_block(
            pamguard::core::ModuleRuntime::block_id(
                "classifier",
                "accepted"));
    const auto disabled_classifications =
        passthrough_runtime.find_block(
            pamguard::core::ModuleRuntime::block_id(
                "classifier",
                "classifications"));
    if (!raw_clicks || !accepted_clicks ||
        !disabled_classifications) {
        throw std::runtime_error(
            "Disabled classifier graph did not expose its blocks");
    }
    std::vector<pamguard::core::DataUnit> raw_units;
    std::vector<pamguard::core::DataUnit> accepted_units;
    std::size_t disabled_classification_count = 0;
    auto raw_subscription =
        raw_clicks->subscribe([&](const auto& unit) {
            raw_units.push_back(unit);
        });
    auto accepted_subscription =
        accepted_clicks->subscribe([&](const auto& unit) {
            accepted_units.push_back(unit);
        });
    auto disabled_classification_subscription =
        disabled_classifications->subscribe(
            [&](const auto&) {
                ++disabled_classification_count;
            });
    passthrough_runtime.start();
    passthrough_runtime.ingest(
        "source",
        click_runtime_chunk(1, false));
    passthrough_runtime.stop();
    if (raw_units.size() != 1 ||
        accepted_units.size() != 1 ||
        disabled_classification_count != 0) {
        throw std::runtime_error(
            "Disabled click classifier was not a one-for-one "
            "passthrough");
    }
    const auto* raw_click =
        std::any_cast<
            pamguard::detectors::ClickDetectionResult>(
            &raw_units[0].payload);
    const auto* accepted_click =
        std::any_cast<
            pamguard::detectors::ClickDetectionResult>(
            &accepted_units[0].payload);
    if (!raw_click || !accepted_click ||
        raw_click->channel_bitmap !=
            accepted_click->channel_bitmap ||
        raw_click->trigger_bitmap !=
            accepted_click->trigger_bitmap ||
        raw_click->start_sample !=
            accepted_click->start_sample ||
        raw_click->duration_samples !=
            accepted_click->duration_samples ||
        raw_click->time_unix_ms !=
            accepted_click->time_unix_ms ||
        raw_click->signal_excess_db !=
            accepted_click->signal_excess_db ||
        raw_click->channels != accepted_click->channels ||
        raw_click->waveform != accepted_click->waveform ||
        raw_click->click_type != accepted_click->click_type ||
        raw_click->classifiers_passed !=
            accepted_click->classifiers_passed ||
        raw_click->delays_in_samples !=
            accepted_click->delays_in_samples ||
        raw_click->bearing_radians !=
            accepted_click->bearing_radians ||
        raw_click->echo != accepted_click->echo) {
        throw std::runtime_error(
            "Disabled click classifier changed the click payload");
    }
    const auto& raw_metadata = raw_units[0].metadata;
    const auto& accepted_metadata =
        accepted_units[0].metadata;
    if (raw_metadata.type_id != accepted_metadata.type_id ||
        raw_metadata.schema_version !=
            accepted_metadata.schema_version ||
        raw_metadata.uid != accepted_metadata.uid ||
        raw_metadata.sequence != accepted_metadata.sequence ||
        raw_metadata.time_unix_ms !=
            accepted_metadata.time_unix_ms ||
        raw_metadata.start_sample !=
            accepted_metadata.start_sample ||
        raw_metadata.duration_samples !=
            accepted_metadata.duration_samples ||
        raw_metadata.channel_bitmap !=
            accepted_metadata.channel_bitmap ||
        raw_metadata.sequence_bitmap !=
            accepted_metadata.sequence_bitmap ||
        raw_metadata.clock_domain_id !=
            accepted_metadata.clock_domain_id ||
        raw_metadata.discontinuity !=
            accepted_metadata.discontinuity ||
        accepted_metadata.source_block_id !=
            accepted_clicks->descriptor().id) {
        throw std::runtime_error(
            "Disabled click classifier changed passthrough "
            "metadata");
    }

    struct LocaliserGateProbe {
        std::vector<
            pamguard::detectors::ClickDetectionResult> accepted;
        std::size_t localisation_count = 0;
        std::size_t bearing_count = 0;
    };
    const auto run_no_bearing_localiser =
        [&](double veto_start, double veto_end) {
            auto document = click_only_document(
                1,
                click_runtime_settings(1, "all", {}));
            document.modules.push_back({
                "classifier",
                "pamguard.click-classifier",
                "Disabled classifier",
                true,
                R"({"enabled":false,"mode":"basic","discardUnclassified":false,"types":[]})",
            });
            Json localiser_settings = {
                {"preSample", 40},
                {"speedOfSoundMps", 1500.0},
                {
                    "hydrophones",
                    Json::array({
                        {
                            {"channel", 0},
                            {"xM", 0.0},
                            {"yM", 0.0},
                            {"zM", 0.0},
                        },
                    }),
                },
                {
                    "delayMeasurement",
                    {
                        {"filterBearings", false},
                        {"filterBand", "highPass"},
                        {"filterHighPassHz", 0.0},
                        {"filterLowPassHz", 0.0},
                        {"envelopeBearings", false},
                        {"useLeadingEdge", false},
                        {"upSample", 1},
                        {"useRestrictedBins", false},
                        {"restrictedBins", 80},
                        {"typeSettings", Json::array()},
                    },
                },
                {
                    "angleVetoes",
                    Json::array({
                        {
                            {"channels", 0},
                            {"startAngleDegrees", veto_start},
                            {"endAngleDegrees", veto_end},
                        },
                    }),
                },
            };
            document.modules.push_back({
                "localiser",
                "pamguard.click-localiser",
                "Complete click",
                true,
                localiser_settings.dump(),
            });
            document.connections.push_back({
                "classifier-input",
                {"clicks", "clicks"},
                {"classifier", "clicks"},
            });
            document.connections.push_back({
                "localiser-input",
                {"classifier", "accepted"},
                {"localiser", "clicks"},
            });

            pamguard::core::ModuleRuntime runtime;
            runtime.configure(document);
            const auto completed = runtime.find_block(
                pamguard::core::ModuleRuntime::block_id(
                    "localiser",
                    "accepted"));
            const auto localisations = runtime.find_block(
                pamguard::core::ModuleRuntime::block_id(
                    "localiser",
                    "localisations"));
            const auto bearings = runtime.find_block(
                pamguard::core::ModuleRuntime::block_id(
                    "localiser",
                    "bearings"));
            if (!completed || !localisations || !bearings) {
                throw std::runtime_error(
                    "No-bearing click localiser omitted an output block");
            }
            LocaliserGateProbe probe;
            auto completed_subscription =
                completed->subscribe([&](const auto& unit) {
                    probe.accepted.push_back(
                        std::any_cast<
                            pamguard::detectors::ClickDetectionResult>(
                            unit.payload));
                });
            auto localisation_subscription =
                localisations->subscribe([&](const auto&) {
                    ++probe.localisation_count;
                });
            auto bearing_subscription =
                bearings->subscribe([&](const auto&) {
                    ++probe.bearing_count;
                });
            runtime.start();
            runtime.ingest(
                "source",
                click_runtime_chunk(1, false));
            runtime.stop();
            return probe;
        };

    const auto no_bearing =
        run_no_bearing_localiser(1.0, 2.0);
    if (no_bearing.accepted.size() != 1 ||
        !no_bearing.accepted[0].bearing_radians.has_value() ||
        *no_bearing.accepted[0].bearing_radians != 0.0 ||
        no_bearing.localisation_count != 0 ||
        no_bearing.bearing_count != 0) {
        throw std::runtime_error(
            "Java no-localisation getAngle()=0 semantics were not "
            "attached before publication");
    }
    const auto zero_boundary =
        run_no_bearing_localiser(0.0, 0.0);
    if (!zero_boundary.accepted.empty() ||
        zero_boundary.localisation_count != 0 ||
        zero_boundary.bearing_count != 0) {
        throw std::runtime_error(
            "Inclusive Java angle-veto boundary did not discard the "
            "no-bearing zero-degree click before publication");
    }
    if (pamguard::detectors::ClickAngleVetoes::pass_all(
            {{
                .channels = 0,
                .start_angle_degrees = 10.0,
                .end_angle_degrees = 20.0,
            }},
            -15.0)) {
        throw std::runtime_error(
            "Java angle veto did not compare the absolute bearing");
    }

    const auto require_group_bitmaps =
        [](const auto& clicks,
           std::vector<std::uint32_t> expected,
           const char* grouping) {
            std::vector<std::uint32_t> actual;
            actual.reserve(clicks.size());
            for (const auto& click : clicks) {
                actual.push_back(click.channel_bitmap);
            }
            std::sort(actual.begin(), actual.end());
            std::sort(expected.begin(), expected.end());
            if (actual != expected) {
                throw std::runtime_error(
                    std::string("Click detector ") + grouping +
                    " grouping did not create one independent "
                    "detector per Java channel group");
            }
        };

    require_group_bitmaps(
        run_click_only_runtime(
            4,
            click_runtime_settings(15, "all", {})),
        {15},
        "all");
    require_group_bitmaps(
        run_click_only_runtime(
            4,
            click_runtime_settings(15, "singles", {})),
        {1, 2, 4, 8},
        "singles");
    require_group_bitmaps(
        run_click_only_runtime(
            4,
            click_runtime_settings(
                15,
                "user",
                {0, 0, 1, 1})),
        {3, 12},
        "user");

    auto incomplete_user =
        click_runtime_settings(3, "user", {0});
    bool rejected = false;
    try {
        pamguard::core::ModuleRuntime invalid_runtime;
        invalid_runtime.configure(
            click_only_document(2, incomplete_user));
    }
    catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected) {
        throw std::runtime_error(
            "Click detector accepted user groups that do not "
            "cover every selected channel");
    }

    auto invalid_group =
        click_runtime_settings(3, "user", {0, -1});
    rejected = false;
    try {
        pamguard::core::ModuleRuntime invalid_runtime;
        invalid_runtime.configure(
            click_only_document(2, invalid_group));
    }
    catch (const std::invalid_argument&) {
        rejected = true;
    }
    if (!rejected) {
        throw std::runtime_error(
            "Click detector accepted an invalid Java group number");
    }

    const auto train_document =
        [](const Json& train_settings) {
            auto document = click_only_document(
                1,
                click_runtime_settings(1, "all", {}));
            document.modules.push_back({
                "trains",
                "pamguard.click-train",
                "Click trains",
                true,
                train_settings.dump(),
            });
            document.connections.push_back({
                "train-input",
                {"clicks", "clicks"},
                {"trains", "clicks"},
            });
            return document;
        };
    auto disabled_train_settings =
        click_train_runtime_settings(false);
    disabled_train_settings["minClicks"] = 1;
    pamguard::core::ModuleRuntime disabled_train_runtime;
    disabled_train_runtime.configure(
        train_document(disabled_train_settings));
    const auto disabled_trains =
        disabled_train_runtime.find_block(
            pamguard::core::ModuleRuntime::block_id(
                "trains",
                "trains"));
    if (!disabled_trains) {
        throw std::runtime_error(
            "Disabled click train graph omitted its output block");
    }
    std::size_t disabled_train_count = 0;
    auto disabled_train_subscription =
        disabled_trains->subscribe([&](const auto&) {
            ++disabled_train_count;
        });
    disabled_train_runtime.start();
    disabled_train_runtime.ingest(
        "source",
        click_runtime_chunk(1, false));
    disabled_train_runtime.stop();
    if (disabled_train_count != 0) {
        throw std::runtime_error(
            "Disabled Click Train Identification produced a train");
    }

    auto zero_distance_settings =
        click_train_runtime_settings(false);
    zero_distance_settings[
        "initialPerpendicularDistanceM"] = 0.0;
    pamguard::core::ModuleRuntime zero_distance_runtime;
    zero_distance_runtime.configure(
        train_document(zero_distance_settings));

    const std::vector<std::pair<std::string, Json>>
        invalid_train_settings{
            {"minIciSeconds", -0.1},
            {"maxIciSeconds", 0.0},
            {"maxIciChange", 0.5},
            {"okAngleErrorDegrees", -1.0},
            {"initialPerpendicularDistanceM", -1.0},
            {"minClicks", 0},
            {"minAngleChangeDegrees", -1.0},
            {"iciUpdateRatio", 1.1},
            {"minUpdateGapSeconds", -1.0},
        };
    for (const auto& [key, value] :
         invalid_train_settings) {
        auto invalid_settings =
            click_train_runtime_settings(true);
        invalid_settings[key] = value;
        rejected = false;
        try {
            pamguard::core::ModuleRuntime invalid_runtime;
            invalid_runtime.configure(
                train_document(invalid_settings));
        }
        catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (!rejected) {
            throw std::runtime_error(
                "Click train runtime did not parse/validate " +
                key);
        }
    }

    struct EchoProbe {
        std::vector<
            pamguard::detectors::ClickDetectionResult> clicks;
        std::size_t feature_count = 0;
        std::size_t accepted_count = 0;
        std::size_t classification_count = 0;
        std::vector<
            pamguard::detectors::ClickTrainSummary> trains;
    };
    const auto run_echo_graph =
        [&](bool discard_echoes) {
            const auto settings = click_runtime_settings(
                1,
                "all",
                {},
                true,
                discard_echoes,
                0.11);
            auto train_settings =
                click_train_runtime_settings(true);
            train_settings["minIciSeconds"] = 0.05;
            train_settings["maxIciSeconds"] = 0.5;
            train_settings["minClicks"] = 1;
            pamguard::core::ModuleGraphDocument document{
                1,
                1,
                {
                    {"source", "pamguard.acquisition", "Input", true,
                     R"({"sourceId":"echo-runtime-test","sampleRateHz":48000,"channelCount":1,"subtractDC":false,"dcTimeConstantSeconds":1})"},
                    {"clicks", "pamguard.click-detector", "Clicks", true,
                     settings.dump()},
                    {"features", "pamguard.click-features", "Features", true,
                     R"({"fftLength":64,"lengthEnergyFraction":90.0,"widthEnergyFraction":90.0,"energyBandsHz":[[1000,6000],[6000,14000]],"peakFrequencySearchHz":[500,20000],"meanFrequencyRangeHz":[500,20000]})"},
                    {"classifier", "pamguard.click-classifier", "Classifier", true,
                     R"({"enabled":true,"mode":"basic","discardUnclassified":false,"types":[]})"},
                    {"trains", "pamguard.click-train", "Trains", true,
                     train_settings.dump()},
                },
                {
                    {"click-input", {"source", "audio"}, {"clicks", "input"}},
                    {"feature-input", {"clicks", "clicks"}, {"features", "clicks"}},
                    {"classifier-input", {"clicks", "clicks"}, {"classifier", "clicks"}},
                    {"train-input", {"clicks", "clicks"}, {"trains", "clicks"}},
                },
            };
            pamguard::core::ModuleGraph graph(registry);
            const auto validation = graph.validate(document);
            if (!validation.valid()) {
                throw std::runtime_error(
                    "Click echo downstream graph failed validation");
            }

            pamguard::core::ModuleRuntime runtime;
            runtime.configure(document);
            const auto click_block = runtime.find_block(
                pamguard::core::ModuleRuntime::block_id(
                    "clicks",
                    "clicks"));
            const auto feature_block = runtime.find_block(
                pamguard::core::ModuleRuntime::block_id(
                    "features",
                    "features"));
            const auto accepted_block = runtime.find_block(
                pamguard::core::ModuleRuntime::block_id(
                    "classifier",
                    "accepted"));
            const auto classification_block = runtime.find_block(
                pamguard::core::ModuleRuntime::block_id(
                    "classifier",
                    "classifications"));
            const auto train_block = runtime.find_block(
                pamguard::core::ModuleRuntime::block_id(
                    "trains",
                    "trains"));
            if (!click_block || !feature_block ||
                !accepted_block || !classification_block ||
                !train_block) {
                throw std::runtime_error(
                    "Click echo graph did not expose downstream blocks");
            }

            EchoProbe probe;
            auto click_subscription =
                click_block->subscribe([&](const auto& unit) {
                    const auto* click =
                        std::any_cast<
                            pamguard::detectors::
                                ClickDetectionResult>(
                            &unit.payload);
                    if (!click) {
                        throw std::runtime_error(
                            "Echo click payload has the wrong type");
                    }
                    probe.clicks.push_back(*click);
                });
            auto feature_subscription =
                feature_block->subscribe([&](const auto&) {
                    ++probe.feature_count;
                });
            auto accepted_subscription =
                accepted_block->subscribe([&](const auto&) {
                    ++probe.accepted_count;
                });
            auto classification_subscription =
                classification_block->subscribe([&](const auto&) {
                    ++probe.classification_count;
                });
            auto train_subscription =
                train_block->subscribe([&](const auto& unit) {
                    const auto* train =
                        std::any_cast<
                            pamguard::detectors::
                                ClickTrainSummary>(
                            &unit.payload);
                    if (!train) {
                        throw std::runtime_error(
                            "Echo train payload has the wrong type");
                    }
                    probe.trains.push_back(*train);
                });
            runtime.start();
            runtime.ingest(
                "source",
                click_runtime_chunk(1, true));
            runtime.stop();
            return probe;
        };

    const auto flagged = run_echo_graph(false);
    if (flagged.clicks.size() != 2 ||
        flagged.clicks[0].echo ||
        !flagged.clicks[1].echo ||
        flagged.feature_count != 2 ||
        flagged.accepted_count != 2 ||
        flagged.classification_count != 2 ||
        flagged.trains.empty() ||
        flagged.trains.back().click_count != 2 ||
        !flagged.trains.back().completed ||
        std::any_of(
            flagged.trains.begin(),
            flagged.trains.end(),
            [](const auto& train) {
                return train.click_count != 2;
            })) {
        throw std::runtime_error(
            "Online Simple Echo flag mode did not preserve both "
            "clicks through downstream graph branches: clicks=" +
            std::to_string(flagged.clicks.size()) +
            ", features=" +
            std::to_string(flagged.feature_count) +
            ", accepted=" +
            std::to_string(flagged.accepted_count) +
            ", classifications=" +
            std::to_string(flagged.classification_count) +
            ", trains=" +
            std::to_string(flagged.trains.size()) +
            ", train-clicks=" +
            std::to_string(
                flagged.trains.empty()
                    ? 0
                    : flagged.trains.back().click_count));
    }

    const auto discarded = run_echo_graph(true);
    if (discarded.clicks.size() != 1 ||
        discarded.clicks[0].echo ||
        discarded.feature_count != 1 ||
        discarded.accepted_count != 1 ||
        discarded.classification_count != 1 ||
        discarded.trains.empty() ||
        discarded.trains.back().click_count != 1 ||
        !discarded.trains.back().completed ||
        std::any_of(
            discarded.trains.begin(),
            discarded.trains.end(),
            [](const auto& train) {
                return train.click_count != 1;
            })) {
        throw std::runtime_error(
            "Online Simple Echo discard mode did not gate echoes "
            "before features, classifier, and click trains: clicks=" +
            std::to_string(discarded.clicks.size()) +
            ", features=" +
            std::to_string(discarded.feature_count) +
            ", accepted=" +
            std::to_string(discarded.accepted_count) +
            ", classifications=" +
            std::to_string(discarded.classification_count) +
            ", trains=" +
            std::to_string(discarded.trains.size()) +
            ", train-clicks=" +
            std::to_string(
                discarded.trains.empty()
                    ? 0
                    : discarded.trains.back().click_count));
    }
}

void check_mht_click_train_graph_node() {
    pamguard::core::ModuleGraphDocument document{
        1,
        5,
        {
            {"source", "pamguard.acquisition", "Input", true,
             R"({"sourceId":"test","sampleRateHz":48000,"channelCount":2,"subtractDC":false,"dcTimeConstantSeconds":1})"},
            {"clicks", "pamguard.click-detector", "Clicks", true,
             R"({"channelBitmap":3,"triggerBitmap":3,"minTriggerChannels":1,"thresholdDb":10.0,"longFilter":0.00001,"longFilter2":0.000001,"shortFilter":0.1,"preSample":10,"postSample":12,"minSep":8,"maxLength":128,"sampleNoise":false,"storeBackground":false,"publishTriggerFunction":false,"preFilter":{"type":"none"},"triggerFilter":{"type":"none"}})"},
            {"mht", "pamguard.mht-click-train", "MHT", true,
             R"({"minClicks":3,"classifier":{"enabled":true,"averageSpectrumFftLength":64,"pre":{"chi2Threshold":0,"minClicks":3,"minTimeSeconds":0,"speciesFlag":1},"idi":{"enabled":true,"useMedianIdi":true,"minMedianIdi":0.05,"maxMedianIdi":0.15,"useMeanIdi":false,"useStdIdi":false,"speciesFlag":42}}})"},
        },
        {
            {"click-input", {"source", "audio"}, {"clicks", "input"}},
            {"mht-input", {"clicks", "clicks"}, {"mht", "clicks"}},
        },
    };
    pamguard::core::ModuleRegistry registry;
    pamguard::core::register_builtin_module_types(registry);
    pamguard::core::ModuleGraph graph(registry);
    if (!graph.validate(document).valid()) {
        throw std::runtime_error("MHT graph failed registry validation");
    }
    pamguard::core::ModuleRuntime runtime;
    runtime.configure(document);
    const auto train_block = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("mht", "trains"));
    if (!train_block) {
        throw std::runtime_error("MHT graph did not expose its train block");
    }
    std::vector<pamguard::core::GraphMhtClickTrainResult> trains;
    auto subscription = train_block->subscribe([&](const auto& unit) {
        const auto* train =
            std::any_cast<
                pamguard::core::GraphMhtClickTrainResult>(
                &unit.payload);
        if (!train) {
            throw std::runtime_error("MHT graph train payload is invalid");
        }
        trains.push_back(*train);
    });
    runtime.start();
    pamguard::core::AudioChunk chunk;
    chunk.sample_rate_hz = 48000;
    chunk.channel_count = 2;
    chunk.interleaved_pcm.assign(48000 * 2, 0.0);
    for (std::size_t sample = 0; sample < 48000; ++sample) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            double value = 0.01 * std::sin(
                static_cast<double>(sample) * 0.13 +
                static_cast<double>(channel) * 0.31);
            const auto position =
                sample >= 4000
                ? (sample - 4000) % 4800
                : std::size_t{4800};
            if (position <= 6 &&
                sample >= 4000 &&
                sample < 4000 + 8 * 4800) {
                value +=
                    ((sample & 1u) == 0 ? 1.0 : -1.0) *
                    (channel == 0 ? 1.0 : 0.82);
            }
            chunk.interleaved_pcm[sample * 2 + channel] = value;
        }
    }
    runtime.ingest("source", std::move(chunk));
    runtime.stop();
    std::size_t best_count = 0;
    bool classified = false;
    for (const auto& train : trains) {
        best_count = std::max(best_count, train.click_count);
        classified =
            classified ||
            (!train.junk_train && train.species_id == 42);
    }
    if (best_count < 6 || !classified) {
        throw std::runtime_error(
            "Graph-wrapped MHT or IDI classifier changed session-parity behavior");
    }
}

void check_fft_detector_graph_nodes() {
    auto energy_settings = Json::parse(
        pamguard::core::
            ishmael_energy_sum_runtime_default_settings_json());
    energy_settings["channelBitmap"] = 1;
    energy_settings["activeChannelBitmap"] = 1;
    energy_settings["f1Hz"] = 4;
    energy_settings["threshold"] = 1000000;
    auto sgram_settings = Json::parse(
        pamguard::core::
            ishmael_sgram_corr_runtime_default_settings_json());
    sgram_settings["channelBitmap"] = 1;
    sgram_settings["activeChannelBitmap"] = 1;
    sgram_settings["segments"] =
        Json::array({Json::array({0, 0, 0.25, 2})});
    sgram_settings["spreadHz"] = 1;
    sgram_settings["threshold"] = 1000000;
    pamguard::core::ModuleGraphDocument document{
        1,
        4,
        {
            {"source", "pamguard.acquisition", "Input", true,
             R"({"sourceId":"test","sampleRateHz":8,"channelCount":1,"subtractDC":false,"dcTimeConstantSeconds":1})"},
            {"fft", "pamguard.fft", "FFT", true,
             R"({"fftLength":4,"fftHop":2,"channels":[0]})"},
            {"noise", "pamguard.fft-noise-monitor", "Noise", true,
             R"({"fftLength":4,"fftHop":2,"channels":[0],"measurementIntervalSeconds":1,"nMeasures":2,"useAll":true,"bands":[{"name":"Band","lowFrequencyHz":0,"highFrequencyHz":4}]})"},
            {"ltsa", "pamguard.ltsa", "LTSA", true,
             R"({"intervalSeconds":1})"},
            {"ishmael", "pamguard.ishmael-energy-sum", "Ishmael", true,
             energy_settings.dump()},
            {"whistles", "pamguard.whistles-moans", "Whistles", true,
             R"({"channelBitmap":1,"groupingType":"all","channelGroups":[],"minFrequencyHz":0,"maxFrequencyHz":0,"connectType":8,"minLength":1,"minPixels":1,"keepShapeStubs":true,"fragmentationMethod":0,"maxCrossLength":5})"},
            {"sgram", "pamguard.ishmael-sgram-corr", "Sgram", true,
             sgram_settings.dump()},
            {"match-filter", "pamguard.ishmael-match-filter", "Match filter", true,
             R"({"kernel":[1,0,-1,0],"channels":[0],"threshold":1000000,"minTimeSeconds":0,"maxTimeSeconds":99999,"refractoryTimeSeconds":0})"},
        },
        {
            {"fft-input", {"source", "audio"}, {"fft", "input"}},
            {"noise-input", {"fft", "fft"}, {"noise", "input"}},
            {"ltsa-input", {"fft", "fft"}, {"ltsa", "input"}},
            {"ishmael-input", {"fft", "fft"}, {"ishmael", "input"}},
            {"whistle-input", {"fft", "fft"}, {"whistles", "input"}},
            {"sgram-input", {"fft", "fft"}, {"sgram", "input"}},
            {"match-filter-input", {"source", "audio"}, {"match-filter", "input"}},
        },
    };
    pamguard::core::ModuleRegistry registry;
    pamguard::core::register_builtin_module_types(registry);
    pamguard::core::ModuleGraph graph(registry);
    if (!graph.validate(document).valid()) {
        throw std::runtime_error("FFT detector graph failed validation");
    }
    pamguard::core::ModuleRuntime runtime;
    runtime.configure(document);
    const auto noise = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("noise", "measurements"));
    const auto ltsa = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("ltsa", "ltsa"));
    const auto function = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("ishmael", "function"));
    const auto detections = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id("ishmael", "detections"));
    const auto contours = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "whistles",
            "contours"));
    const auto sgram_function = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "sgram",
            "function"));
    const auto match_function = runtime.find_block(
        pamguard::core::ModuleRuntime::block_id(
            "match-filter",
            "function"));
    if (!noise || !ltsa || !function || !detections ||
        !contours ||
        !sgram_function || !match_function ||
        runtime.data_blocks().size() != 11) {
        throw std::runtime_error(
            "FFT detector nodes did not expose their typed blocks");
    }
    std::size_t noise_periods = 0;
    std::size_t ltsa_periods = 0;
    std::size_t function_samples = 0;
    std::size_t contour_count = 0;
    std::size_t sgram_samples = 0;
    std::size_t match_samples = 0;
    auto noise_subscription =
        noise->subscribe([&](const auto&) { ++noise_periods; });
    auto ltsa_subscription =
        ltsa->subscribe([&](const auto&) { ++ltsa_periods; });
    auto function_subscription =
        function->subscribe([&](const auto&) { ++function_samples; });
    auto contour_subscription =
        contours->subscribe([&](const auto&) { ++contour_count; });
    auto sgram_subscription =
        sgram_function->subscribe([&](const auto&) { ++sgram_samples; });
    auto match_subscription =
        match_function->subscribe([&](const auto&) { ++match_samples; });
    runtime.start();
    pamguard::core::AudioChunk chunk;
    // Java WhistleToneConnectProcess deliberately rejects contours which
    // begin in the first quarter-second of a stream.
    chunk.start_sample = 4;
    chunk.sample_rate_hz = 8;
    chunk.channel_count = 1;
    chunk.interleaved_pcm.resize(24);
    for (std::size_t sample = 0; sample < 24; ++sample) {
        chunk.interleaved_pcm[sample] =
            std::sin(static_cast<double>(sample) * 0.4);
    }
    runtime.ingest("source", std::move(chunk));
    runtime.stop();
    if (noise_periods == 0 || ltsa_periods == 0 ||
        function_samples != 11 ||
        contour_count == 0 || sgram_samples == 0 ||
        match_samples == 0) {
        throw std::runtime_error(
            "FFT detector graph did not execute continuously");
    }
}

void check_integrated_stop_finalization() {
    using namespace pamguard::core;

    ScopedTemporaryDirectory recording_directory(
        "pamguard-runtime-stop-finalization");
    auto portable_recorder_settings =
        SoundRecorderSettings{};
    portable_recorder_settings.file_initials =
        "lifecycle";
    const auto recorder_settings =
        Json({
            {
                "directory",
                recording_directory.path().string(),
            },
            {"startTransport", "off"},
            {
                "settings",
                Json::parse(
                    sound_recorder_settings_to_json(
                        portable_recorder_settings)),
            },
        }).dump();
    ModuleGraphDocument document{
        1,
        11,
        {
            {"click-source", "pamguard.acquisition", "Click input", true,
             R"({"sourceId":"lifecycle-click","sampleRateHz":48000,"channelCount":2,"subtractDC":false,"dcTimeConstantSeconds":1})"},
            {"clicks", "pamguard.click-detector", "Clicks", true,
             R"({"channelBitmap":3,"triggerBitmap":3,"minTriggerChannels":1,"thresholdDb":10.0,"longFilter":0.00001,"longFilter2":0.000001,"shortFilter":0.1,"preSample":10,"postSample":12,"minSep":8,"maxLength":128,"sampleNoise":false,"storeBackground":false,"publishTriggerFunction":false,"preFilter":{"type":"none"},"triggerFilter":{"type":"none"}})"},
            {"trains", "pamguard.click-train", "Pending trains", true,
             R"({"enabled":true,"maxIciSeconds":0.5,"minClicks":1})"},
            {"clips", "pamguard.clip-generator", "Idle clips", true,
             R"({"storageMode":"binary","datedSubFolders":true,"requiredHistorySeconds":0,"triggerPolicies":[]})"},
            {"recorder", "pamguard.sound-recorder", "Open recorder", true,
             recorder_settings},
            {"whistle-source", "pamguard.acquisition", "Whistle input", true,
             R"({"sourceId":"lifecycle-whistle","sampleRateHz":8,"channelCount":1,"subtractDC":false,"dcTimeConstantSeconds":1})"},
            {"fft", "pamguard.fft", "Whistle FFT", true,
             R"({"fftLength":4,"fftHop":2,"channels":[0]})"},
            {"whistles", "pamguard.whistles-moans", "Pending contours", true,
             R"({"channelBitmap":1,"groupingType":"all","channelGroups":[],"minFrequencyHz":0,"maxFrequencyHz":0,"connectType":8,"minLength":1,"minPixels":1,"keepShapeStubs":true,"fragmentationMethod":0,"maxCrossLength":5})"},
        },
        {
            {"click-input",
             {"click-source", "audio"},
             {"clicks", "input"}},
            {"train-input",
             {"clicks", "clicks"},
             {"trains", "clicks"}},
            {"clip-audio",
             {"click-source", "audio"},
             {"clips", "audio"}},
            {"recorder-input",
             {"click-source", "audio"},
             {"recorder", "input"}},
            {"fft-input",
             {"whistle-source", "audio"},
             {"fft", "input"}},
            {"whistle-input",
             {"fft", "fft"},
             {"whistles", "input"}},
        },
    };

    ModuleRegistry registry;
    register_builtin_module_types(registry);
    ModuleGraph graph(registry);
    const auto validation = graph.validate(document);
    if (!validation.valid()) {
        std::string message =
            "Integrated lifecycle graph failed validation:";
        for (const auto& issue : validation.issues) {
            message +=
                " [" + issue.code + "] " + issue.message;
        }
        throw std::runtime_error(message);
    }

    ModuleRuntime runtime;
    runtime.configure(document);
    const auto click_block = runtime.find_block(
        ModuleRuntime::block_id("clicks", "clicks"));
    const auto train_block = runtime.find_block(
        ModuleRuntime::block_id("trains", "trains"));
    const auto clip_block = runtime.find_block(
        ModuleRuntime::block_id("clips", "clips"));
    const auto recording_block = runtime.find_block(
        ModuleRuntime::block_id("recorder", "recordings"));
    const auto contour_block = runtime.find_block(
        ModuleRuntime::block_id("whistles", "contours"));
    if (!click_block || !train_block || !clip_block ||
        !recording_block || !contour_block) {
        throw std::runtime_error(
            "Integrated lifecycle graph omitted a finalizable output block");
    }

    std::vector<pamguard::detectors::ClickDetectionResult> clicks;
    std::vector<pamguard::detectors::ClickTrainSummary> trains;
    std::vector<GraphAudioClip> clips;
    std::vector<GraphRecordingEvent> recordings;
    std::vector<pamguard::detectors::ConnectedRegionResult>
        contours;
    auto click_subscription =
        click_block->subscribe([&](const DataUnit& unit) {
            clicks.push_back(
                std::any_cast<
                    pamguard::detectors::ClickDetectionResult>(
                    unit.payload));
        });
    auto train_subscription =
        train_block->subscribe([&](const DataUnit& unit) {
            trains.push_back(
                std::any_cast<
                    pamguard::detectors::ClickTrainSummary>(
                    unit.payload));
        });
    auto clip_subscription =
        clip_block->subscribe([&](const DataUnit& unit) {
            clips.push_back(
                std::any_cast<GraphAudioClip>(unit.payload));
        });
    auto recording_subscription =
        recording_block->subscribe([&](const DataUnit& unit) {
            recordings.push_back(
                std::any_cast<GraphRecordingEvent>(
                    unit.payload));
        });
    auto contour_subscription =
        contour_block->subscribe([&](const DataUnit& unit) {
            contours.push_back(
                std::any_cast<
                    pamguard::detectors::ConnectedRegionResult>(
                    unit.payload));
        });

    runtime.start();
    const auto recorder_command =
        runtime.set_sound_recorder_transport(
            "recorder",
            SoundRecorderTransportState::Continuous);
    if (recorder_command != SoundRecorderCommandResult::Applied) {
        throw std::runtime_error(
            "Integrated lifecycle could not start Recorder explicitly");
    }
    AudioChunk click_audio;
    click_audio.start_sample = 0;
    click_audio.time_unix_ms = 1000;
    click_audio.sample_rate_hz = 48000;
    click_audio.channel_count = 2;
    click_audio.interleaved_pcm.resize(
        256 * click_audio.channel_count);
    for (std::size_t sample = 0; sample < 256; ++sample) {
        for (std::size_t channel = 0;
             channel < click_audio.channel_count;
             ++channel) {
            click_audio.interleaved_pcm[
                sample * click_audio.channel_count + channel] =
                synthetic_sample(channel, sample);
        }
    }
    runtime.ingest("click-source", std::move(click_audio));

    AudioChunk whistle_audio;
    // Keep the pending contour beyond Java's first-quarter-second guard.
    whistle_audio.start_sample = 4;
    whistle_audio.time_unix_ms = 2000;
    whistle_audio.sample_rate_hz = 8;
    whistle_audio.channel_count = 1;
    whistle_audio.interleaved_pcm.resize(24);
    for (std::size_t sample = 0;
         sample < whistle_audio.interleaved_pcm.size();
         ++sample) {
        whistle_audio.interleaved_pcm[sample] =
            std::sin(static_cast<double>(sample) * 0.4);
    }
    runtime.ingest("whistle-source", std::move(whistle_audio));

    if (clicks.size() != 1 || !trains.empty() ||
        !clips.empty() || !recordings.empty() ||
        !contours.empty()) {
        throw std::runtime_error(
            "Lifecycle fixture did not leave every expected result pending");
    }

    runtime.stop();
    const auto stopped_statuses = runtime.module_statuses();
    if (runtime.running() ||
        std::any_of(
            stopped_statuses.begin(),
            stopped_statuses.end(),
            [](const auto& status) {
                return status.state != ModuleState::Stopped;
            })) {
        throw std::runtime_error(
            "Integrated lifecycle reported idle before every node stopped");
    }
    const auto completed_recording =
        recordings.empty()
        ? std::filesystem::path{}
        : recording_directory.path() /
            recordings.front().path;
    if (trains.size() != 1 ||
        trains.front().click_count != 1 ||
        !trains.front().completed ||
        !clips.empty() ||
        contours.empty() ||
        recordings.size() != 1 ||
        recordings.front().state != "flushed" ||
        recordings.front().frame_count != 256 ||
        recordings.front().path.empty() ||
        std::filesystem::path(recordings.front().path).is_absolute() ||
        !std::filesystem::is_regular_file(
            completed_recording) ||
        std::filesystem::file_size(
            completed_recording) !=
            44 + 256 * 2 * sizeof(std::int16_t)) {
        throw std::runtime_error(
            "Stop returned clean idle before pending scientific and recorder outputs finalized, or emitted an incomplete clip");
    }
}

void check_operator_support_nodes() {
    using namespace pamguard::core;
    auto raw = std::make_shared<DataBlock>(
        DataBlockDescriptor{
            "operator-raw",
            "Raw audio",
            "source",
            "audio",
            kRawAudioDataType,
            1,
            8.0,
            1,
        });
    auto levels = std::make_shared<DataBlock>(
        DataBlockDescriptor{
            "operator-levels",
            "Levels",
            "levels",
            "levels",
            kLevelMeasurementDataType,
            1,
            8.0,
            1,
        });
    auto recordings = std::make_shared<DataBlock>(
        DataBlockDescriptor{
            "operator-recordings",
            "Recordings",
            "recorder",
            "recordings",
            kRecordingEventDataType,
            1,
            8.0,
            1,
        });
    auto click_events = std::make_shared<DataBlock>(
        DataBlockDescriptor{
            "operator-clicks",
            "Clicks",
            "clicks",
            "clicks",
            kClickDataType,
            1,
            8.0,
            1,
        });
    auto alarms = std::make_shared<DataBlock>(
        DataBlockDescriptor{
            "operator-alarms",
            "Alarms",
            "alarm",
            "alarms",
            kAlarmStateDataType,
            1,
            8.0,
            1,
        });
    auto entries = std::make_shared<DataBlock>(
        DataBlockDescriptor{
            "operator-entries",
            "Operator entries",
            "effort",
            "events",
            kOperatorEventDataType,
            1,
        });
    auto storage = std::make_shared<DataBlock>(
        DataBlockDescriptor{
            "operator-storage",
            "Storage",
            "storage",
            "status",
            kStorageHealthDataType,
            1,
        });
    auto clips = std::make_shared<DataBlock>(
        DataBlockDescriptor{
            "operator-clips",
            "Clips",
            "clips",
            "clips",
            kAudioClipDataType,
            1,
            8.0,
            1,
        });

    const auto temporary = std::filesystem::temp_directory_path() /
        ("pamguard-operator-node-" +
         std::to_string(
             std::chrono::steady_clock::now()
                 .time_since_epoch()
                 .count()));
    AudioSourceNode source(
        "source",
        {false, 1.0},
        raw);
    LevelMeterNode meter(
        "levels",
        {0.5, 1},
        raw,
        levels);
    SoundRecorderNode recorder(
        "recorder",
        {temporary, "test", 0.0},
        raw,
        recordings);
    AlarmEventCounterNode alarm(
        "alarm",
        {2, 1.0, "test alarm"},
        click_events,
        alarms);
    ClipGeneratorNode clip_generator(
        "clips",
        {0.25, 0.25, 2.0},
        raw,
        click_events,
        clips);
    OperatorInputNode effort(
        "effort",
        "effort",
        entries);
    StorageHealthNode storage_health(
        "storage",
        std::filesystem::temp_directory_path(),
        0.0,
        0.02,
        storage);

    GraphLevelMeasurement level_result;
    GraphRecordingEvent recording_result;
    GraphAlarmState alarm_result;
    GraphOperatorEvent entry_result;
    GraphStorageHealth storage_result;
    std::size_t storage_updates = 0;
    GraphAudioClip clip_result;
    auto level_subscription = levels->subscribe(
        [&](const DataUnit& unit) {
            level_result = std::any_cast<
                GraphLevelMeasurement>(unit.payload);
        });
    auto recording_subscription = recordings->subscribe(
        [&](const DataUnit& unit) {
            recording_result = std::any_cast<
                GraphRecordingEvent>(unit.payload);
        });
    auto alarm_subscription = alarms->subscribe(
        [&](const DataUnit& unit) {
            alarm_result = std::any_cast<
                GraphAlarmState>(unit.payload);
        });
    auto entry_subscription = entries->subscribe(
        [&](const DataUnit& unit) {
            entry_result = std::any_cast<
                GraphOperatorEvent>(unit.payload);
        });
    auto storage_subscription = storage->subscribe(
        [&](const DataUnit& unit) {
            storage_result = std::any_cast<
                GraphStorageHealth>(unit.payload);
            ++storage_updates;
        });
    auto clip_subscription = clips->subscribe(
        [&](const DataUnit& unit) {
            clip_result = std::any_cast<
                GraphAudioClip>(unit.payload);
        });

    source.prepare();
    meter.prepare();
    recorder.prepare();
    alarm.prepare();
    clip_generator.prepare();
    effort.prepare();
    storage_health.prepare();
    meter.start();
    recorder.start();
    const auto recorder_command = recorder.set_transport_state(
        SoundRecorderTransportState::Continuous);
    if (recorder_command != SoundRecorderCommandResult::Applied) {
        throw std::runtime_error(
            "Operator-support Recorder did not accept Continuous");
    }
    alarm.start();
    clip_generator.start();
    effort.start();
    storage_health.start();
    source.start();

    AudioChunk chunk;
    chunk.start_sample = 10;
    chunk.time_unix_ms = 1000;
    chunk.sample_rate_hz = 8;
    chunk.channel_count = 1;
    chunk.interleaved_pcm =
        {0.5, -0.5, 0.5, -0.5, 1.0, -1.0, 0.0, 0.0};
    source.ingest(std::move(chunk));
    for (const auto time_ms : {1000, 1500}) {
        DataUnitMetadata metadata;
        metadata.time_unix_ms = time_ms;
        metadata.start_sample = 13;
        metadata.duration_samples = 1;
        click_events->publish(
            make_data_unit(std::move(metadata), time_ms));
    }
    effort.publish(
        {"", "On effort", "visual watch", 1.0},
        1700);
    std::this_thread::sleep_for(
        std::chrono::milliseconds(45));
    recorder.stop();
    meter.stop();
    alarm.stop();
    clip_generator.stop();
    effort.stop();
    storage_health.stop();
    source.stop();

    const auto completed_recording =
        std::filesystem::path(recording_result.path).is_absolute()
        ? std::filesystem::path(recording_result.path)
        : temporary / recording_result.path;
    if (level_result.measured_frames != 8 ||
        level_result.rms_dbfs.size() != 1 ||
        level_result.peak_dbfs[0] > 1e-9 ||
        !alarm_result.active ||
        alarm_result.event_count != 2 ||
        entry_result.category != "effort" ||
        entry_result.label != "On effort" ||
        !storage_result.available ||
        storage_updates < 2 ||
        storage_result.capacity_bytes == 0 ||
        clip_result.trigger_start_sample != 13 ||
        clip_result.clip_start_sample != 11 ||
        clip_result.interleaved_pcm.size() != 5 ||
        clip_result.incomplete ||
        recording_result.frame_count != 8 ||
        recording_result.state != "completed" ||
        recording_result.path.empty() ||
        std::filesystem::path(recording_result.path).is_absolute() ||
        !std::filesystem::exists(completed_recording) ||
        std::filesystem::file_size(completed_recording) !=
            44 + 8 * sizeof(std::int16_t)) {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
        throw std::runtime_error(
            "Operator-support nodes did not publish valid typed results");
    }
    std::error_code ignored;
    std::filesystem::remove_all(temporary, ignored);
}

void check_prepared_runtime_swap() {
    pamguard::core::ModuleGraphDocument first_document;
    first_document.revision = 41;
    pamguard::core::ModuleGraphDocument second_document;
    second_document.revision = 42;

    pamguard::core::ModuleRuntime first;
    pamguard::core::ModuleRuntime second;
    first.configure(first_document);
    second.configure(second_document);
    first.swap_stopped(second);
    if (first.revision() != 42 ||
        second.revision() != 41) {
        throw std::runtime_error(
            "Prepared runtime exchange did not preserve both revisions");
    }

    first.start();
    bool running_rejected = false;
    try {
        first.swap_stopped(second);
    }
    catch (const std::logic_error&) {
        running_rejected = true;
    }
    first.stop();
    if (!running_rejected ||
        first.revision() != 42 ||
        second.revision() != 41) {
        throw std::runtime_error(
            "Prepared runtime exchange did not reject a running runtime "
            "without changing either side");
    }
}

} // namespace

int main() {
    try {
        auto registry = make_registry();
        bool duplicate_rejected = false;
        try {
            registry.register_type({
                "fft",
                "Duplicate",
                "Test",
                "Must fail",
            });
        }
        catch (const std::invalid_argument&) {
            duplicate_rejected = true;
        }
        if (!duplicate_rejected) {
            throw std::runtime_error("Module registry accepted a duplicate type id");
        }

        pamguard::core::ModuleGraph graph(registry);
        auto document = valid_document();
        if (!graph.validate(document).valid()) {
            throw std::runtime_error("Valid branched operator graph was rejected");
        }
        const auto applied = graph.apply(document, 0);
        if (!applied.applied || applied.revision != 1) {
            throw std::runtime_error("Valid graph was not applied transactionally");
        }
        if (graph.apply(document, 0).applied) {
            throw std::runtime_error("Stale graph revision was accepted");
        }

        auto invalid_type = valid_document();
        invalid_type.connections[3].source = {"source-1", "audio"};
        const auto type_validation = graph.validate(invalid_type);
        if (!has_issue(type_validation.issues, "incompatible_data_type")) {
            throw std::runtime_error("Incompatible port types were not rejected");
        }

        auto missing_input = valid_document();
        missing_input.connections.erase(missing_input.connections.begin());
        const auto missing_validation = graph.validate(missing_input);
        if (!has_issue(missing_validation.issues, "missing_required_input")) {
            throw std::runtime_error("Missing required input was not rejected");
        }

        auto disabled_source = valid_document();
        disabled_source.modules.front().enabled = false;
        const auto disabled_validation = graph.validate(disabled_source);
        if (!has_issue(disabled_validation.issues, "disabled_source")) {
            throw std::runtime_error(
                "Enabled module connected to a disabled source was not rejected");
        }

        auto cycle = valid_document();
        cycle.connections.push_back({
            "cycle",
            {"decimator-1", "output"},
            {"decimator-1", "input"},
        });
        const auto cycle_validation = graph.validate(cycle);
        if (!has_issue(cycle_validation.issues, "cycle")) {
            throw std::runtime_error("Cyclic graph was not rejected");
        }

        const auto sources = graph.compatible_sources(
            valid_document(),
            {"fft-low", "input"});
        if (sources.size() != 2 ||
            sources[0].data_type != "pamguard.raw-audio" ||
            sources[1].data_type != "pamguard.raw-audio") {
            throw std::runtime_error("Compatible source discovery did not expose both raw branches");
        }

        auto persisted = graph.snapshot();
        const auto encoded = pamguard::core::module_graph_to_json(persisted, true);
        const auto decoded = pamguard::core::module_graph_from_json(encoded);
        if (pamguard::core::module_graph_to_json(decoded) !=
            pamguard::core::module_graph_to_json(persisted)) {
            throw std::runtime_error("Module graph JSON round trip changed stable graph state");
        }

        const auto run_check =
            [](const char* name, const auto& check) {
                try {
                    check();
                }
                catch (const std::exception& error) {
                    throw std::runtime_error(
                        std::string(name) + ": " + error.what());
                }
            };
        run_check("data block", check_data_block);
        run_check(
            "non-blocking presentation delivery",
            check_non_blocking_presentation_delivery);
        run_check(
            "observer failure isolation",
            check_observer_failure_isolation);
        run_check(
            "queued self-unsubscribe",
            check_queued_self_unsubscribe);
        run_check(
            "executable signal branches",
            check_executable_signal_branches);
        run_check(
            "acquisition DC filter",
            check_acquisition_dc_filter);
        run_check(
            "filter runtime contract",
            check_filter_params_runtime_contract);
        run_check(
            "runtime graph factory",
            check_runtime_graph_factory);
        run_check(
            "click detector graph node",
            check_click_detector_graph_node);
        run_check(
            "post-localiser train bearing gate",
            check_post_localiser_simple_train_bearing_gate);
        run_check(
            "matched-template click annotations",
            check_matched_template_click_annotations);
        run_check(
            "click grouping and echo",
            check_click_detector_grouping_and_echo_runtime);
        run_check(
            "MHT click train graph node",
            check_mht_click_train_graph_node);
        run_check(
            "FFT detector graph nodes",
            check_fft_detector_graph_nodes);
        run_check(
            "integrated stop finalization",
            check_integrated_stop_finalization);
        run_check(
            "operator support nodes",
            check_operator_support_nodes);
        run_check(
            "prepared runtime swap",
            check_prepared_runtime_swap);
        std::cout << "Module graph and typed data-block check passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
