#include "pamguard/detectors/IshmaelDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pamguard::detectors {

namespace {

// Java's -Double.MIN_VALUE: the reference's "unset" sentinel for the noise
// floor and smoothing state.
constexpr double kUnsetSentinel = -std::numeric_limits<double>::denorm_min();

} // namespace

IshmaelEnergySum::IshmaelEnergySum(double sample_rate_hz, const IshmaelEnergySumConfig& config)
    : sample_rate_hz_(sample_rate_hz),
      config_(config),
      noise_floor_(kUnsetSentinel),
      smooth_result_(kUnsetSentinel) {}

void IshmaelEnergySum::prepare_params(int len) {
    saved_gram_height_ = len;
    // "Should be max(1,...) here, but FFT bin 0 has 0's in it."
    // The reference divides its float sampleRate by 2 in float precision.
    const double half_rate = static_cast<double>(static_cast<float>(sample_rate_hz_) / 2.0F);
    lo_bin_ = std::max(1, static_cast<int>(std::floor(len * config_.f0 / half_rate)));
    hi_bin_ = std::min(len - 1, static_cast<int>(std::ceil(len * config_.f1 / half_rate)));
    lo_bin_ratio_ = std::max(1, static_cast<int>(std::floor(len * config_.ratio_f0 / half_rate)));
    hi_bin_ratio_ = std::min(len - 1, static_cast<int>(std::ceil(len * config_.ratio_f1 / half_rate)));
}

double IshmaelEnergySum::calc_energy_peak(const std::vector<double>& magnitude_squared,
                                          int low_bin, int high_bin) const {
    double sum = 0.0;
    if (config_.use_log) {
        for (int i = low_bin; i <= high_bin; ++i) {
            // The reference's floor hack, verbatim.
            sum += std::log10(std::max(magnitude_squared[static_cast<std::size_t>(i)], 1.0e-9)) + 5;
        }
    }
    else {
        for (int i = low_bin; i <= high_bin; ++i) {
            sum += magnitude_squared[static_cast<std::size_t>(i)];
        }
    }
    return sum / (high_bin - low_bin + 1);
}

IshmaelDetSample IshmaelEnergySum::process_frame(const std::vector<double>& magnitude_squared) {
    if (static_cast<int>(magnitude_squared.size()) != saved_gram_height_) {
        prepare_params(static_cast<int>(magnitude_squared.size()));
    }

    double result = calc_energy_peak(magnitude_squared, lo_bin_, hi_bin_);
    if (config_.use_ratio) {
        const double result_ratio = calc_energy_peak(magnitude_squared, lo_bin_ratio_, hi_bin_ratio_);
        if (config_.use_log) {
            result = result - result_ratio;
        }
        else {
            result = result / result_ratio;
        }
    }

    // Reference quirk kept verbatim: smoothResult is assigned only on the
    // FIRST result and never updated, so every later result is blended with
    // the first one rather than a running average.
    if (config_.output_smoothing) {
        if (smooth_result_ == kUnsetSentinel) {
            smooth_result_ = result;
        }
        else {
            result = config_.short_filter * result + (1.0 - config_.short_filter) * smooth_result_;
        }
    }

    IshmaelDetSample sample;
    if (config_.adaptive_threshold) {
        if (noise_floor_ == kUnsetSentinel) {
            noise_floor_ = result;
        }
        else {
            noise_floor_ = config_.long_filter * result + (1.0 - config_.long_filter) * noise_floor_;
        }
        // Exponential decay after a loud transient.
        if (noise_floor_ > config_.spike_decay * result) {
            noise_floor_ = 0.5 * noise_floor_;
        }
        sample.has_noise_floor = true;
        sample.noise_floor = noise_floor_;
        sample.raw_value = result;
        result = result - noise_floor_;
    }
    sample.det_value = result;
    return sample;
}

void IshmaelPeakPicker::PerChannelInfo::reset() {
    // The reference's reset: lastStartSam/lastDurationSam and nUnderThresh
    // deliberately survive.
    n_over_thresh = 0;
    start_sample = 0;
    end_sample = 0;
    peak_height = 0.0;
}

IshmaelPeakPicker::IshmaelPeakPicker(double sample_rate_hz, std::size_t fft_hop,
                                     const IshmaelEnergySumConfig& config)
    : config_(config),
      sample_rate_hz_(sample_rate_hz) {
    // getDetSampleRate: the FFT frame rate, as a float, exactly as the
    // reference computes and stores it.
    det_sample_rate_ = fft_hop == 0
        ? 0.0F
        : static_cast<float>(sample_rate_hz) / static_cast<float>(fft_hop);
    const double d_rate = static_cast<double>(det_sample_rate_);
    min_time_n_ = std::max<std::int64_t>(0, static_cast<std::int64_t>(d_rate * config_.min_time_s));
    if (config_.max_time_s == 0.0) {
        // maxTime==0 disables the cap; -1 because the gate tests maxTimeN+1.
        max_time_n_ = std::numeric_limits<std::int64_t>::max() - 1;
    }
    else {
        max_time_n_ = std::max<std::int64_t>(0, static_cast<std::int64_t>(d_rate * config_.max_time_s));
    }
    refractory_time_sam_ = std::max<std::int64_t>(0, static_cast<std::int64_t>(d_rate * config_.refractory_time_s));
}

IshmaelPeakPicker::PerChannelInfo& IshmaelPeakPicker::channel_info(std::size_t channel) {
    if (channel >= channels_.size()) {
        channels_.resize(channel + 1);
    }
    return channels_[channel];
}

std::optional<IshmaelDetection> IshmaelPeakPicker::process(std::size_t channel,
                                                           std::int64_t start_sample,
                                                           double det_value) {
    PerChannelInfo& chan = channel_info(channel);

    if (det_value > config_.thresh && chan.n_over_thresh <= max_time_n_ + 1) {
        if (chan.n_over_thresh == 0) {
            chan.peak_height = -std::numeric_limits<double>::infinity();
            chan.start_sample = start_sample;
        }
        chan.end_sample = start_sample;
        chan.n_under_thresh = 0;
        ++chan.n_over_thresh;
        if (det_value >= chan.peak_height) {
            chan.peak_height = det_value;
            chan.peak_time_sample = start_sample;
        }
        return std::nullopt;
    }

    // Below threshold, or past the maximum event length.
    ++chan.n_under_thresh;
    if (chan.n_under_thresh < refractory_time_sam_) {
        // Calls cross the threshold many times: wait out the gap before
        // deciding the event has ended.
        return std::nullopt;
    }
    const std::int64_t duration = chan.end_sample - chan.start_sample + 1;
    if (chan.n_over_thresh > 0 && duration >= min_time_n_ && chan.n_over_thresh <= max_time_n_) {
        // Reference unit quirks kept verbatim: duration is RAW samples
        // against minTimeN in detection-rate slices, and the
        // inter-detection interval is RAW samples against refractoryTimeSam
        // in slices. (The reference also computes an endMsec that
        // IshDetection's constructor discards; it is not reproduced.)
        const std::int64_t duration_sam = duration;
        const auto start_msec = static_cast<std::int64_t>(
            static_cast<double>(chan.start_sample) * 1000.0 / sample_rate_hz_);

        std::optional<IshmaelDetection> accepted;
        const double idi_sam = std::abs(static_cast<double>(
            chan.start_sample - (chan.last_start_sample + chan.last_duration_samples)));
        if (chan.last_start_sample == -1 || chan.start_sample < chan.last_start_sample ||
            idi_sam >= static_cast<double>(refractory_time_sam_)) {
            IshmaelDetection detection;
            detection.channel = channel;
            detection.start_sample = chan.start_sample;
            detection.duration_samples = duration_sam;
            detection.peak_time_sample = chan.peak_time_sample;
            detection.peak_height = chan.peak_height;
            detection.start_time_ms = start_msec;
            detection.low_freq_hz = static_cast<float>(config_.f0);
            detection.high_freq_hz = static_cast<float>(config_.f1);
            accepted = detection;
        }
        chan.last_start_sample = chan.start_sample;
        chan.last_duration_samples = duration_sam;
        chan.reset();
        return accepted;
    }
    // A building call that never reached adequate duration: reset and allow
    // a new detection to start.
    chan.reset();
    return std::nullopt;
}

} // namespace pamguard::detectors
