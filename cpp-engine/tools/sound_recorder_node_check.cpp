#include "pamguard/core/OperatorNodes.h"
#include "pamguard/core/SignalNodes.h"

#include <algorithm>
#include <any>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using pamguard::core::AudioChunk;
using pamguard::core::DataBlock;
using pamguard::core::DataBlockDescriptor;
using pamguard::core::DataUnit;
using pamguard::core::DataUnitMetadata;
using pamguard::core::GraphRecordingEvent;
using pamguard::core::SoundRecorderCommandResult;
using pamguard::core::SoundRecorderNode;
using pamguard::core::SoundRecorderNodeConfig;
using pamguard::core::SoundRecorderOperationMode;
using pamguard::core::SoundRecorderTransportState;
using pamguard::core::kRawAudioDataType;
using pamguard::core::kRecordingEventDataType;
using pamguard::core::make_data_unit;

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(
              std::filesystem::temp_directory_path() /
              ("pamguard-sound-recorder-node-" +
               std::to_string(
                   std::chrono::steady_clock::now()
                       .time_since_epoch()
                       .count()))) {
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path&
    path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

struct RecorderBlocks {
    std::shared_ptr<DataBlock> audio;
    std::shared_ptr<DataBlock> events;
};

RecorderBlocks make_blocks(
    std::string suffix,
    std::uint32_t channel_bitmap) {
    RecorderBlocks result;
    result.audio = std::make_shared<DataBlock>(
        DataBlockDescriptor{
            "raw-" + suffix,
            "Raw audio",
            "source",
            "raw",
            kRawAudioDataType,
            1,
            0.0,
            channel_bitmap,
        });
    result.events = std::make_shared<DataBlock>(
        DataBlockDescriptor{
            "recordings-" + suffix,
            "Recordings",
            "recorder",
            "recordings",
            kRecordingEventDataType,
            1,
            0.0,
            channel_bitmap,
        });
    return result;
}

void publish_audio(
    const std::shared_ptr<DataBlock>& block,
    std::int64_t time_unix_ms,
    std::int64_t start_sample,
    std::uint32_t channel_bitmap,
    std::uint32_t sample_rate_hz,
    std::size_t channel_count,
    std::vector<double> samples) {
    AudioChunk chunk;
    chunk.time_unix_ms = time_unix_ms;
    chunk.start_sample = static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, start_sample));
    chunk.sample_rate_hz = sample_rate_hz;
    chunk.channel_count = channel_count;
    chunk.interleaved_pcm = std::move(samples);

    DataUnitMetadata metadata;
    metadata.time_unix_ms = time_unix_ms;
    metadata.start_sample = start_sample;
    metadata.duration_samples = chunk.frame_count();
    metadata.channel_bitmap = channel_bitmap;
    block->publish(
        make_data_unit(
            std::move(metadata),
            std::move(chunk)));
}

std::vector<std::filesystem::path> wav_files(
    const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(directory)) {
        return result;
    }
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(
             directory)) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".wav") {
            result.push_back(entry.path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::uint8_t> read_bytes(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "Could not read " + path.string());
    }
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

std::uint16_t read_u16(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset) {
    return static_cast<std::uint16_t>(
        bytes.at(offset) |
        (std::uint16_t{bytes.at(offset + 1)} << 8));
}

std::uint32_t read_u32(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset) {
    return static_cast<std::uint32_t>(
        bytes.at(offset) |
        (std::uint32_t{bytes.at(offset + 1)} << 8) |
        (std::uint32_t{bytes.at(offset + 2)} << 16) |
        (std::uint32_t{bytes.at(offset + 3)} << 24));
}

void require(
    bool condition,
    const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void check_idle_manual_transport_and_pcm16(
    const std::filesystem::path& root) {
    const auto directory = root / "manual";
    auto blocks = make_blocks("manual", 3);
    std::vector<GraphRecordingEvent> recording_events;
    std::vector<DataUnitMetadata> event_metadata;
    auto event_subscription = blocks.events->subscribe(
        [&](const DataUnit& unit) {
            recording_events.push_back(
                std::any_cast<GraphRecordingEvent>(
                    unit.payload));
            event_metadata.push_back(unit.metadata);
        });

    SoundRecorderNodeConfig config;
    config.directory = directory;
    config.settings.operation_mode =
        SoundRecorderOperationMode::Continuous;
    config.settings.channel_bitmap = 2;
    config.settings.bit_depth = 16;
    config.settings.file_initials = "PAM";
    config.settings.dated_subfolders = false;
    config.settings.limit_length_seconds = false;
    config.settings.limit_length_megabytes = false;
    SoundRecorderNode recorder(
        "manual-recorder",
        std::move(config),
        blocks.audio,
        blocks.events);

    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Continuous) ==
            SoundRecorderCommandResult::NodeNotRunning,
        "Recorder accepted a transport command before graph start");
    recorder.prepare();
    recorder.start();
    auto status = recorder.recorder_status();
    require(
        status.transport == SoundRecorderTransportState::Off &&
            !status.file_open &&
            status.selected_channel_bitmap == 2 &&
            status.bit_depth == 16,
        "Graph start did not leave the recorder Off");

    const std::vector<double> stereo = {
        0.25, -1.0,
        0.25, -0.5,
        0.25, 0.0,
        0.25, 0.5,
        0.25, 1.0,
    };
    publish_audio(
        blocks.audio,
        1'700'000'000'123,
        100,
        3,
        8'000,
        2,
        stereo);
    require(
        wav_files(directory).empty() &&
            recording_events.empty(),
        "Off recorder created output merely because audio arrived");
    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Cycle) ==
            SoundRecorderCommandResult::
                UnsupportedOperationMode &&
        recorder.set_operation_mode(
            SoundRecorderOperationMode::RestoreLast) ==
            SoundRecorderCommandResult::
                UnsupportedOperationMode,
        "Unsupported automatic recorder modes were not explicit");

    std::atomic<bool> status_failed = false;
    std::thread status_reader([&] {
        for (int index = 0; index < 2'000; ++index) {
            const auto snapshot =
                recorder.recorder_status();
            if (snapshot.selected_channel_bitmap != 2 ||
                snapshot.bit_depth != 16) {
                status_failed = true;
            }
        }
    });
    bool command_failed = false;
    for (int index = 0; index < 100; ++index) {
        const auto start_result =
            recorder.set_operation_mode(
                SoundRecorderOperationMode::Continuous);
        const auto stop_result =
            recorder.set_operation_mode(
                SoundRecorderOperationMode::Idle);
        command_failed =
            command_failed ||
            start_result !=
                SoundRecorderCommandResult::Applied ||
            stop_result !=
                SoundRecorderCommandResult::Applied;
    }
    status_reader.join();
    require(
        !command_failed &&
            !status_failed &&
            wav_files(directory).empty(),
        "Concurrent status/transport cycle observed invalid state");

    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Continuous) ==
            SoundRecorderCommandResult::Applied &&
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Continuous) ==
            SoundRecorderCommandResult::
                AlreadyInRequestedMode,
        "Manual Continuous command did not transition cleanly");

    publish_audio(
        blocks.audio,
        1'700'000'000'123,
        100,
        3,
        8'000,
        2,
        stereo);
    status = recorder.recorder_status();
    require(
        status.file_open &&
            status.frames_in_current_file == 5 &&
            status.channel_count == 1 &&
            status.sample_rate_hz == 8'000,
        "Continuous recorder status did not describe its open file");
    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Idle) ==
            SoundRecorderCommandResult::Applied,
        "Manual Off command failed");
    require(
        recording_events.size() == 1 &&
            recording_events.front().state == "completed" &&
            !std::filesystem::path(
                recording_events.front().path).is_absolute() &&
            recording_events.front().frame_count == 5 &&
            recording_events.front().channel_count == 1 &&
            event_metadata.front().channel_bitmap == 2,
        "Manual stop did not publish the selected-channel recording");

    const auto files = wav_files(directory);
    require(
        files.size() == 1 &&
            files.front().filename().string().starts_with(
                "PAM_") &&
            files.front().extension() == ".wav" &&
            recording_events.front().path ==
                files.front().filename().generic_string(),
        "Recorder did not use the Java-style file prefix");
    const auto bytes = read_bytes(files.front());
    require(
        bytes.size() == 44 + 5 * 2 &&
            std::string(bytes.begin(), bytes.begin() + 4) ==
                "RIFF" &&
            std::string(bytes.begin() + 8, bytes.begin() + 12) ==
                "WAVE" &&
            read_u16(bytes, 20) == 1 &&
            read_u16(bytes, 22) == 1 &&
            read_u32(bytes, 24) == 8'000 &&
            read_u16(bytes, 34) == 16 &&
            read_u32(bytes, 40) == 10,
        "Recorder wrote an invalid PCM16 WAV header");
    const std::vector<std::uint8_t> expected_payload = {
        0x00, 0x80,
        0x00, 0xC0,
        0x00, 0x00,
        0x00, 0x40,
        0xFF, 0x7F,
    };
    require(
        std::equal(
            expected_payload.begin(),
            expected_payload.end(),
            bytes.begin() + 44),
        "Recorder PCM16 payload did not contain only selected channel 1");

    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Continuous) ==
            SoundRecorderCommandResult::Applied,
        "Repeat recording cycle did not start");
    publish_audio(
        blocks.audio,
        1'700'000'001'123,
        105,
        3,
        8'000,
        2,
        {0.0, 0.25, 0.0, -0.25});
    recorder.stop();
    require(
        recording_events.size() == 2 &&
            recording_events.back().state == "completed" &&
            recording_events.back().frame_count == 2 &&
            recorder.recorder_status().transport ==
                SoundRecorderTransportState::Off,
        "Graph stop did not safely close a repeated recording");

    recorder.start();
    publish_audio(
        blocks.audio,
        1'700'000'002'123,
        107,
        3,
        8'000,
        2,
        stereo);
    recorder.stop();
    require(
        recording_events.size() == 2,
        "Restarting the graph implicitly restarted recording");
}

void check_time_segmentation_and_flush(
    const std::filesystem::path& root) {
    const auto directory = root / "time-segments";
    auto blocks = make_blocks("time-segments", 1);
    std::vector<GraphRecordingEvent> events;
    auto subscription = blocks.events->subscribe(
        [&](const DataUnit& unit) {
            events.push_back(
                std::any_cast<GraphRecordingEvent>(
                    unit.payload));
        });

    SoundRecorderNodeConfig config;
    config.directory = directory;
    config.settings.channel_bitmap = 1;
    config.settings.bit_depth = 16;
    config.settings.file_initials = "SEG";
    config.settings.dated_subfolders = false;
    config.settings.limit_length_seconds = true;
    config.settings.max_length_seconds = 1;
    config.settings.round_file_starts = false;
    config.settings.limit_length_megabytes = false;
    SoundRecorderNode recorder(
        "time-segment-recorder",
        std::move(config),
        blocks.audio,
        blocks.events);
    recorder.prepare();
    recorder.start();
    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Continuous) ==
            SoundRecorderCommandResult::Applied,
        "Segmented recorder did not start");
    publish_audio(
        blocks.audio,
        2'000,
        20,
        1,
        4,
        1,
        std::vector<double>(10, 0.25));
    require(
        events.size() == 2 &&
            events[0].frame_count == 4 &&
            events[1].frame_count == 4 &&
            events[0].state == "segment-completed" &&
            events[1].state == "segment-completed" &&
            recorder.recorder_status().file_open &&
            recorder.recorder_status().
                frames_in_current_file == 2,
        "Time limit did not split audio at exact frame boundaries");
    recorder.flush();
    require(
        events.size() == 3 &&
            events.back().state == "flushed" &&
            events.back().frame_count == 2 &&
            recorder.recorder_status().transport ==
                SoundRecorderTransportState::Continuous &&
            !recorder.recorder_status().file_open,
        "Flush did not safely finalise the current segment");
    publish_audio(
        blocks.audio,
        5'000,
        30,
        1,
        4,
        1,
        {0.5});
    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Idle) ==
            SoundRecorderCommandResult::Applied &&
            events.size() == 4 &&
            events.back().frame_count == 1,
        "Continuous transport did not reopen after flush");
    recorder.stop();
    require(
        wav_files(directory).size() == 4,
        "Segmented recorder did not retain every closed WAV");
}

void check_megabyte_segmentation(
    const std::filesystem::path& root) {
    const auto directory = root / "size-segments";
    auto blocks = make_blocks("size-segments", 1);
    std::vector<GraphRecordingEvent> events;
    auto subscription = blocks.events->subscribe(
        [&](const DataUnit& unit) {
            events.push_back(
                std::any_cast<GraphRecordingEvent>(
                    unit.payload));
        });

    SoundRecorderNodeConfig config;
    config.directory = directory;
    config.settings.channel_bitmap = 1;
    config.settings.bit_depth = 32;
    config.settings.file_initials = "SIZE";
    config.settings.dated_subfolders = false;
    config.settings.limit_length_seconds = false;
    config.settings.limit_length_megabytes = true;
    config.settings.max_length_megabytes = 1;
    SoundRecorderNode recorder(
        "size-segment-recorder",
        std::move(config),
        blocks.audio,
        blocks.events);
    recorder.prepare();
    recorder.start();
    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Continuous) ==
            SoundRecorderCommandResult::Applied,
        "Size-limited recorder did not start");
    publish_audio(
        blocks.audio,
        10'000,
        0,
        1,
        48'000,
        1,
        std::vector<double>(262'145, 0.0));
    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Idle) ==
            SoundRecorderCommandResult::Applied &&
            events.size() == 2 &&
            events[0].frame_count == 262'144 &&
            events[0].state == "segment-completed" &&
            events[1].frame_count == 1,
        "Megabyte limit did not split PCM32 at exactly 1 MiB");
    const auto files = wav_files(directory);
    require(
        files.size() == 2 &&
            std::filesystem::file_size(files[0]) ==
                44 + (std::uint64_t{1} << 20) &&
            std::filesystem::file_size(files[1]) == 48,
        "Megabyte-limited WAV sizes were incorrect");
    recorder.stop();
}

void check_integer_depth(
    const std::filesystem::path& root,
    int bit_depth,
    const std::vector<std::uint8_t>& expected_payload) {
    const auto suffix = "pcm-" + std::to_string(bit_depth);
    const auto directory = root / suffix;
    auto blocks = make_blocks(suffix, 1);
    std::vector<GraphRecordingEvent> events;
    auto subscription = blocks.events->subscribe(
        [&](const DataUnit& unit) {
            events.push_back(
                std::any_cast<GraphRecordingEvent>(
                    unit.payload));
        });

    SoundRecorderNodeConfig config;
    config.directory = directory;
    config.settings.channel_bitmap = 1;
    config.settings.bit_depth = bit_depth;
    config.settings.file_initials = "PCM";
    config.settings.dated_subfolders = false;
    config.settings.limit_length_seconds = false;
    config.settings.limit_length_megabytes = false;
    SoundRecorderNode recorder(
        "pcm-recorder-" + std::to_string(bit_depth),
        std::move(config),
        blocks.audio,
        blocks.events);
    recorder.prepare();
    recorder.start();
    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Continuous) ==
            SoundRecorderCommandResult::Applied,
        "PCM depth recorder did not start");
    publish_audio(
        blocks.audio,
        20'000 + bit_depth,
        0,
        1,
        8'000,
        1,
        {-1.0, 0.0, 1.0});
    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Idle) ==
            SoundRecorderCommandResult::Applied &&
            events.size() == 1,
        "PCM depth recorder did not stop");
    const auto files = wav_files(directory);
    require(
        files.size() == 1,
        "PCM depth recorder did not create one WAV");
    const auto bytes = read_bytes(files.front());
    require(
        read_u16(bytes, 20) == 1 &&
            read_u16(bytes, 34) == bit_depth &&
            read_u32(bytes, 40) ==
                expected_payload.size() &&
            bytes.size() == 44 +
                expected_payload.size() &&
            std::equal(
                expected_payload.begin(),
                expected_payload.end(),
                bytes.begin() + 44),
        "Integer PCM encoding was incorrect at " +
            std::to_string(bit_depth) + " bits");
    recorder.stop();
}

void check_unsupported_buffer(
    const std::filesystem::path& root) {
    auto blocks = make_blocks("buffer", 1);
    SoundRecorderNodeConfig config;
    config.directory = root / "buffer";
    config.settings.channel_bitmap = 1;
    config.settings.enable_buffer = true;
    SoundRecorderNode recorder(
        "buffer-recorder",
        std::move(config),
        blocks.audio,
        blocks.events);
    bool rejected = false;
    try {
        recorder.prepare();
    }
    catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(
        rejected,
        "Unimplemented Continuous+Buffer semantics were silently accepted");
}

void check_invalid_filename_initials(
    const std::filesystem::path& root) {
    const std::vector<std::string> invalid_initials = {
        "bad<name",
        "bad>name",
        "bad:name",
        "bad\"name",
        "bad/name",
        R"(bad\name)",
        "bad|name",
        "bad?name",
        "bad*name",
        std::string{"bad\tname"},
        std::string{"bad"} + char{0x7F} + "name",
    };
    for (std::size_t index = 0;
         index < invalid_initials.size();
         ++index) {
        auto blocks = make_blocks(
            "invalid-" + std::to_string(index),
            1);
        SoundRecorderNodeConfig config;
        config.directory =
            root / ("invalid-" + std::to_string(index));
        config.settings.channel_bitmap = 1;
        config.settings.file_initials =
            invalid_initials[index];
        SoundRecorderNode recorder(
            "invalid-recorder-" + std::to_string(index),
            std::move(config),
            blocks.audio,
            blocks.events);
        bool rejected = false;
        try {
            recorder.prepare();
        }
        catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(
            rejected,
            "Sound Recorder node accepted invalid file initials");
    }
}

void check_collision_safe_creation(
    const std::filesystem::path& root) {
    const auto directory = root / "collisions";
    auto blocks = make_blocks("collisions", 1);
    std::vector<GraphRecordingEvent> events;
    auto subscription = blocks.events->subscribe(
        [&](const DataUnit& unit) {
            events.push_back(
                std::any_cast<GraphRecordingEvent>(
                    unit.payload));
        });

    SoundRecorderNodeConfig config;
    config.directory = directory;
    config.settings.channel_bitmap = 1;
    config.settings.bit_depth = 16;
    config.settings.file_initials = "COLLIDE";
    config.settings.dated_subfolders = false;
    config.settings.limit_length_seconds = false;
    config.settings.limit_length_megabytes = false;
    SoundRecorderNode recorder(
        "collision-recorder",
        std::move(config),
        blocks.audio,
        blocks.events);
    recorder.prepare();
    recorder.start();

    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Continuous) ==
            SoundRecorderCommandResult::Applied,
        "Collision recorder did not start its first file");
    publish_audio(
        blocks.audio,
        1'700'000'000'123,
        0,
        1,
        8'000,
        1,
        {0.25});
    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Idle) ==
            SoundRecorderCommandResult::Applied,
        "Collision recorder did not close its first file");
    const auto first_files = wav_files(directory);
    require(
        first_files.size() == 1 && events.size() == 1,
        "Collision recorder did not create its first file");
    const auto first_path = first_files.front();
    const auto first_bytes = read_bytes(first_path);

    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Continuous) ==
            SoundRecorderCommandResult::Applied,
        "Collision recorder did not start its second file");
    publish_audio(
        blocks.audio,
        1'700'000'000'123,
        1,
        1,
        8'000,
        1,
        {-0.25, -0.5});
    require(
        recorder.set_operation_mode(
            SoundRecorderOperationMode::Idle) ==
            SoundRecorderCommandResult::Applied,
        "Collision recorder did not close its second file");

    const auto files = wav_files(directory);
    require(
        files.size() == 2 &&
            events.size() == 2 &&
            events[0].path != events[1].path &&
            !std::filesystem::path(events[0].path).is_absolute() &&
            !std::filesystem::path(events[1].path).is_absolute() &&
            std::filesystem::exists(directory / events[0].path) &&
            std::filesystem::exists(directory / events[1].path),
        "Timestamp collision did not allocate distinct relative paths");
    require(
        read_bytes(first_path) == first_bytes,
        "Timestamp collision overwrote the first recording");
    recorder.stop();
}

} // namespace

int main() {
    try {
        TemporaryDirectory temporary;
        check_idle_manual_transport_and_pcm16(
            temporary.path());
        check_time_segmentation_and_flush(
            temporary.path());
        check_megabyte_segmentation(
            temporary.path());
        check_integer_depth(
            temporary.path(),
            8,
            {0x00, 0x80, 0xFF});
        check_integer_depth(
            temporary.path(),
            24,
            {
                0x00, 0x00, 0x80,
                0x00, 0x00, 0x00,
                0xFF, 0xFF, 0x7F,
            });
        check_integer_depth(
            temporary.path(),
            32,
            {
                0x00, 0x00, 0x00, 0x80,
                0x00, 0x00, 0x00, 0x00,
                0xFF, 0xFF, 0xFF, 0x7F,
            });
        check_unsupported_buffer(temporary.path());
        check_invalid_filename_initials(
            temporary.path());
        check_collision_safe_creation(
            temporary.path());
        std::cout
            << "Sound Recorder node checks passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
