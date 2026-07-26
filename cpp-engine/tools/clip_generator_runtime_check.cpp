#include "pamguard/core/ModuleRuntime.h"
#include "pamguard/core/OperatorNodes.h"

#include <any>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

namespace {

using nlohmann::json;
using namespace pamguard::core;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::shared_ptr<DataBlock> raw_block(
    std::string id,
    double sample_rate_hz,
    std::uint32_t channel_bitmap) {
    return std::make_shared<DataBlock>(
        DataBlockDescriptor{
            std::move(id),
            "Raw audio",
            "source",
            "audio",
            kRawAudioDataType,
            1,
            sample_rate_hz,
            channel_bitmap,
        });
}

std::shared_ptr<DataBlock> trigger_block(
    std::string id,
    std::string producer) {
    return std::make_shared<DataBlock>(
        DataBlockDescriptor{
            std::move(id),
            "Trigger",
            std::move(producer),
            "detections",
            "test.detection",
            1,
            10.0,
            0xFFFFFFFFu,
        });
}

std::shared_ptr<DataBlock> clip_block(
    double sample_rate_hz,
    std::uint32_t channel_bitmap) {
    return std::make_shared<DataBlock>(
        DataBlockDescriptor{
            "block:clips:clips",
            "Clips",
            "clips",
            "clips",
            kAudioClipDataType,
            1,
            sample_rate_hz,
            channel_bitmap,
        });
}

void publish_audio(
    const std::shared_ptr<DataBlock>& block,
    std::uint64_t start_sample,
    std::size_t frames,
    bool discontinuity = false) {
    AudioChunk chunk;
    chunk.start_sample = start_sample;
    chunk.time_unix_ms =
        static_cast<std::int64_t>(
            start_sample * 100);
    chunk.sample_rate_hz = 10;
    // Sparse bitmap 0b101 uses physical-index slots 0, 1, 2.
    chunk.channel_count = 3;
    chunk.interleaved_pcm.resize(frames * 3);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        chunk.interleaved_pcm[frame * 3] =
            100.0 + start_sample + frame;
        chunk.interleaved_pcm[frame * 3 + 1] =
            200.0 + start_sample + frame;
        chunk.interleaved_pcm[frame * 3 + 2] =
            300.0 + start_sample + frame;
    }
    DataUnitMetadata metadata;
    metadata.time_unix_ms = chunk.time_unix_ms;
    metadata.start_sample =
        static_cast<std::int64_t>(start_sample);
    metadata.duration_samples = frames;
    metadata.channel_bitmap = 0b101;
    metadata.discontinuity = discontinuity;
    block->publish(
        make_data_unit(
            std::move(metadata),
            std::move(chunk)));
}

void publish_trigger(
    const std::shared_ptr<DataBlock>& block,
    std::uint64_t uid,
    std::int64_t time_ms,
    std::int64_t start_sample,
    std::uint64_t duration,
    std::uint32_t channels) {
    DataUnitMetadata metadata;
    metadata.uid = uid;
    metadata.sequence = uid;
    metadata.time_unix_ms = time_ms;
    metadata.start_sample = start_sample;
    metadata.duration_samples = duration;
    metadata.channel_bitmap = channels;
    block->publish(
        make_data_unit(
            std::move(metadata),
            static_cast<int>(uid)));
}

ClipGeneratorTriggerInput policy(
    std::shared_ptr<DataBlock> input,
    ClipGeneratorChannelSelection channels,
    double pre = 0.0,
    double post = 0.0,
    bool enabled = true) {
    ClipGeneratorTriggerInput result;
    result.input = std::move(input);
    result.source_unit_id =
        result.input->descriptor().producer_module_id;
    result.source_output_role = "detections";
    result.runtime_block_id =
        result.input->descriptor().id;
    result.source_data_type =
        result.input->descriptor().data_type;
    result.enabled = enabled;
    result.pre_trigger_seconds = pre;
    result.post_trigger_seconds = post;
    result.channel_selection = channels;
    result.use_data_budget = false;
    return result;
}

void check_multi_policy_channels_and_truncation() {
    auto raw = raw_block("block:source:audio", 10.0, 0b101);
    auto detection = trigger_block(
        "block:detection:detections",
        "detection");
    auto first = trigger_block(
        "block:first:detections",
        "first");
    auto all = trigger_block(
        "block:all:detections",
        "all");
    auto disabled = trigger_block(
        "block:disabled:detections",
        "disabled");
    auto output = clip_block(10.0, 0b101);

    auto detection_policy = policy(
        detection,
        ClipGeneratorChannelSelection::
            DetectionChannelsOnly,
        0.15,
        0.15);
    detection_policy.clip_prefix = "DET";
    ClipGeneratorNode node(
        "clips",
        {0.0, 0.0, 10.0},
        raw,
        {
            detection_policy,
            policy(
                first,
                ClipGeneratorChannelSelection::
                    FirstDetectionChannelOnly),
            policy(
                all,
                ClipGeneratorChannelSelection::
                    AllChannels),
            policy(
                disabled,
                ClipGeneratorChannelSelection::
                    AllChannels,
                0.0,
                0.0,
                false),
        },
        output);
    std::vector<GraphAudioClip> clips;
    auto subscription = output->subscribe(
        [&](const DataUnit& unit) {
            clips.push_back(
                std::any_cast<GraphAudioClip>(
                    unit.payload));
        });
    node.prepare();
    node.start();
    publish_audio(raw, 0, 10);
    publish_trigger(detection, 11, 5000, 5, 1, 0b101);
    publish_trigger(first, 12, 5100, 5, 1, 0b100);
    publish_trigger(all, 13, 5200, 5, 1, 0b001);
    publish_trigger(disabled, 14, 5300, 5, 1, 0b001);
    publish_trigger(first, 15, 5400, 5, 1, 0b010);
    node.stop();

    require(clips.size() == 3,
            "Enabled trigger policies were not independent");
    const auto& detection_clip = clips.at(0);
    require(
        detection_clip.clip_start_sample == 3 &&
        detection_clip.interleaved_pcm.size() == 8 &&
        detection_clip.channel_count == 2 &&
        detection_clip.selected_channel_bitmap == 0b101,
        "Java full-expression truncation or sparse detection channels changed");
    require(
        detection_clip.interleaved_pcm ==
            std::vector<double>{
                103, 303,
                104, 304,
                105, 305,
                106, 306,
            },
        "Detection-channel extraction did not preserve physical channels");
    require(
        detection_clip.clip_start_time_unix_ms == 4850 &&
        detection_clip.trigger_uid == 11 &&
        detection_clip.trigger_duration_samples == 1 &&
        detection_clip.trigger_channel_bitmap == 0b101 &&
        detection_clip.trigger_source_unit_id == "detection" &&
        detection_clip.trigger_source_output_role == "detections" &&
        detection_clip.trigger_runtime_block_id ==
            "block:detection:detections" &&
        detection_clip.trigger_data_type == "test.detection" &&
        detection_clip.clip_prefix == "DET" &&
        !detection_clip.incomplete,
        "Clip start metadata or trigger provenance was not retained");
    require(
        clips.at(1).selected_channel_bitmap == 0b100 &&
        clips.at(1).channel_count == 1 &&
        clips.at(1).interleaved_pcm ==
            std::vector<double>{305},
        "First-detection-channel selection failed on a sparse bitmap");
    require(
        clips.at(2).selected_channel_bitmap == 0b101 &&
        clips.at(2).channel_count == 2 &&
        clips.at(2).interleaved_pcm ==
            std::vector<double>{105, 305},
        "All-channel selection failed on a sparse bitmap");
}

void check_wait_drop_and_non_fifo_processing() {
    auto raw = raw_block("block:source:audio", 10.0, 0b101);
    auto slow = trigger_block("block:slow:events", "slow");
    auto fast = trigger_block("block:fast:events", "fast");
    auto output = clip_block(10.0, 0b101);
    ClipGeneratorNode node(
        "clips",
        {0.0, 0.0, 10.0},
        raw,
        {
            policy(
                slow,
                ClipGeneratorChannelSelection::AllChannels,
                0.0,
                2.0),
            policy(
                fast,
                ClipGeneratorChannelSelection::AllChannels),
        },
        output);
    std::vector<GraphAudioClip> clips;
    auto subscription = output->subscribe(
        [&](const DataUnit& unit) {
            clips.push_back(
                std::any_cast<GraphAudioClip>(
                    unit.payload));
        });
    node.prepare();
    node.start();
    publish_audio(raw, 0, 10);
    publish_trigger(slow, 21, 1000, 2, 1, 1);
    publish_trigger(fast, 22, 1100, 5, 1, 1);
    require(
        clips.size() == 1 &&
        clips.front().trigger_uid == 22,
        "A waiting request FIFO-blocked a later ready request");
    node.stop();
    require(
        clips.size() == 1,
        "Stopping published a clip without its post-trigger audio");

    auto gap_output = clip_block(10.0, 0b101);
    auto gap_trigger = trigger_block(
        "block:gap:events",
        "gap");
    ClipGeneratorNode gap_node(
        "gap-clips",
        {0.0, 0.0, 10.0},
        raw,
        {
            policy(
                gap_trigger,
                ClipGeneratorChannelSelection::
                    DetectionChannelsOnly),
        },
        gap_output);
    std::size_t gap_clips = 0;
    auto gap_subscription = gap_output->subscribe(
        [&](const DataUnit&) { ++gap_clips; });
    gap_node.prepare();
    gap_node.start();
    publish_audio(raw, 20, 4);
    publish_trigger(gap_trigger, 23, 2000, 21, 5, 1);
    publish_audio(raw, 25, 5, true);
    publish_trigger(gap_trigger, 24, 2100, 25, 1, 0b010);
    gap_node.stop();
    require(
        gap_clips == 0,
        "Gapped or invalid-channel requests produced clips");
}

void check_discarded_history_and_zero_policies() {
    auto raw = raw_block("block:source:audio", 10.0, 0b101);
    auto trigger = trigger_block(
        "block:old:events",
        "old");
    auto output = clip_block(10.0, 0b101);
    ClipGeneratorNode node(
        "clips",
        {0.0, 0.0, 1.0},
        raw,
        {
            policy(
                trigger,
                ClipGeneratorChannelSelection::AllChannels),
        },
        output);
    std::size_t clips = 0;
    auto subscription = output->subscribe(
        [&](const DataUnit&) { ++clips; });
    node.prepare();
    node.start();
    publish_audio(raw, 0, 10);
    publish_audio(raw, 10, 10);
    publish_audio(raw, 20, 10);
    publish_trigger(trigger, 31, 3000, 1, 1, 1);
    node.stop();
    require(
        clips == 0,
        "A request for discarded raw audio produced a clip");

    auto idle_output = clip_block(10.0, 0b101);
    ClipGeneratorNode idle(
        "idle-clips",
        {0.0, 0.0, 1.0},
        raw,
        std::vector<ClipGeneratorTriggerInput>{},
        idle_output);
    std::size_t idle_clips = 0;
    auto idle_subscription = idle_output->subscribe(
        [&](const DataUnit&) { ++idle_clips; });
    idle.prepare();
    idle.start();
    publish_audio(raw, 30, 10);
    idle.stop();
    require(
        idle_clips == 0,
        "A zero-policy Clip Generator was not safely idle");
}

void check_independent_java_budgets() {
    auto raw = raw_block("block:source:audio", 10.0, 0b101);
    auto first = trigger_block(
        "block:budget-a:events",
        "budget-a");
    auto second = trigger_block(
        "block:budget-b:events",
        "budget-b");
    auto output = clip_block(10.0, 0b101);
    auto first_policy = policy(
        first,
        ClipGeneratorChannelSelection::
            FirstDetectionChannelOnly);
    auto second_policy = policy(
        second,
        ClipGeneratorChannelSelection::
            FirstDetectionChannelOnly);
    for (auto* candidate :
         {&first_policy, &second_policy}) {
        candidate->use_data_budget = true;
        candidate->data_budget_kilobytes = 0;
        candidate->budget_period_hours = 1.0;
    }
    std::vector<std::size_t> random_calls(2, 0);
    ClipGeneratorNodeConfig config;
    config.maximum_buffer_seconds = 10.0;
    config.random_unit_interval =
        [&](std::size_t index) {
            ++random_calls.at(index);
            return 0.5;
        };
    ClipGeneratorNode node(
        "budget-clips",
        config,
        raw,
        {first_policy, second_policy},
        output);
    std::vector<GraphAudioClip> clips;
    auto subscription = output->subscribe(
        [&](const DataUnit& unit) {
            clips.push_back(
                std::any_cast<GraphAudioClip>(
                    unit.payload));
        });
    node.prepare();
    node.start();
    publish_audio(raw, 0, 10);
    publish_trigger(first, 41, 1000, 2, 1, 1);
    publish_trigger(first, 42, 1001, 3, 1, 1);
    publish_trigger(second, 43, 1002, 4, 1, 4);
    publish_trigger(second, 44, 1003, 5, 1, 4);
    node.stop();
    require(
        clips.size() == 2 &&
        clips.at(0).trigger_uid == 41 &&
        clips.at(1).trigger_uid == 43,
        "StandardClipBudgetMaker state was not independent per policy");
    require(
        random_calls == std::vector<std::size_t>{1, 1},
        "Budget RNG injection did not make each policy deterministic");

    auto truncation_trigger = trigger_block(
        "block:budget-truncation:events",
        "budget-truncation");
    auto truncation_output = clip_block(10.0, 0b101);
    auto truncation_policy = policy(
        truncation_trigger,
        ClipGeneratorChannelSelection::
            DetectionChannelsOnly,
        0.12,
        0.12);
    truncation_policy.use_data_budget = true;
    truncation_policy.data_budget_kilobytes = 1;
    // Java clamps periods shorter than one second to one second.
    truncation_policy.budget_period_hours = 0.0001;
    std::size_t truncation_random_calls = 0;
    ClipGeneratorNodeConfig truncation_config;
    truncation_config.maximum_buffer_seconds = 10.0;
    truncation_config.random_unit_interval =
        [&](std::size_t) {
            ++truncation_random_calls;
            /*
             * Java casts the 3.4 estimated samples to 3 before applying
             * two channels: 56 bytes. At t=3 ms the probability is
             * 0.003 / (56 / 1024) ~= 0.054857.
             */
            return 0.0543;
        };
    ClipGeneratorNode truncation_node(
        "budget-truncation-clips",
        truncation_config,
        raw,
        {truncation_policy},
        truncation_output);
    std::size_t truncation_clips = 0;
    auto truncation_subscription =
        truncation_output->subscribe(
            [&](const DataUnit&) {
                ++truncation_clips;
            });
    truncation_node.prepare();
    truncation_node.start();
    publish_audio(raw, 0, 10);
    publish_trigger(
        truncation_trigger,
        45,
        0,
        5,
        1,
        0b101);
    publish_trigger(
        truncation_trigger,
        46,
        3,
        6,
        1,
        0b101);
    truncation_node.stop();
    require(
        truncation_clips == 2 &&
        truncation_random_calls == 1,
        "Budget clip-size math did not truncate samples before channel bytes");
}

json runtime_policy(
    std::string unit,
    std::string runtime_block) {
    return {
        {"triggerSource",
         {
             {"unitId", std::move(unit)},
             {"outputRole", "events"},
         }},
        {"enabled", true},
        {"secondsBeforeTrigger", 0.0},
        {"secondsAfterTrigger", 0.0},
        {"channelSelection", "all-channels"},
        {"clipPrefix", nullptr},
        {"useDataBudget", false},
        {"dataBudgetKilobytes", 10240},
        {"budgetPeriodHours", 24.0},
        {"runtimeBlockId", std::move(runtime_block)},
        {"sourceDataType", kOperatorEventDataType},
        {"spectrogramMark", false},
    };
}

ModuleGraphDocument runtime_document(
    bool include_policy,
    bool mismatched_runtime_block = false) {
    json policies = json::array();
    std::vector<ModuleConnection> connections{
        {
            "audio",
            {"source", "audio"},
            {"clips", "audio"},
        },
    };
    if (include_policy) {
        policies.push_back(
            runtime_policy(
                "trigger",
                mismatched_runtime_block
                    ? "block:wrong:events"
                    : "block:trigger:events"));
        connections.push_back({
            "trigger",
            {"trigger", "events"},
            {"clips", "triggers"},
        });
    }
    return {
        1,
        1,
        {
            {
                "source",
                "pamguard.acquisition",
                "Source",
                true,
                json{
                    {"sourceId", "clip-runtime"},
                    {"sampleRateHz", 10},
                    {"channelCount", 1},
                    {"subtractDC", false},
                    {"dcTimeConstantSeconds", 1.0},
                }.dump(),
            },
            {
                "trigger",
                "pamguard.user-input",
                "Trigger",
                true,
                R"({"defaultCategory":"annotation"})",
            },
            {
                "clips",
                "pamguard.clip-generator",
                "Clips",
                true,
                json{
                    {"storageMode", "binary"},
                    {"datedSubFolders", true},
                    {"requiredHistorySeconds", 0.0},
                    {"triggerPolicies", std::move(policies)},
                }.dump(),
            },
        },
        std::move(connections),
    };
}

void check_runtime_mapping_and_storage_boundary() {
    ModuleRuntime idle;
    idle.configure(runtime_document(false));
    idle.start();
    AudioChunk chunk;
    chunk.start_sample = 0;
    chunk.time_unix_ms = 0;
    chunk.sample_rate_hz = 10;
    chunk.channel_count = 1;
    chunk.interleaved_pcm = {0, 1, 2};
    idle.ingest("source", std::move(chunk));
    idle.stop();

    ModuleRuntime mapped;
    mapped.configure(runtime_document(true));
    const auto clips = mapped.find_block(
        ModuleRuntime::block_id(
            "clips",
            "clips"));
    require(
        static_cast<bool>(clips),
        "Mapped runtime omitted the clip output");
    std::vector<GraphAudioClip> mapped_clips;
    auto subscription = clips->subscribe(
        [&](const DataUnit& unit) {
            mapped_clips.push_back(
                std::any_cast<GraphAudioClip>(
                    unit.payload));
        });
    mapped.start();
    AudioChunk mapped_audio;
    mapped_audio.start_sample = 0;
    mapped_audio.time_unix_ms = 0;
    mapped_audio.sample_rate_hz = 10;
    mapped_audio.channel_count = 1;
    mapped_audio.interleaved_pcm = {0, 1, 2};
    mapped.ingest(
        "source",
        std::move(mapped_audio));
    mapped.publish_operator_event(
        "trigger",
        {"annotation", "mark", "", 0.0},
        100,
        2);
    mapped.stop();
    require(
        mapped_clips.size() == 1 &&
        mapped_clips.front().trigger_source_unit_id ==
            "trigger" &&
        mapped_clips.front().trigger_source_output_role ==
            "events" &&
        mapped_clips.front().trigger_runtime_block_id ==
            "block:trigger:events" &&
        mapped_clips.front().selected_channel_bitmap == 1,
        "Projected source identity was not preserved through ModuleRuntime");

    bool rejected_mismatch = false;
    try {
        ModuleRuntime mismatched;
        mismatched.configure(
            runtime_document(true, true));
    }
    catch (const std::invalid_argument&) {
        rejected_mismatch = true;
    }
    require(
        rejected_mismatch,
        "Runtime accepted a policy whose runtimeBlockId did not match its binding");

    auto wav = runtime_document(false);
    auto settings =
        json::parse(wav.modules.at(2).settings_json);
    settings["storageMode"] = "wav-files";
    wav.modules.at(2).settings_json =
        settings.dump();
    bool rejected_wav = false;
    try {
        ModuleRuntime unsupported;
        unsupported.configure(wav);
    }
    catch (const std::invalid_argument& error) {
        rejected_wav =
            std::string(error.what()).find(
                "deployment-owned") !=
            std::string::npos;
    }
    require(
        rejected_wav,
        "Runtime did not explicitly reject unimplemented deployment-owned WAV storage");
}

} // namespace

int main() {
    try {
        check_multi_policy_channels_and_truncation();
        check_wait_drop_and_non_fifo_processing();
        check_discarded_history_and_zero_policies();
        check_independent_java_budgets();
        check_runtime_mapping_and_storage_boundary();
        std::cout
            << "Clip Generator runtime parity checks passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr
            << "Clip Generator runtime parity check failed: "
            << error.what() << '\n';
        return 1;
    }
}
