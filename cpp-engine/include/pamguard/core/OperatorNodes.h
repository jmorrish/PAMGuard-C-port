#pragma once

#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "pamguard/core/DataModel.h"
#include "pamguard/core/AudioFrame.h"
#include "pamguard/core/ClipGeneratorSettings.h"
#include "pamguard/core/ModuleNode.h"
#include "pamguard/core/SoundRecorderSettings.h"

namespace pamguard::core {

inline constexpr const char* kLevelMeasurementDataType =
    "pamguard.level-measurement";
inline constexpr const char* kRecordingEventDataType =
    "pamguard.recording-event";
inline constexpr const char* kAlarmStateDataType =
    "pamguard.alarm-state";
inline constexpr const char* kOperatorEventDataType =
    "pamguard.operator-event";
inline constexpr const char* kStorageHealthDataType =
    "pamguard.storage-health";
inline constexpr const char* kAudioClipDataType =
    "pamguard.audio-clip";

struct GraphLevelMeasurement {
    std::vector<double> rms_dbfs;
    std::vector<double> peak_dbfs;
    std::uint64_t measured_frames = 0;
};

struct GraphRecordingEvent {
    // Recorder-root-relative generic path; never a host absolute path.
    std::string path;
    std::string state;
    std::uint64_t start_sample = 0;
    std::uint64_t frame_count = 0;
    std::uint32_t sample_rate_hz = 0;
    std::uint32_t channel_count = 0;
};

struct GraphAlarmState {
    bool active = false;
    std::uint64_t event_count = 0;
    std::uint64_t threshold = 0;
    double window_seconds = 0.0;
    std::string message;
};

struct GraphOperatorEvent {
    std::string category;
    std::string label;
    std::string notes;
    double value = 0.0;
};

struct GraphStorageHealth {
    std::string path;
    bool available = false;
    std::uint64_t capacity_bytes = 0;
    std::uint64_t free_bytes = 0;
    std::uint64_t available_bytes = 0;
    double available_percent = 0.0;
    std::string status;
};

struct GraphAudioClip {
    std::uint64_t trigger_uid = 0;
    std::int64_t trigger_time_unix_ms = 0;
    std::int64_t trigger_start_sample = 0;
    std::uint64_t trigger_duration_samples = 0;
    std::uint32_t trigger_channel_bitmap = 0;
    std::string trigger_source_unit_id;
    std::string trigger_source_output_role;
    std::string trigger_runtime_block_id;
    std::string trigger_data_type;
    std::int64_t clip_start_time_unix_ms = 0;
    std::int64_t clip_start_sample = 0;
    std::uint32_t sample_rate_hz = 0;
    std::uint32_t channel_count = 0;
    std::uint32_t selected_channel_bitmap = 0;
    std::string clip_prefix;
    bool incomplete = false;
    std::vector<double> interleaved_pcm;
};

struct LevelMeterNodeConfig {
    double interval_seconds = 0.25;
    std::uint32_t channel_bitmap = 0xFFFFFFFFu;
};

class LevelMeterNode final : public ModuleNode {
public:
    LevelMeterNode(
        std::string instance_id,
        LevelMeterNodeConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~LevelMeterNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void flush() override;
    void reset() override;

private:
    void process(const DataUnit& unit);
    void publish(const DataUnitMetadata& metadata);

    std::string instance_id_;
    LevelMeterNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    std::vector<long double> sum_squares_;
    std::vector<double> peaks_;
    std::uint64_t measured_frames_ = 0;
    DataUnitMetadata last_metadata_;
    std::uint64_t next_uid_ = 1;
};

struct SoundRecorderNodeConfig {
    std::filesystem::path directory;
    /**
     * Compatibility override for the old low-level node API. An empty value
     * uses SoundRecorderSettings::file_initials (Java's "PAM" default).
     */
    std::string file_prefix;
    /**
     * Compatibility override for the old low-level node API. A positive
     * value replaces the settings time limit; zero uses the settings fields.
     */
    double segment_seconds = 0.0;
    SoundRecorderSettings settings;
};

enum class SoundRecorderTransportState {
    Off,
    Continuous,
};

enum class SoundRecorderCommandResult {
    Applied,
    AlreadyInRequestedMode,
    NodeNotRunning,
    UnsupportedOperationMode,
};

struct SoundRecorderNodeStatus {
    SoundRecorderTransportState transport =
        SoundRecorderTransportState::Off;
    bool file_open = false;
    std::filesystem::path current_path;
    std::uint64_t frames_in_current_file = 0;
    std::uint64_t completed_file_count = 0;
    std::uint32_t selected_channel_bitmap = 0;
    std::uint32_t sample_rate_hz = 0;
    std::uint32_t channel_count = 0;
    int bit_depth = 16;
};

class SoundRecorderNode final : public ModuleNode {
public:
    SoundRecorderNode(
        std::string instance_id,
        SoundRecorderNodeConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> events);
    ~SoundRecorderNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void flush() override;
    void reset() override;

    /**
     * Operator transport command. Cycle and RestoreLast are deliberately
     * reported as unsupported until their Java scheduling semantics exist.
     * Starting the graph always leaves this transport Off.
     */
    [[nodiscard]] SoundRecorderCommandResult set_transport_state(
        SoundRecorderTransportState state);
    [[nodiscard]] SoundRecorderCommandResult set_operation_mode(
        SoundRecorderOperationMode mode);
    [[nodiscard]] SoundRecorderNodeStatus recorder_status() const;

private:
    struct PendingRecordingEvent {
        GraphRecordingEvent event;
        DataUnitMetadata metadata;
    };

    void process(const DataUnit& unit);
    void open_file_locked(
        const DataUnitMetadata& metadata,
        std::uint32_t rate,
        std::uint32_t channels);
    [[nodiscard]] PendingRecordingEvent close_file_locked(
        const std::string& state);
    void write_header_locked(std::uint32_t data_bytes);
    void write_sample_locked(double sample);
    [[nodiscard]] std::uint64_t maximum_frames_per_file_locked() const;
    void publish_event(PendingRecordingEvent event);

    std::string instance_id_;
    SoundRecorderNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> events_;
    Subscription subscription_;
    std::atomic<ModuleState> state_ = ModuleState::Created;
    mutable std::mutex recorder_mutex_;
    SoundRecorderTransportState transport_ =
        SoundRecorderTransportState::Off;
    std::ofstream stream_;
    std::filesystem::path current_path_;
    std::uint64_t recording_start_sample_ = 0;
    std::int64_t recording_start_time_ms_ = 0;
    std::uint64_t recorded_frames_ = 0;
    std::uint32_t sample_rate_hz_ = 0;
    std::uint32_t channel_count_ = 0;
    std::uint32_t selected_channel_bitmap_ = 0;
    std::uint64_t completed_file_count_ = 0;
    std::uint64_t next_uid_ = 1;
};

struct AlarmEventCounterNodeConfig {
    std::uint64_t count_threshold = 1;
    double window_seconds = 10.0;
    std::string message = "Detection alarm";
};

class AlarmEventCounterNode final : public ModuleNode {
public:
    AlarmEventCounterNode(
        std::string instance_id,
        AlarmEventCounterNodeConfig config,
        std::shared_ptr<DataBlock> input,
        std::shared_ptr<DataBlock> output);
    ~AlarmEventCounterNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;

private:
    void process(const DataUnit& unit);

    std::string instance_id_;
    AlarmEventCounterNodeConfig config_;
    std::shared_ptr<DataBlock> input_;
    std::shared_ptr<DataBlock> output_;
    Subscription subscription_;
    ModuleState state_ = ModuleState::Created;
    std::vector<std::int64_t> event_times_ms_;
    std::uint64_t next_uid_ = 1;
};

struct ClipGeneratorNodeConfig {
    /**
     * Compatibility fields for direct low-level construction. Projected
     * runtimes carry pre/post time on each trigger policy instead.
     */
    double pre_trigger_seconds = 0.25;
    double post_trigger_seconds = 0.5;
    double maximum_buffer_seconds = 30.0;
    ClipGeneratorStorageMode storage_mode =
        ClipGeneratorStorageMode::Binary;
    /**
     * Optional deterministic source for StandardClipBudgetMaker's
     * Math.random() decision. The argument is the trigger-policy index.
     */
    std::function<double(std::size_t)> random_unit_interval;
};

struct ClipGeneratorTriggerInput {
    std::shared_ptr<DataBlock> input;
    std::string source_unit_id;
    std::string source_output_role;
    std::string runtime_block_id;
    std::string source_data_type;
    bool enabled = true;
    double pre_trigger_seconds = 0.0;
    double post_trigger_seconds = 0.0;
    ClipGeneratorChannelSelection channel_selection =
        ClipGeneratorChannelSelection::DetectionChannelsOnly;
    std::string clip_prefix;
    bool use_data_budget = true;
    int data_budget_kilobytes = 10 * 1024;
    double budget_period_hours = 24.0;
};

class ClipGeneratorNode final : public ModuleNode {
public:
    /**
     * Java-equivalent multi-source constructor. An empty trigger list is a
     * valid, idle Clip Generator configuration.
     */
    ClipGeneratorNode(
        std::string instance_id,
        ClipGeneratorNodeConfig config,
        std::shared_ptr<DataBlock> audio_input,
        std::vector<ClipGeneratorTriggerInput> trigger_inputs,
        std::shared_ptr<DataBlock> output);
    /**
     * Compatibility constructor for the original single-trigger low-level
     * API. It creates one enabled, unlimited-budget policy.
     */
    ClipGeneratorNode(
        std::string instance_id,
        ClipGeneratorNodeConfig config,
        std::shared_ptr<DataBlock> audio_input,
        std::shared_ptr<DataBlock> trigger_input,
        std::shared_ptr<DataBlock> output);
    ~ClipGeneratorNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void flush() override;
    void reset() override;

private:
    struct BufferedAudio {
        AudioChunk audio;
        bool discontinuity = false;
    };

    struct BudgetState {
        bool period_initialised = false;
        std::int64_t period_milliseconds = 0;
        std::int64_t period_start_unix_ms = 0;
        std::int64_t period_end_unix_ms = 0;
        std::uint64_t stored_clips = 0;
        std::int64_t total_stored_size = 0;
        std::int64_t budget_size = 0;
        std::int64_t average_clip_size = 48000;
        bool use_budget = false;
    };

    struct PendingTrigger {
        DataUnitMetadata metadata;
        std::size_t policy_index = 0;
    };

    void process_audio(const DataUnit& unit);
    void process_trigger(
        std::size_t policy_index,
        const DataUnit& unit);
    void emit_ready();
    [[nodiscard]] bool emit(const PendingTrigger& trigger);
    [[nodiscard]] bool should_store(
        std::size_t policy_index,
        const DataUnitMetadata& metadata);
    [[nodiscard]] std::uint32_t selected_channel_bitmap(
        const ClipGeneratorTriggerInput& policy,
        std::uint32_t trigger_bitmap) const noexcept;

    std::string instance_id_;
    ClipGeneratorNodeConfig config_;
    std::shared_ptr<DataBlock> audio_input_;
    std::vector<ClipGeneratorTriggerInput> trigger_inputs_;
    std::shared_ptr<DataBlock> output_;
    Subscription audio_subscription_;
    std::vector<Subscription> trigger_subscriptions_;
    ModuleState state_ = ModuleState::Created;
    std::deque<BufferedAudio> audio_;
    std::deque<PendingTrigger> pending_;
    std::vector<BudgetState> budgets_;
    std::int64_t latest_sample_end_ = 0;
    std::uint64_t next_uid_ = 1;
    std::mt19937_64 random_engine_{std::random_device{}()};
    std::mutex mutex_;
};

class OperatorInputNode final : public ModuleNode {
public:
    OperatorInputNode(
        std::string instance_id,
        std::string default_category,
        std::shared_ptr<DataBlock> output);

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;
    void publish(
        GraphOperatorEvent event,
        std::int64_t time_unix_ms,
        std::int64_t start_sample = 0);

private:
    std::string instance_id_;
    std::string default_category_;
    std::shared_ptr<DataBlock> output_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
};

class StorageHealthNode final : public ModuleNode {
public:
    StorageHealthNode(
        std::string instance_id,
        std::filesystem::path path,
        double warning_free_percent,
        double interval_seconds,
        std::shared_ptr<DataBlock> output);
    ~StorageHealthNode() override;

    [[nodiscard]] const std::string& instance_id() const noexcept override;
    [[nodiscard]] ModuleState state() const noexcept override;
    void prepare() override;
    void start() override;
    void stop() override;
    void reset() override;

private:
    void publish();

    std::string instance_id_;
    std::filesystem::path path_;
    double warning_free_percent_ = 10.0;
    double interval_seconds_ = 30.0;
    std::shared_ptr<DataBlock> output_;
    ModuleState state_ = ModuleState::Created;
    std::uint64_t next_uid_ = 1;
    std::mutex worker_mutex_;
    std::condition_variable worker_condition_;
    bool stop_requested_ = false;
    std::thread worker_;
};

} // namespace pamguard::core
