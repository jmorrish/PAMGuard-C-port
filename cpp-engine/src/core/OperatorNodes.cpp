#include "pamguard/core/OperatorNodes.h"

#include "pamguard/core/AudioFrame.h"
#include "pamguard/core/SignalNodes.h"

#include <algorithm>
#include <any>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#include <io.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pamguard::core {

namespace {

const AudioChunk& audio_payload(const DataUnit& unit) {
    const auto* audio = std::any_cast<AudioChunk>(&unit.payload);
    if (audio == nullptr) {
        throw std::invalid_argument(
            "Operator audio module received a non-audio payload");
    }
    return *audio;
}

double dbfs(double amplitude) {
    return 20.0 * std::log10(std::max(amplitude, 1e-12));
}

template <typename Value>
void write_little_endian(std::ostream& output, Value value) {
    output.write(
        reinterpret_cast<const char*>(&value),
        sizeof(Value));
}

std::int64_t now_unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::tm utc_time(std::time_t value) {
    std::tm result{};
#if defined(_WIN32)
    if (gmtime_s(&result, &value) != 0) {
        throw std::runtime_error(
            "Could not convert Sound Recorder file time to UTC");
    }
#else
    if (gmtime_r(&value, &result) == nullptr) {
        throw std::runtime_error(
            "Could not convert Sound Recorder file time to UTC");
    }
#endif
    return result;
}

std::string recorder_timestamp(std::int64_t unix_ms) {
    auto seconds = unix_ms / 1000;
    auto milliseconds = unix_ms % 1000;
    if (milliseconds < 0) {
        milliseconds += 1000;
        --seconds;
    }
    const auto calendar = utc_time(
        static_cast<std::time_t>(seconds));
    std::ostringstream text;
    text << std::put_time(
                &calendar,
                "%Y%m%d_%H%M%S")
         << '_' << std::setfill('0')
         << std::setw(3) << milliseconds;
    return text.str();
}

std::string recorder_date(std::int64_t unix_ms) {
    auto seconds = unix_ms / 1000;
    if (unix_ms < 0 && unix_ms % 1000 != 0) {
        --seconds;
    }
    const auto calendar = utc_time(
        static_cast<std::time_t>(seconds));
    std::ostringstream text;
    text << std::put_time(&calendar, "%Y%m%d");
    return text.str();
}

std::string uppercase_ascii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                std::toupper(character));
        });
    return value;
}

bool contains_invalid_recorder_filename_character(
    std::string_view value) noexcept {
    return std::any_of(
        value.begin(),
        value.end(),
        [](char character) {
            const auto byte =
                static_cast<unsigned char>(character);
            if (byte < 0x20 || byte == 0x7F) {
                return true;
            }
            switch (character) {
            case '<':
            case '>':
            case ':':
            case '"':
            case '/':
            case '\\':
            case '|':
            case '?':
            case '*':
                return true;
            default:
                return false;
            }
        });
}

enum class RecorderFileReservation {
    Reserved,
    AlreadyExists,
    Failed,
};

RecorderFileReservation reserve_recorder_file(
    const std::filesystem::path& path) {
    errno = 0;
#if defined(_WIN32)
    const auto descriptor = _wopen(
        path.c_str(),
        _O_WRONLY | _O_CREAT | _O_EXCL |
            _O_BINARY | _O_NOINHERIT,
        _S_IREAD | _S_IWRITE);
#else
    auto flags = O_WRONLY | O_CREAT | O_EXCL;
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
    const auto descriptor = ::open(
        path.c_str(),
        flags,
        S_IRUSR | S_IWUSR);
#endif
    if (descriptor < 0) {
        return errno == EEXIST
            ? RecorderFileReservation::AlreadyExists
            : RecorderFileReservation::Failed;
    }

#if defined(_WIN32)
    const auto close_result = _close(descriptor);
#else
    const auto close_result = ::close(descriptor);
#endif
    if (close_result == 0) {
        return RecorderFileReservation::Reserved;
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return RecorderFileReservation::Failed;
}

std::string recorder_event_path(
    const std::filesystem::path& path,
    const std::filesystem::path& recorder_root) {
    const auto fallback = path.filename().generic_string();
    const auto relative =
        path.lexically_relative(recorder_root);
    if (relative.empty() || relative.is_absolute()) {
        return fallback;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return fallback;
        }
    }
    const auto value = relative.generic_string();
    return value.empty() || value == "."
        ? fallback
        : value;
}

std::vector<std::size_t> recorder_channel_slots(
    std::uint32_t source_bitmap,
    std::size_t source_channel_count,
    std::uint32_t selected_bitmap) {
    std::vector<std::size_t> physical_channels;
    if (source_bitmap != 0) {
        for (std::size_t channel = 0; channel < 32; ++channel) {
            if ((source_bitmap &
                 (std::uint32_t{1} << channel)) != 0) {
                physical_channels.push_back(channel);
            }
        }
    }
    if (physical_channels.size() != source_channel_count) {
        physical_channels.clear();
        for (std::size_t channel = 0;
             channel < source_channel_count && channel < 32;
             ++channel) {
            physical_channels.push_back(channel);
        }
    }

    std::vector<std::size_t> result;
    for (std::size_t slot = 0;
         slot < physical_channels.size();
         ++slot) {
        if ((selected_bitmap &
             (std::uint32_t{1} <<
              physical_channels[slot])) != 0) {
            result.push_back(slot);
        }
    }
    return result;
}

} // namespace

LevelMeterNode::LevelMeterNode(
    std::string instance_id,
    LevelMeterNodeConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument(
            "Level meter requires an instance id and data blocks");
    }
}

LevelMeterNode::~LevelMeterNode() { stop(); }
const std::string& LevelMeterNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState LevelMeterNode::state() const noexcept { return state_; }

void LevelMeterNode::prepare() {
    if (input_->descriptor().data_type != kRawAudioDataType ||
        output_->descriptor().data_type != kLevelMeasurementDataType ||
        config_.interval_seconds <= 0.0) {
        throw std::invalid_argument(
            "Level meter requires raw audio and a positive interval");
    }
    state_ = ModuleState::Prepared;
}

void LevelMeterNode::start() {
    if (state_ != ModuleState::Prepared &&
        state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Level meter must be prepared before it starts");
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void LevelMeterNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void LevelMeterNode::flush() {
    if (measured_frames_ != 0) {
        publish(last_metadata_);
    }
}

void LevelMeterNode::reset() {
    stop();
    sum_squares_.clear();
    peaks_.clear();
    measured_frames_ = 0;
    next_uid_ = 1;
    state_ = ModuleState::Created;
}

void LevelMeterNode::process(const DataUnit& unit) {
    const auto& audio = audio_payload(unit);
    if (sum_squares_.size() != audio.channel_count) {
        sum_squares_.assign(audio.channel_count, 0.0L);
        peaks_.assign(audio.channel_count, 0.0);
        measured_frames_ = 0;
    }
    for (std::size_t frame = 0; frame < audio.frame_count(); ++frame) {
        for (std::size_t channel = 0;
             channel < audio.channel_count;
             ++channel) {
            if (channel < 32 &&
                (config_.channel_bitmap &
                 (std::uint32_t{1} << channel)) == 0) {
                continue;
            }
            const auto sample = audio.sample(frame, channel);
            sum_squares_[channel] += sample * sample;
            peaks_[channel] =
                std::max(peaks_[channel], std::abs(sample));
        }
    }
    measured_frames_ += audio.frame_count();
    last_metadata_ = unit.metadata;
    const auto required_frames = static_cast<std::uint64_t>(
        std::max(
            1.0,
            config_.interval_seconds * audio.sample_rate_hz));
    if (measured_frames_ >= required_frames) {
        publish(unit.metadata);
    }
}

void LevelMeterNode::publish(const DataUnitMetadata& input) {
    GraphLevelMeasurement result;
    result.measured_frames = measured_frames_;
    result.rms_dbfs.reserve(sum_squares_.size());
    result.peak_dbfs.reserve(peaks_.size());
    for (std::size_t channel = 0;
         channel < sum_squares_.size();
         ++channel) {
        const auto rms = std::sqrt(
            static_cast<double>(sum_squares_[channel] /
                std::max<std::uint64_t>(1, measured_frames_)));
        result.rms_dbfs.push_back(dbfs(rms));
        result.peak_dbfs.push_back(dbfs(peaks_[channel]));
    }
    auto metadata = input;
    metadata.type_id.clear();
    metadata.source_block_id.clear();
    metadata.uid = next_uid_++;
    metadata.sequence = metadata.uid;
    metadata.duration_samples = measured_frames_;
    output_->publish(
        make_data_unit(std::move(metadata), std::move(result)));
    std::fill(sum_squares_.begin(), sum_squares_.end(), 0.0L);
    std::fill(peaks_.begin(), peaks_.end(), 0.0);
    measured_frames_ = 0;
}

SoundRecorderNode::SoundRecorderNode(
    std::string instance_id,
    SoundRecorderNodeConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> events)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      events_(std::move(events)) {
    if (instance_id_.empty() || !input_ || !events_) {
        throw std::invalid_argument(
            "Sound recorder requires an instance id and data blocks");
    }
}

SoundRecorderNode::~SoundRecorderNode() {
    try {
        stop();
    }
    catch (...) {
        // Destructors cannot surface storage or observer failures. stop()
        // remains the explicit error-reporting close path for callers.
    }
}
const std::string& SoundRecorderNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState SoundRecorderNode::state() const noexcept {
    return state_.load();
}

void SoundRecorderNode::prepare() {
    const auto& settings = config_.settings;
    const auto& filename_initials =
        config_.file_prefix.empty()
        ? settings.file_initials
        : config_.file_prefix;
    if (contains_invalid_recorder_filename_character(
            filename_initials)) {
        throw std::invalid_argument(
            "Sound Recorder file initials contain a character "
            "that is not valid in an output filename");
    }
    const auto file_type =
        uppercase_ascii(settings.file_type);
    const auto bit_depth_supported =
        settings.bit_depth == 8 ||
        settings.bit_depth == 16 ||
        settings.bit_depth == 24 ||
        settings.bit_depth == 32;
    const auto enabled_trigger = std::any_of(
        settings.trigger_policies.begin(),
        settings.trigger_policies.end(),
        [](const auto& policy) {
            return policy.enabled;
        });
    if (input_->descriptor().data_type != kRawAudioDataType ||
        events_->descriptor().data_type != kRecordingEventDataType ||
        config_.directory.empty() ||
        !std::isfinite(config_.segment_seconds) ||
        config_.segment_seconds < 0.0 ||
        settings.channel_bitmap == 0 ||
        !bit_depth_supported ||
        (file_type != "WAVE" && file_type != "WAV") ||
        settings.max_length_seconds <= 0 ||
        settings.max_length_megabytes == 0) {
        throw std::invalid_argument(
            "Sound recorder settings and data blocks are invalid");
    }
    if (settings.enable_buffer) {
        throw std::invalid_argument(
            "Sound Recorder buffering is not implemented; "
            "Continuous+Buffer must not be selected");
    }
    if (enabled_trigger) {
        throw std::invalid_argument(
            "Sound Recorder trigger policies are not implemented");
    }
    const auto available =
        input_->descriptor().channel_bitmap;
    selected_channel_bitmap_ =
        available == 0
        ? settings.channel_bitmap
        : settings.channel_bitmap & available;
    if (selected_channel_bitmap_ == 0) {
        throw std::invalid_argument(
            "Sound Recorder has no selected input channels");
    }
    {
        std::lock_guard lock(recorder_mutex_);
        transport_ = SoundRecorderTransportState::Off;
    }
    state_ = ModuleState::Prepared;
}

void SoundRecorderNode::start() {
    const auto current_state = state_.load();
    if (current_state != ModuleState::Prepared &&
        current_state != ModuleState::Stopped) {
        throw std::logic_error(
            "Sound recorder must be prepared before it starts");
    }
    {
        std::lock_guard lock(recorder_mutex_);
        // Graph execution and recorder transport are intentionally separate.
        // This matches a fresh Java RecorderSettings startStatus of Off.
        transport_ = SoundRecorderTransportState::Off;
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void SoundRecorderNode::stop() {
    subscription_.cancel();
    PendingRecordingEvent event;
    bool publish = false;
    {
        std::lock_guard lock(recorder_mutex_);
        if (state_.load() == ModuleState::Running) {
            state_ = ModuleState::Stopped;
        }
        transport_ = SoundRecorderTransportState::Off;
        if (stream_.is_open()) {
            event = close_file_locked("completed");
            publish = true;
        }
    }
    if (publish) {
        publish_event(std::move(event));
    }
}

void SoundRecorderNode::flush() {
    PendingRecordingEvent event;
    bool publish = false;
    {
        std::lock_guard lock(recorder_mutex_);
        if (stream_.is_open()) {
            event = close_file_locked("flushed");
            publish = true;
        }
    }
    if (publish) {
        publish_event(std::move(event));
    }
}

void SoundRecorderNode::reset() {
    stop();
    {
        std::lock_guard lock(recorder_mutex_);
        next_uid_ = 1;
        completed_file_count_ = 0;
        selected_channel_bitmap_ = 0;
    }
    state_ = ModuleState::Created;
}

SoundRecorderCommandResult
SoundRecorderNode::set_operation_mode(
    SoundRecorderOperationMode mode) {
    if (mode == SoundRecorderOperationMode::Cycle ||
        mode == SoundRecorderOperationMode::RestoreLast) {
        return SoundRecorderCommandResult::
            UnsupportedOperationMode;
    }
    return set_transport_state(
        mode == SoundRecorderOperationMode::Continuous
        ? SoundRecorderTransportState::Continuous
        : SoundRecorderTransportState::Off);
}

SoundRecorderCommandResult
SoundRecorderNode::set_transport_state(
    SoundRecorderTransportState requested) {
    if (state_.load() != ModuleState::Running) {
        return SoundRecorderCommandResult::NodeNotRunning;
    }

    PendingRecordingEvent event;
    bool publish = false;
    SoundRecorderCommandResult result =
        SoundRecorderCommandResult::Applied;
    {
        std::lock_guard lock(recorder_mutex_);
        if (state_.load() != ModuleState::Running) {
            return SoundRecorderCommandResult::NodeNotRunning;
        }
        if (transport_ == requested) {
            return SoundRecorderCommandResult::
                AlreadyInRequestedMode;
        }
        transport_ = requested;
        if (requested == SoundRecorderTransportState::Off &&
            stream_.is_open()) {
            event = close_file_locked("completed");
            publish = true;
        }
    }
    if (publish) {
        publish_event(std::move(event));
    }
    return result;
}

SoundRecorderNodeStatus
SoundRecorderNode::recorder_status() const {
    std::lock_guard lock(recorder_mutex_);
    SoundRecorderNodeStatus status;
    status.transport = transport_;
    status.file_open = stream_.is_open();
    status.current_path = current_path_;
    status.frames_in_current_file = recorded_frames_;
    status.completed_file_count = completed_file_count_;
    status.selected_channel_bitmap =
        selected_channel_bitmap_;
    status.sample_rate_hz = sample_rate_hz_;
    status.channel_count = channel_count_;
    status.bit_depth = config_.settings.bit_depth;
    return status;
}

void SoundRecorderNode::open_file_locked(
    const DataUnitMetadata& metadata,
    std::uint32_t rate,
    std::uint32_t channels) {
    auto directory = config_.directory;
    if (config_.settings.dated_subfolders) {
        directory /= recorder_date(
            metadata.time_unix_ms);
    }
    std::error_code directory_error;
    std::filesystem::create_directories(
        directory,
        directory_error);
    if (directory_error) {
        throw std::runtime_error(
            "Could not prepare the Sound Recorder output directory");
    }
    const auto& prefix = config_.file_prefix.empty()
        ? config_.settings.file_initials
        : config_.file_prefix;
    const auto filename_stem =
        prefix + "_" +
        recorder_timestamp(metadata.time_unix_ms);
    constexpr std::uint32_t kMaximumCollisionSuffix =
        1'000'000;
    current_path_.clear();
    for (std::uint32_t suffix = 0;
         suffix <= kMaximumCollisionSuffix;
         ++suffix) {
        const auto filename =
            filename_stem +
            (suffix == 0
                 ? std::string{}
                 : "-" + std::to_string(suffix)) +
            ".wav";
        const auto candidate = directory / filename;
        const auto reservation =
            reserve_recorder_file(candidate);
        if (reservation ==
            RecorderFileReservation::Reserved) {
            current_path_ = candidate;
            break;
        }
        if (reservation ==
            RecorderFileReservation::Failed) {
            throw std::runtime_error(
                "Could not reserve a Sound Recorder output file");
        }
    }
    if (current_path_.empty()) {
        throw std::runtime_error(
            "Sound Recorder could not allocate a unique output "
            "filename");
    }

    stream_.clear();
    stream_.open(
        current_path_,
        std::ios::binary | std::ios::in);
    if (!stream_) {
        std::error_code ignored;
        std::filesystem::remove(current_path_, ignored);
        current_path_.clear();
        throw std::runtime_error(
            "Could not open the reserved Sound Recorder output file");
    }
    recording_start_sample_ = static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, metadata.start_sample));
    recording_start_time_ms_ = metadata.time_unix_ms;
    recorded_frames_ = 0;
    sample_rate_hz_ = rate;
    channel_count_ = channels;
    write_header_locked(0);
    if (!stream_) {
        stream_.close();
        std::error_code ignored;
        std::filesystem::remove(current_path_, ignored);
        current_path_.clear();
        recorded_frames_ = 0;
        sample_rate_hz_ = 0;
        channel_count_ = 0;
        throw std::runtime_error(
            "Could not initialise the Sound Recorder output file");
    }
}

void SoundRecorderNode::write_header_locked(
    std::uint32_t data_bytes) {
    const auto bytes_per_sample = static_cast<std::uint32_t>(
        config_.settings.bit_depth / 8);
    stream_.clear();
    stream_.seekp(0);
    stream_.write("RIFF", 4);
    write_little_endian(
        stream_,
        static_cast<std::uint32_t>(36 + data_bytes));
    stream_.write("WAVEfmt ", 8);
    write_little_endian(stream_, std::uint32_t{16});
    write_little_endian(stream_, std::uint16_t{1});
    write_little_endian(
        stream_,
        static_cast<std::uint16_t>(channel_count_));
    write_little_endian(stream_, sample_rate_hz_);
    write_little_endian(
        stream_,
        sample_rate_hz_ * channel_count_ *
            bytes_per_sample);
    write_little_endian(
        stream_,
        static_cast<std::uint16_t>(
            channel_count_ * bytes_per_sample));
    write_little_endian(
        stream_,
        static_cast<std::uint16_t>(
            config_.settings.bit_depth));
    stream_.write("data", 4);
    write_little_endian(stream_, data_bytes);
}

void SoundRecorderNode::write_sample_locked(double sample) {
    if (!std::isfinite(sample)) {
        sample = 0.0;
    }
    sample = std::clamp(sample, -1.0, 1.0);
    const auto bit_depth = config_.settings.bit_depth;
    if (bit_depth == 8) {
        const auto value = sample <= -1.0
            ? std::int64_t{-128}
            : sample >= 1.0
                ? std::int64_t{127}
                : static_cast<std::int64_t>(
                    sample * 128.0);
        stream_.put(static_cast<char>(
            static_cast<std::uint8_t>(value + 128)));
        return;
    }

    const auto scale =
        std::uint64_t{1} << (bit_depth - 1);
    const auto maximum =
        static_cast<std::int64_t>(scale - 1);
    const auto minimum =
        -static_cast<std::int64_t>(scale);
    const auto value = sample <= -1.0
        ? minimum
        : sample >= 1.0
            ? maximum
            : static_cast<std::int64_t>(
                sample * static_cast<double>(scale));
    const auto encoded = static_cast<std::uint64_t>(value);
    for (int byte = 0; byte < bit_depth / 8; ++byte) {
        stream_.put(static_cast<char>(
            (encoded >> (byte * 8)) & 0xFFu));
    }
}

std::uint64_t
SoundRecorderNode::maximum_frames_per_file_locked() const {
    const auto bytes_per_frame =
        static_cast<std::uint64_t>(channel_count_) *
        static_cast<std::uint64_t>(
            config_.settings.bit_depth / 8);
    if (bytes_per_frame == 0) {
        throw std::logic_error(
            "Sound Recorder cannot calculate an empty WAV frame");
    }
    std::uint64_t maximum =
        (std::numeric_limits<std::uint32_t>::max() -
         std::uint64_t{36}) /
        bytes_per_frame;

    const auto seconds = config_.segment_seconds > 0.0
        ? config_.segment_seconds
        : config_.settings.limit_length_seconds
            ? static_cast<double>(
                config_.settings.max_length_seconds)
            : 0.0;
    if (seconds > 0.0) {
        long double duration =
            static_cast<long double>(seconds) *
            sample_rate_hz_;
        if (config_.segment_seconds <= 0.0 &&
            config_.settings.limit_length_seconds &&
            config_.settings.round_file_starts) {
            const auto interval_ms =
                static_cast<std::int64_t>(
                    config_.settings.max_length_seconds) *
                std::int64_t{1000};
            auto boundary_offset_ms =
                recording_start_time_ms_ %
                interval_ms;
            if (boundary_offset_ms < 0) {
                boundary_offset_ms += interval_ms;
            }
            auto file_duration_ms =
                interval_ms - boundary_offset_ms;
            if (file_duration_ms < 2'000) {
                file_duration_ms += interval_ms;
            }
            duration =
                static_cast<long double>(
                    file_duration_ms) *
                sample_rate_hz_ /
                1000.0L;
        }
        const auto duration_frames =
            duration >= static_cast<long double>(
                std::numeric_limits<std::uint64_t>::max())
            ? std::numeric_limits<std::uint64_t>::max()
            : static_cast<std::uint64_t>(
                std::max(
                    1.0L,
                    std::floor(duration)));
        maximum = std::min(
            maximum,
            duration_frames);
    }
    if (config_.settings.limit_length_megabytes) {
        const auto megabytes =
            config_.settings.max_length_megabytes;
        const auto byte_limit =
            megabytes >
                std::numeric_limits<std::uint64_t>::max() /
                    (std::uint64_t{1} << 20)
            ? std::numeric_limits<std::uint64_t>::max()
            : megabytes * (std::uint64_t{1} << 20);
        maximum = std::min(
            maximum,
            byte_limit / bytes_per_frame);
    }
    if (maximum == 0) {
        throw std::invalid_argument(
            "Sound Recorder file limits cannot contain one frame");
    }
    return maximum;
}

SoundRecorderNode::PendingRecordingEvent
SoundRecorderNode::close_file_locked(
    const std::string& state) {
    const auto data_bytes_64 =
        recorded_frames_ * channel_count_ *
        static_cast<std::uint64_t>(
            config_.settings.bit_depth / 8);
    const auto data_bytes = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(
            data_bytes_64,
            std::numeric_limits<std::uint32_t>::max()));
    write_header_locked(data_bytes);
    stream_.flush();
    if (!stream_) {
        stream_.close();
        current_path_.clear();
        throw std::runtime_error(
            "Sound Recorder failed while finalising an output file");
    }
    stream_.close();

    GraphRecordingEvent event{
        recorder_event_path(
            current_path_,
            config_.directory),
        state,
        recording_start_sample_,
        recorded_frames_,
        sample_rate_hz_,
        channel_count_,
    };
    DataUnitMetadata metadata;
    metadata.uid = next_uid_++;
    metadata.sequence = metadata.uid;
    metadata.time_unix_ms = recording_start_time_ms_;
    metadata.start_sample =
        static_cast<std::int64_t>(recording_start_sample_);
    metadata.duration_samples = recorded_frames_;
    metadata.channel_bitmap = selected_channel_bitmap_;

    current_path_.clear();
    recorded_frames_ = 0;
    sample_rate_hz_ = 0;
    channel_count_ = 0;
    ++completed_file_count_;
    return {
        std::move(event),
        std::move(metadata),
    };
}

void SoundRecorderNode::publish_event(
    PendingRecordingEvent event) {
    events_->publish(
        make_data_unit(
            std::move(event.metadata),
            std::move(event.event)));
}

void SoundRecorderNode::process(const DataUnit& unit) {
    const auto& audio = audio_payload(unit);
    if (audio.sample_rate_hz == 0 ||
        audio.channel_count == 0 ||
        audio.interleaved_pcm.size() %
            audio.channel_count != 0) {
        throw std::invalid_argument(
            "Sound Recorder received invalid raw audio");
    }
    auto source_bitmap = unit.metadata.channel_bitmap;
    if (source_bitmap == 0) {
        source_bitmap =
            input_->descriptor().channel_bitmap;
    }
    const auto channel_slots = recorder_channel_slots(
        source_bitmap,
        audio.channel_count,
        selected_channel_bitmap_);
    if (channel_slots.empty()) {
        throw std::invalid_argument(
            "Sound Recorder audio has no selected channels");
    }

    std::vector<PendingRecordingEvent> events;
    std::size_t frame_offset = 0;
    {
        std::lock_guard lock(recorder_mutex_);
        if (transport_ !=
                SoundRecorderTransportState::Continuous ||
            state_.load() != ModuleState::Running) {
            return;
        }
        while (frame_offset < audio.frame_count()) {
            if (stream_.is_open() &&
                (sample_rate_hz_ != audio.sample_rate_hz ||
                 channel_count_ != channel_slots.size())) {
                events.push_back(
                    close_file_locked("format-changed"));
            }
            if (!stream_.is_open()) {
                auto metadata = unit.metadata;
                metadata.start_sample +=
                    static_cast<std::int64_t>(
                        frame_offset);
                metadata.time_unix_ms +=
                    static_cast<std::int64_t>(
                        frame_offset *
                        std::uint64_t{1000} /
                        audio.sample_rate_hz);
                open_file_locked(
                    metadata,
                    audio.sample_rate_hz,
                    static_cast<std::uint32_t>(
                        channel_slots.size()));
            }

            const auto maximum_frames =
                maximum_frames_per_file_locked();
            if (recorded_frames_ >= maximum_frames) {
                events.push_back(
                    close_file_locked(
                        "segment-completed"));
                continue;
            }
            const auto available =
                maximum_frames - recorded_frames_;
            const auto frames_to_write =
                std::min<std::uint64_t>(
                    available,
                    audio.frame_count() -
                        frame_offset);
            for (std::uint64_t frame = 0;
                 frame < frames_to_write;
                 ++frame) {
                for (const auto slot : channel_slots) {
                    write_sample_locked(
                        audio.sample(
                            frame_offset + frame,
                            slot));
                }
            }
            recorded_frames_ += frames_to_write;
            frame_offset += static_cast<std::size_t>(
                frames_to_write);
            if (!stream_) {
                throw std::runtime_error(
                    "Sound Recorder failed while writing an output "
                    "file");
            }
            if (recorded_frames_ >= maximum_frames) {
                events.push_back(
                    close_file_locked(
                        "segment-completed"));
            }
        }
    }
    for (auto& event : events) {
        publish_event(std::move(event));
    }
}

AlarmEventCounterNode::AlarmEventCounterNode(
    std::string instance_id,
    AlarmEventCounterNodeConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument(
            "Alarm counter requires an instance id and data blocks");
    }
}

AlarmEventCounterNode::~AlarmEventCounterNode() { stop(); }
const std::string& AlarmEventCounterNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState AlarmEventCounterNode::state() const noexcept { return state_; }

void AlarmEventCounterNode::prepare() {
    if (config_.count_threshold == 0 ||
        config_.window_seconds <= 0.0 ||
        output_->descriptor().data_type != kAlarmStateDataType) {
        throw std::invalid_argument(
            "Alarm counter requires a threshold and positive window");
    }
    state_ = ModuleState::Prepared;
}

void AlarmEventCounterNode::start() {
    if (state_ != ModuleState::Prepared &&
        state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Alarm counter must be prepared before it starts");
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void AlarmEventCounterNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void AlarmEventCounterNode::reset() {
    stop();
    event_times_ms_.clear();
    next_uid_ = 1;
    state_ = ModuleState::Created;
}

void AlarmEventCounterNode::process(const DataUnit& unit) {
    const auto time_ms = unit.metadata.time_unix_ms;
    event_times_ms_.push_back(time_ms);
    const auto cutoff = time_ms -
        static_cast<std::int64_t>(
            std::llround(config_.window_seconds * 1000.0));
    event_times_ms_.erase(
        event_times_ms_.begin(),
        std::lower_bound(
            event_times_ms_.begin(),
            event_times_ms_.end(),
            cutoff));
    GraphAlarmState result{
        event_times_ms_.size() >= config_.count_threshold,
        event_times_ms_.size(),
        config_.count_threshold,
        config_.window_seconds,
        config_.message,
    };
    auto metadata = unit.metadata;
    metadata.type_id.clear();
    metadata.source_block_id.clear();
    metadata.uid = next_uid_++;
    metadata.sequence = metadata.uid;
    metadata.duration_samples = 0;
    output_->publish(
        make_data_unit(std::move(metadata), std::move(result)));
}

namespace {

std::size_t clip_bitmap_count(std::uint32_t bitmap) noexcept {
    std::size_t result = 0;
    while (bitmap != 0) {
        result += bitmap & 1u;
        bitmap >>= 1u;
    }
    return result;
}

std::size_t clip_bitmap_width(std::uint32_t bitmap) noexcept {
    std::size_t result = 0;
    while (bitmap != 0) {
        ++result;
        bitmap >>= 1u;
    }
    return result;
}

std::vector<std::size_t> clip_channels(
    std::uint32_t bitmap) {
    std::vector<std::size_t> result;
    for (std::size_t channel = 0; channel < 32; ++channel) {
        if ((bitmap & (std::uint32_t{1} << channel)) != 0) {
            result.push_back(channel);
        }
    }
    return result;
}

std::optional<std::size_t> clip_channel_slot(
    std::uint32_t available_bitmap,
    std::size_t chunk_channel_count,
    std::size_t physical_channel) noexcept {
    if (physical_channel >= 32 ||
        (available_bitmap &
         (std::uint32_t{1} << physical_channel)) == 0) {
        return std::nullopt;
    }
    /*
     * Engine acquisition currently represents sparse channel maps using
     * physical-index slots. Accept compact ascending-bit packing as well so
     * the Clip Generator is correct for either DataBlock representation.
     */
    if (chunk_channel_count >= clip_bitmap_width(available_bitmap)) {
        return physical_channel < chunk_channel_count
            ? std::optional<std::size_t>(physical_channel)
            : std::nullopt;
    }
    if (chunk_channel_count !=
        clip_bitmap_count(available_bitmap)) {
        return std::nullopt;
    }
    std::size_t slot = 0;
    for (std::size_t channel = 0;
         channel < physical_channel;
         ++channel) {
        if ((available_bitmap &
             (std::uint32_t{1} << channel)) != 0) {
            ++slot;
        }
    }
    return slot;
}

ClipGeneratorTriggerInput legacy_clip_trigger(
    std::shared_ptr<DataBlock> input,
    const ClipGeneratorNodeConfig& config) {
    ClipGeneratorTriggerInput trigger;
    trigger.input = std::move(input);
    if (trigger.input) {
        trigger.source_unit_id =
            trigger.input->descriptor().producer_module_id;
        trigger.source_output_role =
            trigger.input->descriptor().producer_port_id;
        trigger.runtime_block_id =
            trigger.input->descriptor().id;
        trigger.source_data_type =
            trigger.input->descriptor().data_type;
    }
    trigger.pre_trigger_seconds =
        config.pre_trigger_seconds;
    trigger.post_trigger_seconds =
        config.post_trigger_seconds;
    trigger.use_data_budget = false;
    return trigger;
}

bool valid_clip_seconds(double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

} // namespace

ClipGeneratorNode::ClipGeneratorNode(
    std::string instance_id,
    ClipGeneratorNodeConfig config,
    std::shared_ptr<DataBlock> audio_input,
    std::vector<ClipGeneratorTriggerInput> trigger_inputs,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      audio_input_(std::move(audio_input)),
      trigger_inputs_(std::move(trigger_inputs)),
      output_(std::move(output)),
      budgets_(trigger_inputs_.size()) {
    if (instance_id_.empty() || !audio_input_ || !output_) {
        throw std::invalid_argument(
            "Clip generator requires an instance id, raw audio, and output");
    }
}

ClipGeneratorNode::ClipGeneratorNode(
    std::string instance_id,
    ClipGeneratorNodeConfig config,
    std::shared_ptr<DataBlock> audio_input,
    std::shared_ptr<DataBlock> trigger_input,
    std::shared_ptr<DataBlock> output)
    : ClipGeneratorNode(
          std::move(instance_id),
          config,
          std::move(audio_input),
          {legacy_clip_trigger(
              std::move(trigger_input),
              config)},
          std::move(output)) {}

ClipGeneratorNode::~ClipGeneratorNode() { stop(); }
const std::string& ClipGeneratorNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState ClipGeneratorNode::state() const noexcept { return state_; }

void ClipGeneratorNode::prepare() {
    if (audio_input_->descriptor().data_type != kRawAudioDataType ||
        output_->descriptor().data_type != kAudioClipDataType ||
        audio_input_->descriptor().channel_bitmap == 0 ||
        !std::isfinite(
            audio_input_->descriptor().sample_rate_hz) ||
        audio_input_->descriptor().sample_rate_hz <= 0.0 ||
        !std::isfinite(config_.maximum_buffer_seconds) ||
        config_.maximum_buffer_seconds <= 0.0) {
        throw std::invalid_argument(
            "Clip-generator settings or data blocks are invalid");
    }
    if (config_.storage_mode !=
        ClipGeneratorStorageMode::Binary) {
        throw std::invalid_argument(
            "Clip Generator WAV storage is deployment-owned and is not "
            "implemented by the core runtime");
    }
    std::set<std::string> runtime_blocks;
    for (const auto& trigger : trigger_inputs_) {
        if (!trigger.input ||
            trigger.runtime_block_id.empty() ||
            trigger.input->descriptor().id !=
                trigger.runtime_block_id ||
            (!trigger.source_data_type.empty() &&
             trigger.input->descriptor().data_type !=
                 trigger.source_data_type) ||
            !valid_clip_seconds(
                trigger.pre_trigger_seconds) ||
            !valid_clip_seconds(
                trigger.post_trigger_seconds) ||
            trigger.data_budget_kilobytes < 0 ||
            !std::isfinite(
                trigger.budget_period_hours) ||
            trigger.budget_period_hours <= 0.0 ||
            !runtime_blocks.emplace(
                trigger.runtime_block_id).second) {
            throw std::invalid_argument(
                "Clip Generator trigger policies must map one-to-one to "
                "valid runtime blocks");
        }
        if (trigger.enabled &&
            config_.maximum_buffer_seconds <
                trigger.pre_trigger_seconds +
                trigger.post_trigger_seconds) {
            throw std::invalid_argument(
                "Clip Generator audio history is shorter than an enabled "
                "trigger policy");
        }
    }
    state_ = ModuleState::Prepared;
}

void ClipGeneratorNode::start() {
    if (state_ != ModuleState::Prepared &&
        state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Clip generator must be prepared before it starts");
    }
    audio_subscription_ = audio_input_->subscribe(
        [this](const DataUnit& unit) { process_audio(unit); });
    trigger_subscriptions_.clear();
    trigger_subscriptions_.reserve(
        trigger_inputs_.size());
    for (std::size_t index = 0;
         index < trigger_inputs_.size();
         ++index) {
        if (!trigger_inputs_[index].enabled) {
            continue;
        }
        trigger_subscriptions_.push_back(
            trigger_inputs_[index].input->subscribe(
                [this, index](const DataUnit& unit) {
                    process_trigger(index, unit);
                }));
    }
    state_ = ModuleState::Running;
}

void ClipGeneratorNode::stop() {
    audio_subscription_.cancel();
    for (auto& subscription : trigger_subscriptions_) {
        subscription.cancel();
    }
    trigger_subscriptions_.clear();
    {
        std::lock_guard lock(mutex_);
        // A final pass may publish already-complete clips. Requests still
        // waiting for post-trigger audio are deliberately discarded.
        emit_ready();
        pending_.clear();
    }
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void ClipGeneratorNode::flush() {
    std::lock_guard lock(mutex_);
    emit_ready();
}

void ClipGeneratorNode::reset() {
    stop();
    std::lock_guard lock(mutex_);
    audio_.clear();
    pending_.clear();
    budgets_.assign(
        trigger_inputs_.size(),
        BudgetState{});
    latest_sample_end_ = 0;
    next_uid_ = 1;
    state_ = ModuleState::Created;
}

void ClipGeneratorNode::process_audio(const DataUnit& unit) {
    const auto& source = audio_payload(unit);
    std::lock_guard lock(mutex_);
    if (!audio_.empty() &&
        (audio_.back().audio.sample_rate_hz !=
             source.sample_rate_hz ||
         audio_.back().audio.channel_count !=
             source.channel_count)) {
        // Java binds one fixed PamRawDataBlock. A mid-stream format change
        // invalidates both retained samples and outstanding requests.
        audio_.clear();
        pending_.clear();
        latest_sample_end_ = 0;
    }
    audio_.push_back({
        source,
        unit.metadata.discontinuity,
    });
    latest_sample_end_ = std::max(
        latest_sample_end_,
        static_cast<std::int64_t>(
            source.start_sample + source.frame_count()));
    const auto maximum_frames = std::max<std::int64_t>(
        1,
        static_cast<std::int64_t>(
            config_.maximum_buffer_seconds *
            source.sample_rate_hz));
    const auto keep_from =
        latest_sample_end_ - maximum_frames;
    while (audio_.size() > 1 &&
           static_cast<std::int64_t>(
               audio_.front().audio.start_sample +
               audio_.front().audio.frame_count()) <= keep_from) {
        audio_.pop_front();
    }
    emit_ready();
}

void ClipGeneratorNode::process_trigger(
    std::size_t policy_index,
    const DataUnit& unit) {
    std::lock_guard lock(mutex_);
    if (policy_index >= trigger_inputs_.size() ||
        !trigger_inputs_[policy_index].enabled ||
        !should_store(policy_index, unit.metadata)) {
        return;
    }
    pending_.push_back({
        unit.metadata,
        policy_index,
    });
    emit_ready();
}

std::uint32_t ClipGeneratorNode::selected_channel_bitmap(
    const ClipGeneratorTriggerInput& policy,
    std::uint32_t trigger_bitmap) const noexcept {
    const auto available =
        audio_input_->descriptor().channel_bitmap;
    switch (policy.channel_selection) {
    case ClipGeneratorChannelSelection::AllChannels:
        return available;
    case ClipGeneratorChannelSelection::
            DetectionChannelsOnly:
        return trigger_bitmap != 0 &&
                (trigger_bitmap & ~available) == 0
            ? trigger_bitmap
            : 0;
    case ClipGeneratorChannelSelection::
            FirstDetectionChannelOnly: {
        const auto overlap =
            trigger_bitmap & available;
        /*
         * Java shifts by -1 when there is no overlap. That happens to
         * produce bit 31, which getSamples then rejects. Return invalid
         * directly, avoiding language-level undefined behavior.
         */
        return overlap == 0
            ? 0
            : overlap & (0u - overlap);
    }
    }
    return 0;
}

bool ClipGeneratorNode::should_store(
    std::size_t policy_index,
    const DataUnitMetadata& metadata) {
    auto& budget = budgets_.at(policy_index);
    const auto& policy =
        trigger_inputs_.at(policy_index);
    const auto selected = selected_channel_bitmap(
        policy,
        metadata.channel_bitmap);
    if (selected == 0 ||
        metadata.duration_samples >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
        return false;
    }

    const auto time_ms = metadata.time_unix_ms;
    if (!budget.period_initialised ||
        time_ms >= budget.period_end_unix_ms) {
        budget.use_budget =
            policy.use_data_budget;
        budget.period_milliseconds =
            std::max<std::int64_t>(
                1000,
                static_cast<std::int64_t>(
                    policy.budget_period_hours *
                    3600.0 * 1000.0));
        budget.period_start_unix_ms =
            (time_ms / budget.period_milliseconds) *
            budget.period_milliseconds;
        budget.period_end_unix_ms =
            budget.period_start_unix_ms +
            budget.period_milliseconds;
        if (budget.stored_clips > 0) {
            budget.average_clip_size =
                budget.total_stored_size /
                static_cast<std::int64_t>(
                    budget.stored_clips);
        }
        budget.stored_clips = 0;
        budget.total_stored_size = 0;
        budget.budget_size =
            static_cast<std::int64_t>(
                policy.data_budget_kilobytes) *
            1024;
        budget.period_initialised = true;
    }
    if (!budget.use_budget) {
        return true;
    }

    double probability = 1.0;
    if (budget.total_stored_size != 0) {
        if (budget.budget_size <= 0 ||
            budget.total_stored_size >=
                budget.budget_size) {
            probability = 0.0;
        }
        else {
            const auto used_time_fraction =
                static_cast<double>(
                    time_ms -
                    budget.period_start_unix_ms) /
                static_cast<double>(
                    budget.period_milliseconds);
            const auto used_data_fraction =
                static_cast<double>(
                    budget.total_stored_size) /
                static_cast<double>(
                    budget.budget_size);
            probability =
                used_time_fraction /
                used_data_fraction;
        }
    }

    double random_value = 0.0;
    if (budget.total_stored_size != 0) {
        random_value =
            config_.random_unit_interval
            ? config_.random_unit_interval(
                  policy_index)
            : std::generate_canonical<double, 53>(
                  random_engine_);
        if (!std::isfinite(random_value) ||
            random_value < 0.0 ||
            random_value >= 1.0) {
            return false;
        }
    }
    if (!(probability > random_value)) {
        return false;
    }

    const auto rate =
        audio_input_->descriptor().sample_rate_hz;
    const auto estimated_samples =
        static_cast<long double>(
            metadata.duration_samples) +
        static_cast<long double>(rate) *
            static_cast<long double>(
                policy.pre_trigger_seconds +
                policy.post_trigger_seconds);
    const auto estimated_sample_count =
        estimated_samples >=
            static_cast<long double>(
                std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(
              estimated_samples);
    const auto estimated_bytes =
        44.0L +
        2.0L *
            static_cast<long double>(
                estimated_sample_count) *
            static_cast<long double>(
                clip_bitmap_count(selected));
    budget.total_stored_size +=
        estimated_bytes >=
            static_cast<long double>(
                std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(
              estimated_bytes);
    ++budget.stored_clips;
    return true;
}

void ClipGeneratorNode::emit_ready() {
    if (audio_.empty() || pending_.empty()) {
        return;
    }
    const auto rate =
        audio_.front().audio.sample_rate_hz;
    const auto available_start =
        static_cast<std::int64_t>(
            audio_.front().audio.start_sample);

    for (auto request = pending_.begin();
         request != pending_.end();) {
        const auto& policy =
            trigger_inputs_.at(
                request->policy_index);
        if (request->metadata.duration_samples >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
            request = pending_.erase(request);
            continue;
        }
        /*
         * ClipProcess.processClipRequest casts these full expressions to
         * long. Do not replace them with ceil(pre/post * sampleRate).
         */
        const auto requested_start =
            static_cast<std::int64_t>(
                std::max(
                    static_cast<double>(
                        request->metadata.start_sample) -
                        policy.pre_trigger_seconds *
                            rate,
                    0.0));
        const auto requested_end =
            static_cast<std::int64_t>(
                static_cast<double>(
                    request->metadata.start_sample) +
                static_cast<double>(
                    request->metadata.duration_samples) +
                policy.post_trigger_seconds * rate);
        if (selected_channel_bitmap(
                policy,
                request->metadata.channel_bitmap) == 0 ||
            requested_end < requested_start ||
            requested_start < available_start) {
            request = pending_.erase(request);
            continue;
        }
        if (latest_sample_end_ < requested_end) {
            // Continue scanning: a later request with a shorter post period
            // may already be ready (Java's iterator does the same).
            ++request;
            continue;
        }
        (void) emit(*request);
        request = pending_.erase(request);
    }
}

bool ClipGeneratorNode::emit(
    const PendingTrigger& trigger) {
    if (audio_.empty()) {
        return false;
    }
    const auto& policy =
        trigger_inputs_.at(trigger.policy_index);
    const auto rate =
        audio_.front().audio.sample_rate_hz;
    const auto requested_start =
        static_cast<std::int64_t>(
            std::max(
                static_cast<double>(
                    trigger.metadata.start_sample) -
                    policy.pre_trigger_seconds * rate,
                0.0));
    const auto requested_end =
        static_cast<std::int64_t>(
            static_cast<double>(
                trigger.metadata.start_sample) +
            static_cast<double>(
                trigger.metadata.duration_samples) +
            policy.post_trigger_seconds * rate);
    if (requested_end < requested_start) {
        return false;
    }

    const auto selected_bitmap =
        selected_channel_bitmap(
            policy,
            trigger.metadata.channel_bitmap);
    const auto selected_channels =
        clip_channels(selected_bitmap);
    if (selected_channels.empty()) {
        return false;
    }
    const auto frame_count =
        static_cast<std::size_t>(
            requested_end - requested_start);
    GraphAudioClip clip;
    clip.trigger_uid = trigger.metadata.uid;
    clip.trigger_time_unix_ms =
        trigger.metadata.time_unix_ms;
    clip.trigger_start_sample =
        trigger.metadata.start_sample;
    clip.trigger_duration_samples =
        trigger.metadata.duration_samples;
    clip.trigger_channel_bitmap =
        trigger.metadata.channel_bitmap;
    clip.trigger_source_unit_id =
        policy.source_unit_id;
    clip.trigger_source_output_role =
        policy.source_output_role;
    clip.trigger_runtime_block_id =
        policy.runtime_block_id;
    clip.trigger_data_type =
        trigger.metadata.type_id.empty()
        ? policy.source_data_type
        : trigger.metadata.type_id;
    clip.clip_start_time_unix_ms =
        trigger.metadata.time_unix_ms -
        static_cast<std::int64_t>(
            policy.pre_trigger_seconds * 1000.0);
    clip.clip_start_sample = requested_start;
    clip.sample_rate_hz = rate;
    clip.channel_count =
        static_cast<std::uint32_t>(
            selected_channels.size());
    clip.selected_channel_bitmap =
        selected_bitmap;
    clip.clip_prefix = policy.clip_prefix;
    clip.incomplete = false;
    clip.interleaved_pcm.assign(
        frame_count * selected_channels.size(),
        0.0);
    std::vector<bool> copied(frame_count, false);
    const auto available_bitmap =
        audio_input_->descriptor().channel_bitmap;

    for (const auto& buffered : audio_) {
        const auto& chunk = buffered.audio;
        const auto chunk_start =
            static_cast<std::int64_t>(
                chunk.start_sample);
        const auto chunk_end =
            chunk_start +
            static_cast<std::int64_t>(
                chunk.frame_count());
        if (buffered.discontinuity &&
            chunk_start > requested_start &&
            chunk_start < requested_end) {
            return false;
        }
        const auto copy_start =
            std::max(requested_start, chunk_start);
        const auto copy_end =
            std::min(requested_end, chunk_end);
        if (copy_start >= copy_end) {
            continue;
        }
        std::vector<std::size_t> source_slots;
        source_slots.reserve(
            selected_channels.size());
        for (const auto channel : selected_channels) {
            const auto slot = clip_channel_slot(
                available_bitmap,
                chunk.channel_count,
                channel);
            if (!slot) {
                return false;
            }
            source_slots.push_back(*slot);
        }
        for (auto sample = copy_start;
             sample < copy_end;
             ++sample) {
            const auto target_frame =
                static_cast<std::size_t>(
                    sample - requested_start);
            const auto source_frame =
                static_cast<std::size_t>(
                    sample - chunk_start);
            copied[target_frame] = true;
            for (std::size_t channel = 0;
                 channel < source_slots.size();
                 ++channel) {
                clip.interleaved_pcm[
                    target_frame *
                        source_slots.size() +
                    channel] =
                    chunk.sample(
                        source_frame,
                        source_slots[channel]);
            }
        }
    }
    if (std::find(
            copied.begin(),
            copied.end(),
            false) != copied.end()) {
        return false;
    }

    auto metadata = trigger.metadata;
    metadata.type_id.clear();
    metadata.source_block_id.clear();
    metadata.uid = next_uid_++;
    metadata.sequence = metadata.uid;
    metadata.time_unix_ms =
        clip.clip_start_time_unix_ms;
    metadata.start_sample = requested_start;
    metadata.duration_samples = frame_count;
    metadata.channel_bitmap = selected_bitmap;
    metadata.discontinuity = false;
    output_->publish(
        make_data_unit(
            std::move(metadata),
            std::move(clip)));
    return true;
}

OperatorInputNode::OperatorInputNode(
    std::string instance_id,
    std::string default_category,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      default_category_(std::move(default_category)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !output_) {
        throw std::invalid_argument(
            "Operator input requires an instance id and output block");
    }
}

const std::string& OperatorInputNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState OperatorInputNode::state() const noexcept { return state_; }

void OperatorInputNode::prepare() {
    if (output_->descriptor().data_type != kOperatorEventDataType) {
        throw std::invalid_argument(
            "Operator input output block has the wrong type");
    }
    state_ = ModuleState::Prepared;
}

void OperatorInputNode::start() {
    if (state_ != ModuleState::Prepared &&
        state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Operator input must be prepared before it starts");
    }
    state_ = ModuleState::Running;
}

void OperatorInputNode::stop() {
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void OperatorInputNode::reset() {
    next_uid_ = 1;
    state_ = ModuleState::Created;
}

void OperatorInputNode::publish(
    GraphOperatorEvent event,
    std::int64_t time_unix_ms,
    std::int64_t start_sample) {
    if (state_ != ModuleState::Running) {
        throw std::logic_error(
            "Operator input module is not running");
    }
    if (event.category.empty()) {
        event.category = default_category_;
    }
    DataUnitMetadata metadata;
    metadata.uid = next_uid_++;
    metadata.sequence = metadata.uid;
    metadata.time_unix_ms =
        time_unix_ms == 0 ? now_unix_ms() : time_unix_ms;
    metadata.start_sample = start_sample;
    output_->publish(
        make_data_unit(std::move(metadata), std::move(event)));
}

StorageHealthNode::StorageHealthNode(
    std::string instance_id,
    std::filesystem::path path,
    double warning_free_percent,
    double interval_seconds,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      path_(std::move(path)),
      warning_free_percent_(warning_free_percent),
      interval_seconds_(interval_seconds),
      output_(std::move(output)) {
    if (instance_id_.empty() || path_.empty() || !output_) {
        throw std::invalid_argument(
            "Storage health requires an instance id, path, and output");
    }
}

StorageHealthNode::~StorageHealthNode() { stop(); }

const std::string& StorageHealthNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState StorageHealthNode::state() const noexcept { return state_; }

void StorageHealthNode::prepare() {
    if (warning_free_percent_ < 0.0 ||
        warning_free_percent_ > 100.0 ||
        interval_seconds_ <= 0.0 ||
        output_->descriptor().data_type != kStorageHealthDataType) {
        throw std::invalid_argument(
            "Storage-health settings are invalid");
    }
    state_ = ModuleState::Prepared;
}

void StorageHealthNode::start() {
    if (state_ != ModuleState::Prepared &&
        state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Storage health must be prepared before it starts");
    }
    {
        std::lock_guard lock(worker_mutex_);
        stop_requested_ = false;
    }
    state_ = ModuleState::Running;
    publish();
    worker_ = std::thread([this] {
        std::unique_lock lock(worker_mutex_);
        while (!worker_condition_.wait_for(
            lock,
            std::chrono::duration<double>(interval_seconds_),
            [this] { return stop_requested_; })) {
            lock.unlock();
            publish();
            lock.lock();
        }
    });
}

void StorageHealthNode::stop() {
    {
        std::lock_guard lock(worker_mutex_);
        stop_requested_ = true;
    }
    worker_condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void StorageHealthNode::reset() {
    stop();
    next_uid_ = 1;
    state_ = ModuleState::Created;
}

void StorageHealthNode::publish() {
    std::error_code error;
    const auto space = std::filesystem::space(path_, error);
    GraphStorageHealth result;
    result.path = path_.string();
    result.available = !error;
    if (!error) {
        result.capacity_bytes = space.capacity;
        result.free_bytes = space.free;
        result.available_bytes = space.available;
        result.available_percent = space.capacity == 0
            ? 0.0
            : static_cast<double>(space.available) /
                static_cast<double>(space.capacity) * 100.0;
        result.status = result.available_percent <
            warning_free_percent_
            ? "warning"
            : "healthy";
    }
    else {
        result.status = error.message();
    }
    DataUnitMetadata metadata;
    metadata.uid = next_uid_++;
    metadata.sequence = metadata.uid;
    metadata.time_unix_ms = now_unix_ms();
    output_->publish(
        make_data_unit(std::move(metadata), std::move(result)));
}

} // namespace pamguard::core
