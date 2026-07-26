#include "pamguard/core/SignalNodes.h"

#include <algorithm>
#include <any>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pamguard::core {

namespace {

void validate_audio_blocks(const DataBlock& input, const DataBlock& output) {
    if (input.descriptor().data_type != kRawAudioDataType ||
        output.descriptor().data_type != kRawAudioDataType) {
        throw std::invalid_argument("Audio node input and output blocks must carry raw audio");
    }
}

std::size_t declared_channel_count(std::uint32_t bitmap) {
    std::size_t count = 0;
    for (std::size_t channel = 0; channel < 32; ++channel) {
        if ((bitmap & (std::uint32_t{1} << channel)) != 0) {
            count = channel + 1;
        }
    }
    return count;
}

DataUnitMetadata output_metadata(
    const DataUnitMetadata& input,
    const AudioChunk& audio,
    const DataBlockDescriptor& output) {
    auto metadata = input;
    metadata.type_id.clear();
    metadata.source_block_id.clear();
    metadata.start_sample = static_cast<std::int64_t>(audio.start_sample);
    metadata.time_unix_ms = audio.time_unix_ms;
    metadata.duration_samples = audio.frame_count();
    metadata.channel_bitmap = output.channel_bitmap;
    if (!output.clock_domain_id.empty()) {
        metadata.clock_domain_id = output.clock_domain_id;
    }
    return metadata;
}

const AudioChunk& audio_payload(const DataUnit& unit) {
    const auto* audio = std::any_cast<AudioChunk>(&unit.payload);
    if (audio == nullptr) {
        throw std::invalid_argument("Raw-audio data unit payload is not an AudioChunk");
    }
    return *audio;
}

} // namespace

dsp::IirFilterParams default_decimator_filter_params(
    double output_sample_rate_hz) {
    dsp::IirFilterParams filter;
    filter.type = dsp::IirFilterType::Butterworth;
    filter.band = dsp::IirFilterBand::LowPass;
    filter.order = 6;
    filter.low_pass_freq_hz =
        static_cast<float>(output_sample_rate_hz / 2.0);
    // These fields remain at the FilterParams constructor defaults. The
    // high-pass corner is inactive for the default LOWPASS band.
    filter.high_pass_freq_hz = 2000.0F;
    filter.pass_band_ripple_db = 2.0;
    filter.stop_band_ripple_db = 2.0;
    filter.cheby_gamma = 3.0;
    return filter;
}

AudioSourceNode::AudioSourceNode(
    std::string instance_id,
    AudioSourceNodeConfig config,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !output_) {
        throw std::invalid_argument("Audio source requires an instance id and output block");
    }
}

const std::string& AudioSourceNode::instance_id() const noexcept { return instance_id_; }
ModuleState AudioSourceNode::state() const noexcept { return state_; }

void AudioSourceNode::prepare() {
    if (output_->descriptor().data_type != kRawAudioDataType) {
        throw std::invalid_argument("Audio source output block must carry raw audio");
    }
    if (!std::isfinite(config_.dc_time_constant_seconds) ||
        config_.dc_time_constant_seconds <= 0.0) {
        throw std::invalid_argument(
            "Audio source DC time constant must be finite and positive");
    }
    const double sample_rate_hz =
        output_->descriptor().sample_rate_hz;
    if (!std::isfinite(sample_rate_hz) ||
        sample_rate_hz <= 0.0) {
        throw std::invalid_argument(
            "Audio source sample rate must be finite and positive");
    }
    // Acquisition.DCFilter.setTimeContant:
    // alpha = 1 - 1 / (sampleRate * timeConstant).
    dc_alpha_ =
        1.0 -
        1.0 /
            (sample_rate_hz *
             config_.dc_time_constant_seconds);
    if (!std::isfinite(dc_alpha_)) {
        throw std::invalid_argument(
            "Audio source DC filter coefficient must be finite");
    }
    dc_background_by_channel_.assign(
        declared_channel_count(
            output_->descriptor().channel_bitmap),
        0.0);
    state_ = ModuleState::Prepared;
}

void AudioSourceNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error("Audio source must be prepared before it starts");
    }
    state_ = ModuleState::Running;
}

void AudioSourceNode::stop() {
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void AudioSourceNode::reset() {
    next_uid_ = 1;
    next_sequence_ = 1;
    expected_start_sample_.reset();
    std::fill(
        dc_background_by_channel_.begin(),
        dc_background_by_channel_.end(),
        0.0);
    state_ = ModuleState::Created;
}

void AudioSourceNode::ingest(AudioChunk chunk) {
    if (state_ != ModuleState::Running) {
        throw std::logic_error("Audio source is not running");
    }
    if (chunk.channel_count == 0 ||
        chunk.sample_rate_hz == 0 ||
        chunk.interleaved_pcm.size() % chunk.channel_count != 0) {
        throw std::invalid_argument("Audio chunk dimensions and sample rate must be valid");
    }
    if (chunk.orientation_declared &&
        (!std::isfinite(
             chunk.orientation_heading_degrees) ||
         !std::isfinite(
             chunk.orientation_pitch_degrees) ||
         !std::isfinite(
             chunk.orientation_roll_degrees))) {
        throw std::invalid_argument(
            "Declared audio orientation must be finite");
    }
    if (chunk.navigation_origin_declared &&
        (!std::isfinite(
             chunk.navigation_origin_east_metres) ||
         !std::isfinite(
             chunk.navigation_origin_north_metres) ||
         !std::isfinite(
             chunk.navigation_origin_height_metres) ||
         chunk.navigation_reference_id.empty())) {
        throw std::invalid_argument(
            "Declared navigation origin requires finite "
            "coordinates and a reference ID");
    }
    const auto expected_channels =
        declared_channel_count(output_->descriptor().channel_bitmap);
    if (chunk.channel_count != expected_channels ||
        chunk.sample_rate_hz != static_cast<std::uint32_t>(
            std::llround(output_->descriptor().sample_rate_hz))) {
        throw std::invalid_argument(
            "Audio chunk does not match the acquisition block format");
    }
    if (config_.subtract_dc) {
        for (std::size_t frame = 0;
             frame < chunk.frame_count();
             ++frame) {
            for (std::size_t channel = 0;
                 channel < chunk.channel_count;
                 ++channel) {
                const auto index =
                    frame * chunk.channel_count + channel;
                const double sample =
                    chunk.interleaved_pcm[index];
                const double filtered =
                    sample -
                    dc_background_by_channel_[channel];
                dc_background_by_channel_[channel] =
                    sample - dc_alpha_ * filtered;
                chunk.interleaved_pcm[index] = filtered;
            }
        }
    }
    DataUnitMetadata metadata;
    metadata.uid = next_uid_++;
    metadata.sequence = next_sequence_++;
    metadata.time_unix_ms = chunk.time_unix_ms;
    metadata.start_sample = static_cast<std::int64_t>(chunk.start_sample);
    metadata.duration_samples = chunk.frame_count();
    metadata.channel_bitmap = output_->descriptor().channel_bitmap;
    metadata.clock_domain_id = output_->descriptor().clock_domain_id;
    metadata.discontinuity =
        expected_start_sample_.has_value() &&
        chunk.start_sample != *expected_start_sample_;
    expected_start_sample_ =
        chunk.start_sample + chunk.frame_count();
    output_->publish(make_data_unit(std::move(metadata), std::move(chunk)));
}

AmplifierNode::AmplifierNode(
    std::string instance_id,
    AmplifierNodeConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument("Amplifier requires an instance id and data blocks");
    }
}

AmplifierNode::~AmplifierNode() { stop(); }
const std::string& AmplifierNode::instance_id() const noexcept { return instance_id_; }
ModuleState AmplifierNode::state() const noexcept { return state_; }

void AmplifierNode::prepare() {
    validate_audio_blocks(*input_, *output_);
    const auto input_channels =
        declared_channel_count(input_->descriptor().channel_bitmap);
    if (!config_.channel_gains.empty() &&
        config_.channel_gains.size() < input_channels) {
        throw std::invalid_argument(
            "Amplifier must define a gain for every input channel");
    }
    if (std::any_of(
            config_.channel_gains.begin(),
            config_.channel_gains.end(),
            [](double gain) { return !std::isfinite(gain); })) {
        throw std::invalid_argument(
            "Amplifier gains must be finite");
    }
    state_ = ModuleState::Prepared;
}

void AmplifierNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error("Amplifier must be prepared before it starts");
    }
    subscription_ = input_->subscribe([this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void AmplifierNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void AmplifierNode::reset() {
    stop();
    state_ = ModuleState::Created;
}

void AmplifierNode::process(const DataUnit& unit) {
    auto audio = audio_payload(unit);
    if (!config_.channel_gains.empty() &&
        config_.channel_gains.size() < audio.channel_count) {
        throw std::invalid_argument("Amplifier does not define a gain for every input channel");
    }
    for (std::size_t frame = 0; frame < audio.frame_count(); ++frame) {
        for (std::size_t channel = 0; channel < audio.channel_count; ++channel) {
            const auto gain = config_.channel_gains.empty()
                ? 1.0
                : config_.channel_gains[channel];
            audio.interleaved_pcm[frame * audio.channel_count + channel] *= gain;
        }
    }
    auto metadata =
        output_metadata(unit.metadata, audio, output_->descriptor());
    output_->publish(make_data_unit(
        std::move(metadata),
        std::move(audio)));
}

PatchPanelNode::PatchPanelNode(
    std::string instance_id,
    PatchPanelNodeConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument("Patch panel requires an instance id and data blocks");
    }
}

PatchPanelNode::~PatchPanelNode() { stop(); }
const std::string& PatchPanelNode::instance_id() const noexcept { return instance_id_; }
ModuleState PatchPanelNode::state() const noexcept { return state_; }

void PatchPanelNode::prepare() {
    validate_audio_blocks(*input_, *output_);
    if (config_.patches.empty()) {
        throw std::invalid_argument("Patch panel matrix must not be empty");
    }
    const auto output_channels = config_.patches.front().size();
    if (output_channels == 0 ||
        std::any_of(
            config_.patches.begin(),
            config_.patches.end(),
            [&](const auto& row) { return row.size() != output_channels; })) {
        throw std::invalid_argument("Patch panel matrix must be rectangular");
    }
    if (config_.patches.size() <
        declared_channel_count(input_->descriptor().channel_bitmap)) {
        throw std::invalid_argument(
            "Patch panel has fewer rows than declared input channels");
    }
    for (const auto& row : config_.patches) {
        if (std::any_of(
                row.begin(),
                row.end(),
                [](double gain) { return !std::isfinite(gain); })) {
            throw std::invalid_argument(
                "Patch panel gains must be finite");
        }
    }
    state_ = ModuleState::Prepared;
}

void PatchPanelNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error("Patch panel must be prepared before it starts");
    }
    subscription_ = input_->subscribe([this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void PatchPanelNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void PatchPanelNode::reset() {
    stop();
    state_ = ModuleState::Created;
}

void PatchPanelNode::process(const DataUnit& unit) {
    const auto& input_audio = audio_payload(unit);
    if (config_.patches.size() < input_audio.channel_count) {
        throw std::invalid_argument("Patch panel has fewer rows than input channels");
    }
    const auto output_channel_count =
        declared_channel_count(output_->descriptor().channel_bitmap);
    if (output_channel_count == 0) {
        // Java publishes no RawDataUnit when no output route is selected.
        return;
    }
    if (config_.patches.front().size() < output_channel_count) {
        throw std::invalid_argument(
            "Patch panel has fewer columns than declared output channels");
    }
    AudioChunk output_audio = input_audio;
    output_audio.channel_count = output_channel_count;
    output_audio.interleaved_pcm.assign(
        input_audio.frame_count() * output_audio.channel_count,
        0.0);
    for (std::size_t frame = 0; frame < input_audio.frame_count(); ++frame) {
        for (std::size_t input_channel = 0;
             input_channel < input_audio.channel_count;
             ++input_channel) {
            if (input_channel >= 32 ||
                (input_->descriptor().channel_bitmap &
                 (std::uint32_t{1} << input_channel)) == 0) {
                continue;
            }
            const auto sample = input_audio.sample(frame, input_channel);
            for (std::size_t output_channel = 0;
                 output_channel < output_audio.channel_count;
                 ++output_channel) {
                output_audio.interleaved_pcm[
                    frame * output_audio.channel_count + output_channel] +=
                    sample * config_.patches[input_channel][output_channel];
            }
        }
    }
    auto metadata = output_metadata(
        unit.metadata,
        output_audio,
        output_->descriptor());
    output_->publish(make_data_unit(
        std::move(metadata),
        std::move(output_audio)));
}

FilterNode::FilterNode(
    std::string instance_id,
    FilterNodeConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument("Filter requires an instance id and data blocks");
    }
}

FilterNode::~FilterNode() { stop(); }
const std::string& FilterNode::instance_id() const noexcept { return instance_id_; }
ModuleState FilterNode::state() const noexcept { return state_; }

void FilterNode::prepare() {
    validate_audio_blocks(*input_, *output_);
    const auto sample_rate = input_->descriptor().sample_rate_hz;
    if (sample_rate <= 0.0) {
        throw std::invalid_argument("Filter input block must declare its sample rate");
    }
    dsp::validate_filter_params(config_.filter, sample_rate);
    filters_.clear();
    filters_.resize(32);
    for (std::size_t channel = 0; channel < filters_.size(); ++channel) {
        if ((config_.channel_bitmap & (std::uint32_t{1} << channel)) != 0) {
            filters_[channel] =
                std::make_unique<dsp::FastIirFilter>(sample_rate, config_.filter);
            if (config_.filter.type != dsp::IirFilterType::None &&
                !filters_[channel]->active()) {
                throw std::invalid_argument(
                    "Filter parameters did not produce an active filter");
            }
        }
    }
    state_ = ModuleState::Prepared;
}

void FilterNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error("Filter must be prepared before it starts");
    }
    subscription_ = input_->subscribe([this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void FilterNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void FilterNode::reset() {
    stop();
    for (const auto& filter : filters_) {
        if (filter) {
            filter->reset();
        }
    }
    state_ = ModuleState::Created;
}

void FilterNode::process(const DataUnit& unit) {
    auto audio = audio_payload(unit);
    if (audio.channel_count > filters_.size()) {
        throw std::invalid_argument("Filter supports at most 32 channels");
    }
    for (std::size_t channel = 0; channel < audio.channel_count; ++channel) {
        if (!filters_[channel]) {
            for (std::size_t frame = 0; frame < audio.frame_count(); ++frame) {
                audio.interleaved_pcm[
                    frame * audio.channel_count + channel] = 0.0;
            }
            continue;
        }
        std::vector<double> input(audio.frame_count());
        for (std::size_t frame = 0; frame < audio.frame_count(); ++frame) {
            input[frame] = audio.sample(frame, channel);
        }
        std::vector<double> filtered;
        filters_[channel]->run(input, filtered);
        if (filtered.size() != input.size()) {
            throw std::runtime_error(
                "Filter changed the raw-audio block length");
        }
        for (std::size_t frame = 0; frame < audio.frame_count(); ++frame) {
            audio.interleaved_pcm[
                frame * audio.channel_count + channel] = filtered[frame];
        }
    }
    output_->publish(make_data_unit(
        output_metadata(unit.metadata, audio, output_->descriptor()),
        std::move(audio)));
}

DecimatorNode::DecimatorNode(
    std::string instance_id,
    DecimatorNodeConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument("Decimator requires an instance id and data blocks");
    }
}

DecimatorNode::~DecimatorNode() { stop(); }
const std::string& DecimatorNode::instance_id() const noexcept { return instance_id_; }
ModuleState DecimatorNode::state() const noexcept { return state_; }

void DecimatorNode::prepare() {
    validate_audio_blocks(*input_, *output_);
    const auto input_rate = input_->descriptor().sample_rate_hz;
    const auto output_rate = config_.output_sample_rate_hz;
    if (input_rate <= 0.0 || output_rate <= 0.0 ||
        output_rate > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
        std::abs(output_rate - std::round(output_rate)) > 1e-9) {
        throw std::invalid_argument("Decimator input/output sample rates must be positive integer rates");
    }
    if (config_.interpolation < 0 ||
        config_.interpolation > 2) {
        throw std::invalid_argument(
            "Decimator interpolation mode must be 0, 1, or 2");
    }
    const auto declared_output_rate = output_->descriptor().sample_rate_hz;
    if (declared_output_rate > 0.0 &&
        std::abs(declared_output_rate - output_rate) > 1e-9) {
        throw std::invalid_argument("Decimator output block sample rate does not match its settings");
    }

    // DecimatorProcessW.checkFilterParams mutates only lowPassFreq, even when
    // the configured type/band does not use that field.
    auto filter = config_.filter;
    filter.low_pass_freq_hz =
        std::min(
            filter.low_pass_freq_hz,
            static_cast<float>(input_rate / 2.0));
    const auto filter_sample_rate = std::max(input_rate, output_rate);
    dsp::validate_filter_params(filter, filter_sample_rate);
    channels_.clear();
    channels_.resize(32);
    for (std::size_t channel = 0; channel < channels_.size(); ++channel) {
        if ((config_.channel_bitmap & (std::uint32_t{1} << channel)) == 0) {
            continue;
        }
        channels_[channel].filter = std::make_unique<dsp::FastIirFilter>(
            filter_sample_rate,
            filter);
        if (filter.type != dsp::IirFilterType::None &&
            !channels_[channel].filter->active()) {
            throw std::invalid_argument(
                "Decimator FilterParams did not produce an active filter");
        }
        channels_[channel].interpolation_history.assign(
            static_cast<std::size_t>(config_.interpolation),
            0.0);
    }
    initialized_ = false;
    next_output_sample_ = 0;
    state_ = ModuleState::Prepared;
}

void DecimatorNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error("Decimator must be prepared before it starts");
    }
    subscription_ = input_->subscribe([this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void DecimatorNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void DecimatorNode::reset() {
    stop();
    prepare();
    state_ = ModuleState::Created;
}

double DecimatorNode::interpolate(
    ChannelState& state,
    const std::vector<double>& samples,
    double position) const {
    if (config_.interpolation == 0) {
        const auto index = static_cast<std::size_t>(std::floor(position + 0.5));
        return samples.at(index);
    }

    std::vector<double> internal;
    internal.reserve(state.interpolation_history.size() + samples.size());
    internal.insert(
        internal.end(),
        state.interpolation_history.begin(),
        state.interpolation_history.end());
    internal.insert(internal.end(), samples.begin(), samples.end());
    if (config_.interpolation == 1) {
        const auto first = static_cast<std::size_t>(position);
        const auto second = first + 1;
        const auto first_weight = static_cast<double>(second) - position;
        return internal.at(first) * first_weight +
               internal.at(second) * (1.0 - first_weight);
    }

    const auto middle = static_cast<std::size_t>(position + 1.5);
    const auto first = middle - 1;
    const auto third = middle + 1;
    const auto relative = position - static_cast<double>(first);
    const auto first_value = internal.at(first);
    const auto middle_value = internal.at(middle);
    const auto third_value = internal.at(third);
    const auto a = (first_value + third_value) / 2.0 - middle_value;
    const auto b = (third_value - first_value) / 2.0;
    return a * relative * relative + b * relative + middle_value;
}

void DecimatorNode::process(const DataUnit& unit) {
    const auto& input_audio = audio_payload(unit);
    if (input_audio.channel_count == 0 ||
        input_audio.channel_count > channels_.size() ||
        input_audio.sample_rate_hz !=
            static_cast<std::uint32_t>(std::llround(input_->descriptor().sample_rate_hz))) {
        throw std::invalid_argument("Decimator audio does not match its prepared input format");
    }
    if (unit.metadata.discontinuity) {
        for (auto& channel : channels_) {
            if (channel.filter) {
                channel.filter->reset();
                channel.pick_sample = 0.0;
                std::fill(
                    channel.interpolation_history.begin(),
                    channel.interpolation_history.end(),
                    0.0);
            }
        }
        initialized_ = false;
    }

    const auto input_rate = input_->descriptor().sample_rate_hz;
    const auto output_rate = config_.output_sample_rate_hz;
    if (!initialized_) {
        next_output_sample_ = static_cast<std::uint64_t>(
            static_cast<double>(input_audio.start_sample) * output_rate / input_rate);
        initialized_ = true;
    }

    std::vector<std::vector<double>> channel_outputs(input_audio.channel_count);
    std::size_t output_frames = 0;
    for (std::size_t channel = 0; channel < input_audio.channel_count; ++channel) {
        auto& state = channels_[channel];
        if (!state.filter) {
            continue;
        }
        std::vector<double> samples(input_audio.frame_count());
        for (std::size_t frame = 0; frame < input_audio.frame_count(); ++frame) {
            samples[frame] = input_audio.sample(frame, channel);
        }
        if (input_rate > output_rate) {
            std::vector<double> filtered;
            state.filter->run(samples, filtered);
            samples = std::move(filtered);
        }

        auto& produced = channel_outputs[channel];
        produced.reserve(static_cast<std::size_t>(
            std::ceil(samples.size() * output_rate / input_rate)));
        while (state.pick_sample < static_cast<double>(samples.size()) - 0.5) {
            produced.push_back(interpolate(state, samples, state.pick_sample));
            state.pick_sample += input_rate / output_rate;
        }
        state.pick_sample -= static_cast<double>(samples.size());

        const auto history_size = state.interpolation_history.size();
        if (history_size > 0) {
            std::vector<double> combined = state.interpolation_history;
            combined.insert(combined.end(), samples.begin(), samples.end());
            std::copy(
                combined.end() - static_cast<std::ptrdiff_t>(history_size),
                combined.end(),
                state.interpolation_history.begin());
        }
        if (input_rate < output_rate) {
            std::vector<double> filtered;
            state.filter->run(produced, filtered);
            produced = std::move(filtered);
        }
        if (output_frames == 0) {
            output_frames = produced.size();
        }
        else if (produced.size() != output_frames) {
            throw std::runtime_error("Decimator channels lost sample alignment");
        }
    }
    if (output_frames == 0) {
        return;
    }

    AudioChunk output_audio;
    output_audio.start_sample = next_output_sample_;
    output_audio.time_unix_ms = input_audio.time_unix_ms;
    output_audio.sample_rate_hz = static_cast<std::uint32_t>(std::llround(output_rate));
    output_audio.channel_count = input_audio.channel_count;
    output_audio.orientation_declared = input_audio.orientation_declared;
    output_audio.orientation_heading_degrees = input_audio.orientation_heading_degrees;
    output_audio.orientation_pitch_degrees = input_audio.orientation_pitch_degrees;
    output_audio.orientation_roll_degrees = input_audio.orientation_roll_degrees;
    output_audio.navigation_origin_declared =
        input_audio.navigation_origin_declared;
    output_audio.navigation_origin_east_metres =
        input_audio.navigation_origin_east_metres;
    output_audio.navigation_origin_north_metres =
        input_audio.navigation_origin_north_metres;
    output_audio.navigation_origin_height_metres =
        input_audio.navigation_origin_height_metres;
    output_audio.navigation_reference_id =
        input_audio.navigation_reference_id;
    output_audio.interleaved_pcm.assign(
        output_frames * output_audio.channel_count,
        0.0);
    for (std::size_t frame = 0; frame < output_frames; ++frame) {
        for (std::size_t channel = 0; channel < output_audio.channel_count; ++channel) {
            if (!channel_outputs[channel].empty()) {
                output_audio.interleaved_pcm[
                    frame * output_audio.channel_count + channel] =
                    channel_outputs[channel][frame];
            }
        }
    }
    next_output_sample_ += output_frames;
    output_->publish(make_data_unit(
        output_metadata(
            unit.metadata,
            output_audio,
            output_->descriptor()),
        std::move(output_audio)));
}

FftNode::FftNode(
    std::string instance_id,
    FftConfig config,
    std::shared_ptr<DataBlock> input,
    std::shared_ptr<DataBlock> output)
    : instance_id_(std::move(instance_id)),
      config_(std::move(config)),
      input_(std::move(input)),
      output_(std::move(output)) {
    if (instance_id_.empty() || !input_ || !output_) {
        throw std::invalid_argument("FFT requires an instance id and data blocks");
    }
}

FftNode::~FftNode() { stop(); }
const std::string& FftNode::instance_id() const noexcept { return instance_id_; }
ModuleState FftNode::state() const noexcept { return state_; }

void FftNode::prepare() {
    if (input_->descriptor().data_type != kRawAudioDataType ||
        output_->descriptor().data_type != kFftDataType) {
        throw std::invalid_argument("FFT node requires raw-audio input and FFT output");
    }
    engine_ = std::make_unique<dsp::SpectrogramEngine>(config_);
    state_ = ModuleState::Prepared;
}

void FftNode::start() {
    if (state_ != ModuleState::Prepared && state_ != ModuleState::Stopped) {
        throw std::logic_error("FFT must be prepared before it starts");
    }
    subscription_ = input_->subscribe([this](const DataUnit& unit) { process(unit); });
    state_ = ModuleState::Running;
}

void FftNode::stop() {
    subscription_.cancel();
    if (state_ == ModuleState::Running) {
        state_ = ModuleState::Stopped;
    }
}

void FftNode::reset() {
    stop();
    engine_ = std::make_unique<dsp::SpectrogramEngine>(config_);
    state_ = ModuleState::Created;
}

void FftNode::process(const DataUnit& unit) {
    auto frames = engine_->process(audio_payload(unit));
    // PAMGuard publishes FFT units slice-major/channel-minor. The underlying
    // engine keeps channel-local buffers and therefore naturally returns
    // channel-major frames for a multi-frame chunk; normalize at the graph
    // boundary so shared-state downstream modules see authoritative ordering.
    std::stable_sort(
        frames.begin(),
        frames.end(),
        [](const auto& left, const auto& right) {
            if (left.fft_slice != right.fft_slice) {
                return left.fft_slice < right.fft_slice;
            }
            return left.channel < right.channel;
        });
    for (auto& frame : frames) {
        DataUnitMetadata metadata;
        metadata.uid = unit.metadata.uid;
        metadata.sequence = frame.fft_slice;
        metadata.time_unix_ms = frame.time_unix_ms;
        metadata.start_sample = frame.start_sample;
        metadata.duration_samples = config_.fft_length;
        metadata.channel_bitmap = frame.channel < 32
            ? (std::uint32_t{1} << frame.channel)
            : 0;
        metadata.clock_domain_id = unit.metadata.clock_domain_id;
        metadata.discontinuity = unit.metadata.discontinuity;
        output_->publish(make_data_unit(std::move(metadata), std::move(frame)));
    }
}

} // namespace pamguard::core
