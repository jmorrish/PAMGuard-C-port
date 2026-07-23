#include "pamguard/detectors/LtsaMonitor.h"

#include <algorithm>
#include <cmath>

namespace pamguard::detectors {

LtsaMonitor::LtsaMonitor(const LtsaConfig& config)
    : interval_ms_(static_cast<std::int64_t>(config.interval_seconds) * 1000) {}

void LtsaMonitor::prepare(std::int64_t time_ms, std::size_t half_fft_length) {
    // ChannelProcess.prepare: align the window start to an absolute
    // interval boundary (Java integer division, truncation toward zero).
    current_start_ = (time_ms / interval_ms_) * interval_ms_;
    current_end_ = current_start_ + interval_ms_;
    mean_fft_data_.assign(half_fft_length, 0.0);
    prepared_ = true;
}

std::optional<LtsaInterval> LtsaMonitor::process_frame(std::int64_t time_ms,
                                                       std::int64_t start_sample,
                                                       std::int64_t duration_samples,
                                                       const std::vector<double>& magnitude_squared) {
    if (interval_ms_ <= 0 || magnitude_squared.empty()) {
        return std::nullopt;
    }
    // The reference lazily (re)prepares from the first unit's timestamp when
    // meanFftData is missing or the FFT length changed.
    if (!prepared_ || mean_fft_data_.size() != magnitude_squared.size()) {
        prepare(time_ms, magnitude_squared.size());
    }
    std::optional<LtsaInterval> closed;
    if (time_ms >= current_end_) {
        closed = close_period();
    }
    for (std::size_t i = 0; i < mean_fft_data_.size(); ++i) {
        mean_fft_data_[i] += magnitude_squared[i];
    }
    last_sample_ = start_sample;
    if (n_fft_ == 0) {
        start_sample_ = last_sample_;
    }
    last_sample_ += duration_samples;
    ++n_fft_;
    return closed;
}

std::optional<LtsaInterval> LtsaMonitor::flush() {
    return close_period();
}

std::optional<LtsaInterval> LtsaMonitor::close_period() {
    if (n_fft_ == 0) {
        return std::nullopt;
    }
    LtsaInterval interval;
    interval.start_time_ms = current_start_;
    interval.end_time_ms = current_end_;
    interval.n_fft = n_fft_;
    interval.start_sample = start_sample_;
    interval.duration_samples = last_sample_ - start_sample_;
    interval.magnitude.resize(mean_fft_data_.size());
    for (std::size_t i = 0; i < mean_fft_data_.size(); ++i) {
        interval.magnitude[i] = std::sqrt(mean_fft_data_[i] / n_fft_);
    }
    current_start_ = current_end_;
    current_end_ += interval_ms_;
    std::fill(mean_fft_data_.begin(), mean_fft_data_.end(), 0.0);
    n_fft_ = 0;
    return interval;
}

} // namespace pamguard::detectors
