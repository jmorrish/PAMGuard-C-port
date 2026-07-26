#include "pamguard/core/FftDetectorNodes.h"

#include <algorithm>
#include <any>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <utility>

#include "pamguard/core/SignalNodes.h"
#include "pamguard/dsp/SpectrogramEngine.h"

namespace pamguard::core {

namespace {

const dsp::SpectrogramFrame& fft_payload(const DataUnit& unit) {
    const auto* frame = std::any_cast<dsp::SpectrogramFrame>(&unit.payload);
    if (frame == nullptr) {
        throw std::invalid_argument("FFT monitor input payload is not an FFT frame");
    }
    return *frame;
}

std::vector<double> packed_magnitude_squared(
    const dsp::ComplexSpectrum& bins) {
    if (bins.size() < 2) {
        return {};
    }
    const auto fft_length = (bins.size() - 1) * 2;
    std::vector<double> magnitude(fft_length / 2, 0.0);
    magnitude[0] =
        bins[0].real() * bins[0].real() +
        bins[fft_length / 2].real() * bins[fft_length / 2].real();
    for (std::size_t bin = 1; bin < magnitude.size(); ++bin) {
        magnitude[bin] = std::norm(bins[bin]);
    }
    return magnitude;
}

void require_types(
    const DataBlock& input,
    const DataBlock& output,
    const char* output_type,
    const char* node_name) {
    if (input.descriptor().data_type != kFftDataType ||
        output.descriptor().data_type != output_type) {
        throw std::invalid_argument(
            std::string(node_name) + " block types are incompatible");
    }
}

} // namespace

NoiseBandNode::NoiseBandNode(
    std::string instance_id,
    double sample_rate_hz,
    NoiseBandNodeConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      sample_rate_hz_(sample_rate_hz),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument(
            "Noise band monitor requires an instance id and data blocks");
    }
}

NoiseBandNode::~NoiseBandNode() { stop(); }
const std::string& NoiseBandNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState NoiseBandNode::state() const noexcept { return state_; }

void NoiseBandNode::prepare() {
    if (input_->descriptor().data_type != kRawAudioDataType ||
        output_->descriptor().data_type != kNoiseBandDataType) {
        throw std::invalid_argument(
            "Noise band monitor block types are incompatible");
    }
    monitors_.clear();
    for (std::size_t channel = 0; channel < 32; ++channel) {
        if ((input_->descriptor().channel_bitmap &
             config_.channel_bitmap &
             (std::uint32_t{1} << channel)) == 0) {
            continue;
        }
        auto monitor = std::make_unique<detectors::NoiseBandMonitor>(
            sample_rate_hz_,
            config_.monitor);
        if (monitor->valid()) {
            monitors_.emplace(channel, std::move(monitor));
        }
    }
    if (monitors_.empty()) {
        throw std::invalid_argument(
            "Noise band monitor has no valid source channels");
    }
    next_uid_ = 1;
    state_ = ModuleState::Prepared;
}

void NoiseBandNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Noise band monitor must be prepared before it starts");
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void NoiseBandNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void NoiseBandNode::reset() {
    stop();
    monitors_.clear();
    state_ = ModuleState::Created;
}

void NoiseBandNode::process(const DataUnit& unit) {
    const auto* audio = std::any_cast<AudioChunk>(&unit.payload);
    if (audio == nullptr) {
        throw std::invalid_argument(
            "Noise band input payload is not raw audio");
    }
    std::vector<double> samples(audio->frame_count());
    for (auto& [channel, monitor] : monitors_) {
        if (channel >= audio->channel_count) {
            continue;
        }
        for (std::size_t frame = 0; frame < audio->frame_count(); ++frame) {
            samples[frame] = audio->sample(frame, channel);
        }
        auto levels = monitor->process(
            samples,
            static_cast<std::int64_t>(audio->start_sample),
            audio->time_unix_ms);
        if (!levels.has_value()) {
            continue;
        }
        const auto& calibration =
            config_.calibration_db_offset_by_channel.empty()
            ? input_->descriptor()
                  .calibration_db_offset_by_channel
            : config_.calibration_db_offset_by_channel;
        const double offset =
            channel < calibration.size()
            ? calibration[channel]
            : 0.0;
        const auto to_db = [offset](double amplitude) {
            const auto value =
                20.0 * std::log10(amplitude) + offset;
            return std::isfinite(value) ? value : 0.0;
        };
        NoiseBandMeasurement measurement;
        measurement.channel = channel;
        measurement.bands = monitor->bands();
        measurement.end_sample = levels->end_sample;
        measurement.time_unix_ms = levels->time_unix_ms;
        measurement.rms_db.reserve(levels->rms.size());
        measurement.peak_db.reserve(levels->peak.size());
        std::transform(
            levels->rms.begin(),
            levels->rms.end(),
            std::back_inserter(measurement.rms_db),
            to_db);
        std::transform(
            levels->peak.begin(),
            levels->peak.end(),
            std::back_inserter(measurement.peak_db),
            to_db);
        DataUnitMetadata metadata;
        metadata.uid = next_uid_;
        metadata.sequence = next_uid_++;
        metadata.time_unix_ms = measurement.time_unix_ms;
        metadata.start_sample = measurement.end_sample;
        metadata.channel_bitmap = channel < 32
            ? std::uint32_t{1} << channel
            : 0;
        metadata.clock_domain_id = unit.metadata.clock_domain_id;
        output_->publish(make_data_unit(
            std::move(metadata),
            std::move(measurement)));
    }
}

FftNoiseNode::FftNoiseNode(
    std::string instance_id,
    double sample_rate_hz,
    std::size_t fft_length,
    std::size_t fft_hop,
    detectors::FftNoiseConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      sample_rate_hz_(sample_rate_hz),
      fft_length_(fft_length),
      fft_hop_(fft_hop),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument(
            "FFT noise monitor requires an instance id and data blocks");
    }
}

FftNoiseNode::~FftNoiseNode() { stop(); }
const std::string& FftNoiseNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState FftNoiseNode::state() const noexcept { return state_; }

void FftNoiseNode::prepare() {
    require_types(*input_, *output_, kFftNoiseDataType, "FFT noise monitor");
    monitor_ = std::make_unique<detectors::FftNoiseMonitor>(
        sample_rate_hz_,
        fft_length_,
        fft_hop_,
        config_);
    next_uid_ = 1;
    state_ = ModuleState::Prepared;
}

void FftNoiseNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "FFT noise monitor must be prepared before it starts");
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void FftNoiseNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void FftNoiseNode::reset() {
    stop();
    monitor_.reset();
    state_ = ModuleState::Created;
}

void FftNoiseNode::process(const DataUnit& unit) {
    const auto& frame = fft_payload(unit);
    auto periods = monitor_->process_frame(
        frame.channel,
        frame.time_unix_ms,
        frame.start_sample,
        packed_magnitude_squared(frame.bins));
    for (auto& period : periods) {
        const auto& calibration =
            input_->descriptor().calibration_db_offset_by_channel;
        if (period.channel < calibration.size()) {
            const double offset = calibration[period.channel];
            const auto to_db =
                [&](double squared_amplitude) {
                    // AcquisitionProcess.fftBandAmplitude2dB(...,
                    // isSquared=true): sqrt, divide by fftLength,
                    // multiply sqrt(2), then rawAmplitude2dB. The
                    // descriptor offset already includes acquisition
                    // calibration and upstream-process gain removal.
                    double amplitude =
                        std::sqrt(squared_amplitude) /
                        static_cast<double>(fft_length_) *
                        std::sqrt(2.0);
                    const double value =
                        20.0 * std::log10(amplitude) + offset;
                    return std::isfinite(value) ? value : 0.0;
                };
            for (auto& band : period.bands) {
                band.mean = to_db(band.mean);
                band.median = to_db(band.median);
                band.low_95 = to_db(band.low_95);
                band.high_95 = to_db(band.high_95);
                band.minimum = to_db(band.minimum);
                band.maximum = to_db(band.maximum);
            }
        }
        DataUnitMetadata metadata;
        metadata.uid = next_uid_;
        metadata.sequence = next_uid_++;
        metadata.time_unix_ms = period.time_unix_ms;
        metadata.start_sample = period.end_sample;
        metadata.channel_bitmap = period.channel < 32
            ? std::uint32_t{1} << period.channel
            : 0;
        metadata.clock_domain_id = unit.metadata.clock_domain_id;
        output_->publish(make_data_unit(
            std::move(metadata),
            std::move(period)));
    }
}

LtsaNode::LtsaNode(
    std::string instance_id,
    detectors::LtsaConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument(
            "LTSA requires an instance id and data blocks");
    }
}

LtsaNode::~LtsaNode() { stop(); }
const std::string& LtsaNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState LtsaNode::state() const noexcept { return state_; }

void LtsaNode::prepare() {
    require_types(*input_, *output_, kLtsaDataType, "LTSA");
    monitors_.clear();
    next_uid_ = 1;
    state_ = ModuleState::Prepared;
}

void LtsaNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error("LTSA must be prepared before it starts");
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void LtsaNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        flush();
        state_ = ModuleState::Stopped;
    }
}

void LtsaNode::flush() {
    for (auto& [channel, monitor] : monitors_) {
        auto interval = monitor.flush();
        if (interval.has_value()) {
            publish(channel, std::move(*interval));
        }
    }
}

void LtsaNode::reset() {
    stop();
    monitors_.clear();
    state_ = ModuleState::Created;
}

void LtsaNode::process(const DataUnit& unit) {
    const auto& frame = fft_payload(unit);
    if (frame.channel >= 32 ||
        (config_.channel_bitmap &
         (std::uint32_t{1} << frame.channel)) == 0) {
        return;
    }
    auto found = monitors_.find(frame.channel);
    if (found == monitors_.end()) {
        found = monitors_.try_emplace(frame.channel, config_).first;
    }
    auto interval = found->second.process_frame(
        frame.time_unix_ms,
        frame.start_sample,
        static_cast<std::int64_t>(unit.metadata.duration_samples),
        packed_magnitude_squared(frame.bins));
    if (interval.has_value()) {
        publish(frame.channel, std::move(*interval));
    }
}

void LtsaNode::publish(
    std::size_t channel,
    detectors::LtsaInterval interval) {
    DataUnitMetadata metadata;
    metadata.uid = next_uid_;
    metadata.sequence = next_uid_++;
    metadata.time_unix_ms = interval.start_time_ms;
    metadata.start_sample = interval.start_sample;
    metadata.duration_samples = static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, interval.duration_samples));
    metadata.channel_bitmap = channel < 32
        ? std::uint32_t{1} << channel
        : 0;
    output_->publish(make_data_unit(
        std::move(metadata),
        LtsaChannelInterval{channel, std::move(interval)}));
}

IshmaelNode::IshmaelNode(
    std::string instance_id,
    double sample_rate_hz,
    std::size_t fft_hop,
    detectors::IshmaelEnergySumConfig config,
    std::shared_ptr<DataBlock> input,
    IshmaelNodeOutputs outputs)
    : instance_id_(std::move(instance_id)),
      sample_rate_hz_(sample_rate_hz),
      fft_hop_(fft_hop),
      config_(std::move(config)),
      input_(std::move(input)),
      outputs_(std::move(outputs)) {
    if (instance_id_.empty() || !input_ ||
        !outputs_.detection_function || !outputs_.detections) {
        throw std::invalid_argument(
            "Ishmael detector requires an instance id and data blocks");
    }
}

IshmaelNode::~IshmaelNode() { stop(); }
const std::string& IshmaelNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState IshmaelNode::state() const noexcept { return state_; }

void IshmaelNode::prepare() {
    require_types(
        *input_,
        *outputs_.detection_function,
        kIshmaelFunctionDataType,
        "Ishmael detector");
    if (outputs_.detections->descriptor().data_type !=
        kIshmaelDetectionDataType) {
        throw std::invalid_argument(
            "Ishmael detector output block type is incompatible");
    }
    energy_ = std::make_unique<detectors::IshmaelEnergySum>(
        sample_rate_hz_,
        config_);
    picker_ = std::make_unique<detectors::IshmaelPeakPicker>(
        sample_rate_hz_,
        fft_hop_,
        config_);
    next_uid_ = 1;
    state_ = ModuleState::Prepared;
}

void IshmaelNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Ishmael detector must be prepared before it starts");
    }
    // IshPeakProcess.prepareForRun creates fresh PerChannelInfo for every
    // Java run. EnergySumProcess itself is not renewed, so its shared noise
    // floor and write-once smoothing state deliberately survive stop/start.
    if (state_ == ModuleState::Stopped) {
        picker_ = std::make_unique<detectors::IshmaelPeakPicker>(
            sample_rate_hz_,
            fft_hop_,
            config_);
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void IshmaelNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void IshmaelNode::reset() {
    stop();
    energy_.reset();
    picker_.reset();
    state_ = ModuleState::Created;
}

void IshmaelNode::process(const DataUnit& unit) {
    const auto& frame = fft_payload(unit);
    const auto channel_bit = frame.channel < 32
        ? std::uint32_t{1} << frame.channel
        : 0;
    if ((outputs_.detection_function->descriptor().channel_bitmap &
         channel_bit) == 0) {
        return;
    }
    auto sample = energy_->process_frame(
        packed_magnitude_squared(frame.bins));
    DataUnitMetadata function_metadata = unit.metadata;
    function_metadata.type_id.clear();
    function_metadata.source_block_id.clear();
    outputs_.detection_function->publish(make_data_unit(
        std::move(function_metadata),
        sample));

    // Java publishes the function for every selected source channel, then
    // drives IshPeakProcess only from the first channel in each group.
    if ((outputs_.detections->descriptor().channel_bitmap &
         channel_bit) == 0) {
        return;
    }
    auto detection = picker_->process(
        frame.channel,
        frame.start_sample,
        sample.det_value);
    if (!detection.has_value()) {
        return;
    }
    DataUnitMetadata metadata;
    metadata.uid = next_uid_;
    metadata.sequence = next_uid_++;
    detection->start_time_ms =
        unit.metadata.time_unix_ms +
        static_cast<std::int64_t>(std::llround(
            static_cast<double>(
                detection->start_sample -
                frame.start_sample) *
            1000.0 / sample_rate_hz_));
    metadata.time_unix_ms = detection->start_time_ms;
    metadata.start_sample = detection->start_sample;
    metadata.duration_samples = static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, detection->duration_samples));
    metadata.channel_bitmap = detection->channel < 32
        ? std::uint32_t{1} << detection->channel
        : 0;
    metadata.clock_domain_id = unit.metadata.clock_domain_id;
    outputs_.detections->publish(make_data_unit(
        std::move(metadata),
        std::move(*detection)));
}

SpectrogramNoiseNode::SpectrogramNoiseNode(
    std::string instance_id,
    detectors::SpectrogramNoiseConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument(
            "Spectrogram noise reduction requires an instance id and data blocks");
    }
}

SpectrogramNoiseNode::~SpectrogramNoiseNode() { stop(); }

const std::string& SpectrogramNoiseNode::instance_id() const noexcept {
    return instance_id_;
}

ModuleState SpectrogramNoiseNode::state() const noexcept {
    return state_;
}

void SpectrogramNoiseNode::prepare() {
    require_types(
        *input_,
        *output_,
        kFftDataType,
        "Spectrogram noise reduction");
    reducer_ =
        std::make_unique<detectors::SpectrogramNoiseReducer>(config_);
    state_ = ModuleState::Prepared;
}

void SpectrogramNoiseNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Spectrogram noise reduction must be prepared before it starts");
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void SpectrogramNoiseNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void SpectrogramNoiseNode::reset() {
    stop();
    reducer_.reset();
    state_ = ModuleState::Created;
}

void SpectrogramNoiseNode::process(const DataUnit& unit) {
    auto frame = fft_payload(unit);
    if (frame.bins.size() >= 2 && reducer_->active()) {
        const auto half = frame.bins.size() - 1;
        std::vector<std::complex<double>> packed(half);
        packed[0] = {
            frame.bins[0].real(),
            frame.bins[half].real(),
        };
        for (std::size_t bin = 1; bin < half; ++bin) {
            packed[bin] = frame.bins[bin];
        }
        const auto reduced =
            reducer_->process(frame.channel, packed);
        frame.bins[0] = {reduced[0].real(), 0.0};
        frame.bins[half] = {reduced[0].imag(), 0.0};
        for (std::size_t bin = 1; bin < half; ++bin) {
            frame.bins[bin] = reduced[bin];
        }
    }
    auto metadata = unit.metadata;
    metadata.type_id.clear();
    metadata.source_block_id.clear();
    output_->publish(make_data_unit(
        std::move(metadata),
        std::move(frame)));
}

WhistleMoanNode::WhistleMoanNode(
    std::string instance_id,
    WhistleMoanNodeConfig config,
    std::shared_ptr<DataBlock> input,
    WhistleMoanNodeOutputs outputs)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      outputs_(std::move(outputs)) {
    if (instance_id_.empty() || !input_ ||
        !outputs_.contours) {
        throw std::invalid_argument(
            "Whistles & Moans requires an instance id and data blocks");
    }
}

WhistleMoanNode::~WhistleMoanNode() { stop(); }
const std::string& WhistleMoanNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState WhistleMoanNode::state() const noexcept { return state_; }

void WhistleMoanNode::prepare() {
    if (input_->descriptor().data_type != kFftDataType ||
        outputs_.contours->descriptor().data_type !=
            kWhistleContourDataType) {
        throw std::invalid_argument(
            "Whistles & Moans block types are incompatible");
    }
    region_trackers_.clear();
    group_bitmap_by_first_channel_.clear();
    std::vector<std::size_t> channels;
    const auto selected =
        config_.channel_bitmap &
        input_->descriptor().channel_bitmap;
    for (std::size_t channel = 0; channel < 32; ++channel) {
        if ((selected & (std::uint32_t{1} << channel)) != 0) {
            channels.push_back(channel);
        }
    }
    if (channels.empty()) {
        throw std::invalid_argument(
            "Whistles & Moans has no selected FFT channels");
    }
    std::unordered_map<int, std::size_t> first_by_group;
    std::unordered_map<int, std::uint32_t> bitmap_by_group;
    for (const auto channel : channels) {
        int group = 0;
        if (config_.grouping == WhistleGroupingType::Singles) {
            group = static_cast<int>(channel);
        }
        else if (config_.grouping == WhistleGroupingType::User) {
            if (channel >= config_.channel_groups.size()) {
                throw std::invalid_argument(
                    "Whistles & Moans user grouping lacks a selected-channel assignment");
            }
            group = config_.channel_groups[channel];
        }
        first_by_group.try_emplace(group, channel);
        bitmap_by_group[group] |=
            std::uint32_t{1} << channel;
    }
    for (const auto& [group, channel] : first_by_group) {
        group_bitmap_by_first_channel_.emplace(
            channel,
            bitmap_by_group.at(group));
    }
    next_uid_ = 1;
    state_ = ModuleState::Prepared;
}

void WhistleMoanNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Whistles & Moans must be prepared before it starts");
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void WhistleMoanNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        flush();
        state_ = ModuleState::Stopped;
    }
}

void WhistleMoanNode::flush() {
    for (auto& [_, tracker] : region_trackers_) {
        publish_contours(tracker->flush(), nullptr);
    }
}

void WhistleMoanNode::reset() {
    stop();
    region_trackers_.clear();
    group_bitmap_by_first_channel_.clear();
    state_ = ModuleState::Created;
}

void WhistleMoanNode::process(const DataUnit& unit) {
    auto frame = fft_payload(unit);
    if (frame.bins.size() < 2) {
        return;
    }
    const auto half = frame.bins.size() - 1;
    const auto magnitude = packed_magnitude_squared(frame.bins);
    if (!group_bitmap_by_first_channel_.contains(
            frame.channel)) {
        return;
    }
    auto region_tracker =
        region_trackers_.find(frame.channel);
    if (region_tracker == region_trackers_.end()) {
        auto region_config = config_.contours;
        region_config.channel = frame.channel;
        region_config.slice_height = magnitude.size();
        region_config.sample_rate_hz =
            static_cast<std::uint32_t>(std::llround(
                input_->descriptor().sample_rate_hz));
        region_tracker = region_trackers_.emplace(
            frame.channel,
            std::make_unique<
                detectors::ConnectedRegionTracker>(
                std::move(region_config))).first;
    }
    std::vector<bool> active(magnitude.size(), false);
    const auto& region_config = region_tracker->second->config();
    const auto [minimum_bin, maximum_bin] =
        detectors::whistle_frequency_bin_range(
            region_config.min_frequency_hz,
            region_config.max_frequency_hz,
            input_->descriptor().sample_rate_hz,
            half * 2,
            magnitude.size());
    for (std::size_t bin = minimum_bin; bin < maximum_bin; ++bin) {
        active[bin] = magnitude[bin] > 0.0;
    }
    publish_contours(
        region_tracker->second->process_slice(
            frame.fft_slice,
            frame.start_sample,
            frame.time_unix_ms,
            active,
            magnitude),
        &unit.metadata);
}

void WhistleMoanNode::publish_contours(
    std::vector<detectors::ConnectedRegionResult> contours,
    const DataUnitMetadata* source_metadata) {
    for (auto& contour : contours) {
        DataUnitMetadata metadata;
        if (source_metadata != nullptr) {
            metadata.clock_domain_id =
                source_metadata->clock_domain_id;
        }
        metadata.uid = next_uid_;
        metadata.sequence = next_uid_++;
        metadata.time_unix_ms = contour.time_ms;
        metadata.start_sample = contour.start_sample;
        metadata.duration_samples = static_cast<std::uint64_t>(
            std::max<std::int64_t>(0, contour.duration_samples));
        const auto group =
            group_bitmap_by_first_channel_.find(
                contour.channel);
        metadata.channel_bitmap =
            group !=
                group_bitmap_by_first_channel_.end()
            ? group->second
            : 0;
        outputs_.contours->publish(make_data_unit(
            std::move(metadata),
            std::move(contour)));
    }
}

SgramCorrNode::SgramCorrNode(
    std::string instance_id,
    double sample_rate_hz,
    std::size_t fft_length,
    std::size_t fft_hop,
    detectors::SgramCorrConfig config,
    std::shared_ptr<DataBlock> input,
    IshmaelNodeOutputs outputs)
    : instance_id_(std::move(instance_id)),
      sample_rate_hz_(sample_rate_hz),
      fft_length_(fft_length),
      fft_hop_(fft_hop),
      config_(std::move(config)),
      input_(std::move(input)),
      outputs_(std::move(outputs)) {
    if (instance_id_.empty() || !input_ ||
        !outputs_.detection_function || !outputs_.detections) {
        throw std::invalid_argument(
            "Ishmael spectrogram correlation requires data blocks");
    }
}

SgramCorrNode::~SgramCorrNode() { stop(); }
const std::string& SgramCorrNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState SgramCorrNode::state() const noexcept { return state_; }

void SgramCorrNode::prepare() {
    require_types(
        *input_,
        *outputs_.detection_function,
        kIshmaelFunctionDataType,
        "Ishmael spectrogram correlation");
    if (outputs_.detections->descriptor().data_type !=
        kIshmaelDetectionDataType) {
        throw std::invalid_argument(
            "Ishmael spectrogram correlation output type is incompatible");
    }
    detector_ = std::make_unique<detectors::SgramCorrDetector>(
        sample_rate_hz_,
        fft_length_,
        fft_hop_,
        config_);
    if (detector_->kernel_length() == 0) {
        throw std::invalid_argument(
            "Ishmael spectrogram correlation kernel is empty");
    }
    detectors::IshmaelEnergySumConfig picker_config;
    picker_config.thresh = config_.thresh;
    picker_config.min_time_s = config_.min_time_s;
    picker_config.max_time_s = config_.max_time_s;
    picker_config.refractory_time_s = config_.refractory_time_s;
    picker_config.f0 = detector_->min_frequency_hz();
    picker_config.f1 = detector_->max_frequency_hz();
    picker_ = std::make_unique<detectors::IshmaelPeakPicker>(
        sample_rate_hz_,
        fft_hop_,
        picker_config);
    next_uid_ = 1;
    state_ = ModuleState::Prepared;
}

void SgramCorrNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Ishmael spectrogram correlation must be prepared before start");
    }
    // SgramCorrProcess renews its circular per-channel buffers when a run is
    // prepared, and the shared peak process also starts with fresh channels.
    if (state_ == ModuleState::Stopped) {
        detector_ = std::make_unique<detectors::SgramCorrDetector>(
            sample_rate_hz_,
            fft_length_,
            fft_hop_,
            config_);
        detectors::IshmaelEnergySumConfig picker_config;
        picker_config.thresh = config_.thresh;
        picker_config.min_time_s = config_.min_time_s;
        picker_config.max_time_s = config_.max_time_s;
        picker_config.refractory_time_s =
            config_.refractory_time_s;
        picker_config.f0 = detector_->min_frequency_hz();
        picker_config.f1 = detector_->max_frequency_hz();
        picker_ = std::make_unique<detectors::IshmaelPeakPicker>(
            sample_rate_hz_,
            fft_hop_,
            picker_config);
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void SgramCorrNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void SgramCorrNode::reset() {
    stop();
    detector_.reset();
    picker_.reset();
    state_ = ModuleState::Created;
}

void SgramCorrNode::process(const DataUnit& unit) {
    const auto& frame = fft_payload(unit);
    const auto channel_bit = frame.channel < 32
        ? std::uint32_t{1} << frame.channel
        : 0;
    if ((outputs_.detection_function->descriptor().channel_bitmap &
         channel_bit) == 0) {
        return;
    }
    const auto function = detector_->process_frame(
        frame.channel,
        packed_magnitude_squared(frame.bins));
    if (!function.has_value()) {
        return;
    }
    detectors::IshmaelDetSample sample;
    sample.det_value = *function;
    sample.raw_value = *function;
    auto function_metadata = unit.metadata;
    function_metadata.type_id.clear();
    function_metadata.source_block_id.clear();
    outputs_.detection_function->publish(make_data_unit(
        std::move(function_metadata),
        sample));
    if ((outputs_.detections->descriptor().channel_bitmap &
         channel_bit) == 0) {
        return;
    }
    auto detection = picker_->process(
        frame.channel,
        frame.start_sample,
        *function);
    if (!detection.has_value()) {
        return;
    }
    DataUnitMetadata metadata;
    metadata.uid = next_uid_;
    metadata.sequence = next_uid_++;
    detection->start_time_ms =
        unit.metadata.time_unix_ms +
        static_cast<std::int64_t>(std::llround(
            static_cast<double>(
                detection->start_sample -
                frame.start_sample) *
            1000.0 / sample_rate_hz_));
    metadata.time_unix_ms = detection->start_time_ms;
    metadata.start_sample = detection->start_sample;
    metadata.duration_samples = static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, detection->duration_samples));
    metadata.channel_bitmap = detection->channel < 32
        ? std::uint32_t{1} << detection->channel
        : 0;
    metadata.clock_domain_id = unit.metadata.clock_domain_id;
    outputs_.detections->publish(make_data_unit(
        std::move(metadata),
        std::move(*detection)));
}

MatchFiltNode::MatchFiltNode(
    std::string instance_id,
    double sample_rate_hz,
    detectors::MatchFiltConfig config,
    std::shared_ptr<DataBlock> input,
    IshmaelNodeOutputs outputs)
    : instance_id_(std::move(instance_id)),
      sample_rate_hz_(sample_rate_hz),
      config_(std::move(config)),
      input_(std::move(input)),
      outputs_(std::move(outputs)) {
    if (instance_id_.empty() || !input_ ||
        !outputs_.detection_function || !outputs_.detections) {
        throw std::invalid_argument(
            "Ishmael matched filter requires data blocks");
    }
}

MatchFiltNode::~MatchFiltNode() { stop(); }
const std::string& MatchFiltNode::instance_id() const noexcept {
    return instance_id_;
}
ModuleState MatchFiltNode::state() const noexcept { return state_; }

void MatchFiltNode::prepare() {
    if (input_->descriptor().data_type != kRawAudioDataType ||
        outputs_.detection_function->descriptor().data_type !=
            kIshmaelFunctionDataType ||
        outputs_.detections->descriptor().data_type !=
            kIshmaelDetectionDataType) {
        throw std::invalid_argument(
            "Ishmael matched filter block types are incompatible");
    }
    detector_ = std::make_unique<detectors::MatchFiltDetector>(
        sample_rate_hz_,
        config_);
    if (!detector_->valid()) {
        throw std::invalid_argument(
            "Ishmael matched filter kernel is empty");
    }
    detectors::IshmaelEnergySumConfig picker_config;
    picker_config.thresh = config_.thresh;
    picker_config.min_time_s = config_.min_time_s;
    picker_config.max_time_s = config_.max_time_s;
    picker_config.refractory_time_s = config_.refractory_time_s;
    picker_config.f0 = 0.0;
    picker_config.f1 = sample_rate_hz_ / 2.0;
    picker_ = std::make_unique<detectors::IshmaelPeakPicker>(
        sample_rate_hz_,
        1,
        picker_config);
    next_uid_ = 1;
    state_ = ModuleState::Prepared;
}

void MatchFiltNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error(
            "Ishmael matched filter must be prepared before start");
    }
    // MatchFiltProcess2.prepareMyParams creates fresh ChannelDetector objects
    // and reloads the kernel for each Java run. The peak process is renewed
    // at the same boundary.
    if (state_ == ModuleState::Stopped) {
        detector_ =
            std::make_unique<detectors::MatchFiltDetector>(
                sample_rate_hz_,
                config_);
        detectors::IshmaelEnergySumConfig picker_config;
        picker_config.thresh = config_.thresh;
        picker_config.min_time_s = config_.min_time_s;
        picker_config.max_time_s = config_.max_time_s;
        picker_config.refractory_time_s =
            config_.refractory_time_s;
        picker_config.f0 = 0.0;
        picker_config.f1 = sample_rate_hz_ / 2.0;
        picker_ = std::make_unique<detectors::IshmaelPeakPicker>(
            sample_rate_hz_,
            1,
            picker_config);
    }
    subscription_ = input_->subscribe(
        [this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void MatchFiltNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void MatchFiltNode::reset() {
    stop();
    detector_.reset();
    picker_.reset();
    state_ = ModuleState::Created;
}

void MatchFiltNode::process(const DataUnit& unit) {
    const auto* audio = std::any_cast<AudioChunk>(&unit.payload);
    if (audio == nullptr) {
        throw std::invalid_argument(
            "Ishmael matched filter input is not raw audio");
    }
    const auto channels = config_.channels.empty()
        ? std::vector<std::size_t>{0}
        : config_.channels;
    std::vector<double> samples(audio->frame_count());
    for (const auto channel : channels) {
        if (channel >= audio->channel_count) {
            continue;
        }
        for (std::size_t frame = 0; frame < audio->frame_count(); ++frame) {
            samples[frame] = audio->sample(frame, channel);
        }
        for (const auto& block : detector_->process(channel, samples)) {
            for (std::size_t index = 0;
                 index < block.values.size();
                 ++index) {
                const auto start_sample =
                    block.start_sample +
                    static_cast<std::int64_t>(index);
                detectors::IshmaelDetSample sample;
                sample.det_value = block.values[index];
                sample.raw_value = block.values[index];
                DataUnitMetadata function_metadata;
                function_metadata.uid = next_uid_;
                function_metadata.sequence = next_uid_++;
                function_metadata.start_sample = start_sample;
                function_metadata.time_unix_ms =
                    audio->time_unix_ms +
                    static_cast<std::int64_t>(
                        static_cast<double>(
                            start_sample -
                            static_cast<std::int64_t>(
                                audio->start_sample)) *
                        1000.0 / sample_rate_hz_);
                function_metadata.channel_bitmap = channel < 32
                    ? std::uint32_t{1} << channel
                    : 0;
                function_metadata.clock_domain_id =
                    unit.metadata.clock_domain_id;
                outputs_.detection_function->publish(make_data_unit(
                    function_metadata,
                    sample));
                auto detection = picker_->process(
                    channel,
                    start_sample,
                    block.values[index]);
                if (!detection.has_value()) {
                    continue;
                }
                function_metadata.uid = next_uid_;
                function_metadata.sequence = next_uid_++;
                detection->start_time_ms =
                    audio->time_unix_ms +
                    static_cast<std::int64_t>(std::llround(
                        static_cast<double>(
                            detection->start_sample -
                            static_cast<std::int64_t>(
                                audio->start_sample)) *
                        1000.0 / sample_rate_hz_));
                function_metadata.time_unix_ms =
                    detection->start_time_ms;
                function_metadata.start_sample =
                    detection->start_sample;
                function_metadata.duration_samples =
                    static_cast<std::uint64_t>(
                        std::max<std::int64_t>(
                            0,
                            detection->duration_samples));
                outputs_.detections->publish(make_data_unit(
                    std::move(function_metadata),
                    std::move(*detection)));
            }
        }
    }
}

} // namespace pamguard::core
