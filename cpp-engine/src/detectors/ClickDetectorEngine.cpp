#include "pamguard/detectors/ClickDetectorEngine.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace pamguard::detectors {

namespace {

std::uint32_t set_bit(std::uint32_t bitmap, std::size_t bit_number, bool bit_set) {
    const auto mask = static_cast<std::uint32_t>(1u << bit_number);
    return bit_set ? (bitmap | mask) : (bitmap & ~mask);
}

} // namespace

ClickDetectorEngine::TriggerFilter::TriggerFilter(double alpha, double initial_value)
    : TriggerFilter(alpha, alpha, initial_value) {
}

ClickDetectorEngine::TriggerFilter::TriggerFilter(double alpha1, double alpha2, double initial_value) {
    alpha_[0] = alpha1;
    alpha_1_[0] = 1.0 - alpha1;
    alpha_[1] = alpha2;
    alpha_1_[1] = 1.0 - alpha2;
    memory_ = initial_value;
}

double ClickDetectorEngine::TriggerFilter::run(double new_value, bool over_threshold) {
    const auto index = over_threshold ? 1u : 0u;
    memory_ = memory_ * alpha_1_[index] + std::abs(new_value) * alpha_[index];
    return memory_;
}

void ClickDetectorEngine::TriggerFilter::set_memory(double memory) noexcept {
    memory_ = memory;
}

ClickDetectorEngine::ClickDetectorEngine(ClickDetectorConfig config)
    : config_(std::move(config)) {
    if (config_.channel_bitmap == 0) {
        throw std::invalid_argument("click detector channel_bitmap must include at least one channel");
    }
    if (config_.short_filter < 0.0 || config_.short_filter > 1.0 ||
        config_.long_filter < 0.0 || config_.long_filter > 1.0 ||
        config_.long_filter_2 < 0.0 || config_.long_filter_2 > 1.0) {
        throw std::invalid_argument("click detector filter alphas must be in the range 0..1");
    }
    if (config_.max_length == 0) {
        throw std::invalid_argument("click detector max_length must be non-zero");
    }
    if (config_.sample_noise &&
        (!std::isfinite(config_.noise_sample_interval_seconds) ||
         config_.noise_sample_interval_seconds <= 0.0)) {
        throw std::invalid_argument(
            "click detector noise_sample_interval_seconds must be positive and finite");
    }
    if (config_.store_background &&
        config_.background_interval_milliseconds <= 0) {
        throw std::invalid_argument(
            "click detector background_interval_milliseconds must be positive");
    }
    setup_channels();
    reset();
}

const ClickDetectorConfig& ClickDetectorEngine::config() const noexcept {
    return config_;
}

const std::vector<ClickNoiseSampleResult>& ClickDetectorEngine::noise_samples() const noexcept {
    return noise_samples_;
}

const std::vector<ClickTriggerBackgroundResult>&
ClickDetectorEngine::trigger_background() const noexcept {
    return trigger_background_;
}

const std::vector<ClickTriggerFunctionResult>&
ClickDetectorEngine::trigger_function() const noexcept {
    return trigger_function_;
}

void ClickDetectorEngine::reset() {
    short_filters_.clear();
    long_filters_.clear();
    short_filters_.reserve(channels_.size());
    long_filters_.reserve(channels_.size());
    for (std::size_t i = 0; i < channels_.size(); ++i) {
        short_filters_.emplace_back(config_.short_filter, 0.0);
        long_filters_.emplace_back(config_.long_filter, 1.0);
    }
    filters_initialized_ = false;
    click_status_ = ClickStatus::ClickOff;
    samples_processed_ = 0;
    click_start_sample_ = 0;
    click_end_sample_ = 0;
    max_signal_excess_db_ = 0.0;
    click_triggers_ = 0;
    over_threshold_ = 0;
    down_count_ = 0;
    up_count_ = 0;
    next_noise_sample_ = static_cast<std::int64_t>(config_.max_length);
    next_background_time_ms_ = 0;
    waveform_history_.assign(channels_.size(), {});
    history_start_sample_ = 0;
    history_initialized_ = false;
    noise_samples_.clear();
    trigger_background_.clear();
    trigger_function_.clear();
}

std::vector<ClickDetectionResult> ClickDetectorEngine::process(const core::AudioChunk& chunk) {
    if (chunk.sample_rate_hz == 0 || chunk.channel_count == 0) {
        throw std::invalid_argument("audio chunk must include sample rate and channel count");
    }
    if (!std::all_of(channels_.begin(), channels_.end(), [&](std::size_t channel) { return channel < chunk.channel_count; })) {
        throw std::invalid_argument("audio chunk does not contain all click detector channels");
    }
    noise_samples_.clear();
    trigger_background_.clear();
    trigger_function_.clear();
    // PAMGuard's newData: raw -> preFilter -> waveformData (what click
    // waveforms are captured from), then triggerFilter over waveformData ->
    // triggerData (what the short/long trigger consumes). Filter state runs
    // continuously across chunks, exactly as the per-channel FastIIRFilters
    // do in the reference.
    const auto frame_total = chunk.frame_count();
    if (!iir_filters_built_) {
        for (std::size_t i_chan = 0; i_chan < channels_.size(); ++i_chan) {
            pre_iir_filters_.emplace_back(static_cast<double>(chunk.sample_rate_hz), config_.pre_filter);
            trigger_iir_filters_.emplace_back(static_cast<double>(chunk.sample_rate_hz), config_.trigger_filter);
        }
        prefiltered_.assign(channels_.size(), {});
        trigger_data_.assign(channels_.size(), {});
        iir_filters_built_ = true;
    }
    for (std::size_t i_chan = 0; i_chan < channels_.size(); ++i_chan) {
        auto& pre = prefiltered_[i_chan];
        pre.resize(frame_total);
        for (std::size_t i_samp = 0; i_samp < frame_total; ++i_samp) {
            pre[i_samp] = chunk.sample(i_samp, channels_[i_chan]);
        }
        if (pre_iir_filters_[i_chan].active()) {
            std::vector<double> filtered;
            pre_iir_filters_[i_chan].run(pre, filtered);
            pre = std::move(filtered);
        }
        auto& trigger = trigger_data_[i_chan];
        if (trigger_iir_filters_[i_chan].active()) {
            trigger_iir_filters_[i_chan].run(pre, trigger);
        }
        else {
            trigger = pre;
        }
    }

    append_waveform_history(chunk);

    if (!filters_initialized_) {
        initialize_filters(chunk);
        filters_initialized_ = true;
    }

    std::vector<ClickDetectionResult> detections;
    ClickTriggerFunctionResult trigger_function;
    if (config_.publish_trigger_function) {
        trigger_function.channel_bitmap = config_.channel_bitmap;
        trigger_function.start_sample = static_cast<std::int64_t>(chunk.start_sample);
        trigger_function.time_unix_ms = chunk.time_unix_ms;
        trigger_function.channels = channels_;
        trigger_function.signal_excess_db.assign(
            channels_.size(), std::vector<double>(frame_total, 0.0));
    }
    const auto threshold = std::pow(10.0, config_.threshold_db / 20.0);
    const auto frame_count = chunk.frame_count();

    for (std::size_t i_samp = 0; i_samp < frame_count; ++i_samp) {
        double max_signal_excess = -10000.0;
        for (std::size_t i_chan = 0; i_chan < channels_.size(); ++i_chan) {
            const auto channel = channels_[i_chan];
            if (!channel_is_triggered(channel)) {
                continue;
            }

            const double sample = trigger_data_[i_chan][i_samp];
            const double short_value = short_filters_[i_chan].run(sample, false);
            const double long_value = long_filters_[i_chan].run(sample, over_threshold_ != 0);
            over_threshold_ = set_bit(over_threshold_, channel, short_value > long_value * threshold);
            const double db = long_value > 0.0 ? 20.0 * std::log10(short_value / long_value) : -100.0;
            max_signal_excess = std::max(max_signal_excess, db);
            if (config_.publish_trigger_function) {
                trigger_function.signal_excess_db[i_chan][i_samp] = db;
            }
        }

        const auto absolute_sample = static_cast<std::int64_t>(chunk.start_sample + i_samp);
        if (click_status_ == ClickStatus::ClickOff && over_threshold_ != 0) {
            click_status_ = ClickStatus::ClickOn;
            click_start_sample_ = std::max<std::int64_t>(0, absolute_sample - static_cast<std::int64_t>(config_.pre_sample));
            click_end_sample_ = absolute_sample;
            click_triggers_ = over_threshold_;
            max_signal_excess_db_ = max_signal_excess;
            down_count_ = 0;
            up_count_ = 1;
        }
        else if (click_status_ == ClickStatus::ClickEnding) {
            if (over_threshold_ != 0) {
                click_status_ = ClickStatus::ClickOn;
                down_count_ = 0;
                ++up_count_;
                click_end_sample_ = absolute_sample;
            }
            else if (++down_count_ > config_.min_sep) {
                click_status_ = ClickStatus::ClickOff;
                const auto raw_duration = click_end_sample_ + static_cast<std::int64_t>(config_.post_sample) - click_start_sample_ + 1;
                const auto duration = static_cast<std::size_t>(std::min<std::int64_t>(raw_duration, static_cast<std::int64_t>(config_.max_length)));
                if (std::popcount(click_triggers_) >= config_.min_trigger_channels) {
                    ClickDetectionResult result;
                    result.channel_bitmap = config_.channel_bitmap;
                    result.trigger_bitmap = click_triggers_;
                    result.start_sample = click_start_sample_;
                    result.duration_samples = duration;
                    result.time_unix_ms = estimate_time_ms(chunk, click_start_sample_);
                    result.signal_excess_db = max_signal_excess_db_;
                    result.channels = channels_;
                    result.waveform = extract_waveform(click_start_sample_, duration);
                    detections.push_back(result);
                }
            }
        }
        else if (click_status_ == ClickStatus::ClickOn) {
            if (over_threshold_ == 0) {
                click_status_ = ClickStatus::ClickEnding;
            }
            else {
                ++up_count_;
                click_triggers_ |= over_threshold_;
                click_end_sample_ = absolute_sample;
                max_signal_excess_db_ = std::max(max_signal_excess_db_, max_signal_excess);
            }
        }

        if (config_.sample_noise && samples_processed_ > next_noise_sample_) {
            ClickNoiseSampleResult noise;
            noise.channel_bitmap = config_.channel_bitmap;
            noise.start_sample =
                next_noise_sample_ - static_cast<std::int64_t>(config_.max_length);
            noise.duration_samples = config_.max_length;
            noise.time_unix_ms = estimate_time_ms(chunk, noise.start_sample);
            noise.channels = channels_;
            noise.waveform = extract_waveform(noise.start_sample, noise.duration_samples);
            if (!noise.waveform.empty()) {
                noise_samples_.push_back(std::move(noise));
            }
            next_noise_sample_ += static_cast<std::int64_t>(
                config_.noise_sample_interval_seconds *
                static_cast<double>(chunk.sample_rate_hz));
        }
        ++samples_processed_;
    }

    if (config_.publish_trigger_function) {
        trigger_function.long_filter_values.reserve(long_filters_.size());
        for (const auto& filter : long_filters_) {
            trigger_function.long_filter_values.push_back(filter.memory());
        }
        trigger_function_.push_back(std::move(trigger_function));
    }
    if (config_.store_background) {
        const auto end_sample = static_cast<std::int64_t>(
            chunk.start_sample + chunk.frame_count());
        const auto end_time_ms = estimate_time_ms(chunk, end_sample);
        if (end_time_ms >= next_background_time_ms_) {
            ClickTriggerBackgroundResult background;
            background.channel_bitmap = config_.channel_bitmap;
            background.time_unix_ms = end_time_ms;
            background.channels = channels_;
            background.values.reserve(long_filters_.size());
            for (const auto& filter : long_filters_) {
                background.values.push_back(filter.memory());
            }
            trigger_background_.push_back(std::move(background));
            next_background_time_ms_ =
                end_time_ms + config_.background_interval_milliseconds;
        }
    }
    trim_waveform_history();
    return detections;
}

void ClickDetectorEngine::setup_channels() {
    channels_.clear();
    for (std::size_t channel = 0; channel < 32; ++channel) {
        if ((config_.channel_bitmap & (1u << channel)) != 0) {
            channels_.push_back(channel);
        }
    }
}

void ClickDetectorEngine::initialize_filters(const core::AudioChunk& chunk) {
    const auto threshold = std::pow(10.0, config_.threshold_db / 20.0);
    const auto frame_count = chunk.frame_count();
    if (frame_count == 0) {
        return;
    }

    for (std::size_t i_chan = 0; i_chan < channels_.size(); ++i_chan) {
        const auto channel = channels_[i_chan];
        if (!channel_is_triggered(channel)) {
            continue;
        }

        double short_value = 0.0;
        double long_value = 0.0;
        for (std::size_t i_samp = 0; i_samp < frame_count; ++i_samp) {
            const double sample = std::abs(trigger_data_[i_chan][i_samp]);
            short_value += sample;
            long_value += sample;
        }
        short_filters_[i_chan].set_memory(short_value / frame_count * threshold);
        long_filters_[i_chan].set_memory(long_value / frame_count);
    }
}

void ClickDetectorEngine::append_waveform_history(const core::AudioChunk& chunk) {
    const auto expected_start = history_initialized_
        ? history_start_sample_ + (waveform_history_.empty() ? 0 : waveform_history_[0].size())
        : chunk.start_sample;
    if (!history_initialized_ || chunk.start_sample != expected_start) {
        waveform_history_.assign(channels_.size(), {});
        history_start_sample_ = chunk.start_sample;
        history_initialized_ = true;
    }

    const auto frame_count = chunk.frame_count();
    for (std::size_t i = 0; i < frame_count; ++i) {
        for (std::size_t i_chan = 0; i_chan < channels_.size(); ++i_chan) {
            // Click waveforms come from the PREFILTERED stream, as PAMGuard
            // captures from filteredDataBlock when a prefilter runs.
            waveform_history_[i_chan].push_back(prefiltered_[i_chan][i]);
        }
    }
}

void ClickDetectorEngine::trim_waveform_history() {
    if (waveform_history_.empty()) {
        return;
    }

    const auto keep_samples = config_.max_length + config_.pre_sample + config_.post_sample + config_.min_sep + 1;
    while (waveform_history_[0].size() > keep_samples) {
        for (auto& channel_history : waveform_history_) {
            channel_history.pop_front();
        }
        ++history_start_sample_;
    }
}

std::vector<std::vector<double>> ClickDetectorEngine::extract_waveform(std::int64_t start_sample, std::size_t duration) const {
    if (!history_initialized_ || start_sample < 0 || waveform_history_.empty()) {
        return {};
    }

    const auto unsigned_start = static_cast<std::uint64_t>(start_sample);
    if (unsigned_start < history_start_sample_) {
        return {};
    }
    const auto offset = static_cast<std::size_t>(unsigned_start - history_start_sample_);
    if (offset + duration > waveform_history_[0].size()) {
        return {};
    }

    std::vector<std::vector<double>> waveform(waveform_history_.size());
    for (std::size_t i_chan = 0; i_chan < waveform_history_.size(); ++i_chan) {
        waveform[i_chan].reserve(duration);
        for (std::size_t i = 0; i < duration; ++i) {
            waveform[i_chan].push_back(waveform_history_[i_chan][offset + i]);
        }
    }
    return waveform;
}

bool ClickDetectorEngine::channel_is_triggered(std::size_t channel) const noexcept {
    return (config_.trigger_bitmap & (1u << channel)) != 0;
}

std::int64_t ClickDetectorEngine::estimate_time_ms(const core::AudioChunk& chunk, std::int64_t start_sample) const {
    if (chunk.sample_rate_hz == 0) {
        return chunk.time_unix_ms;
    }
    const auto delta_samples = static_cast<double>(start_sample - static_cast<std::int64_t>(chunk.start_sample));
    const auto delta_ms = static_cast<std::int64_t>(delta_samples * 1000.0 / chunk.sample_rate_hz);
    return chunk.time_unix_ms + delta_ms;
}

} // namespace pamguard::detectors
