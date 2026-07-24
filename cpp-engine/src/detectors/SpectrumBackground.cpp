#include "pamguard/detectors/SpectrumBackground.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pamguard::detectors {

SpectrumBackground::SpectrumBackground(double sample_rate_hz,
                                       std::size_t fft_hop,
                                       std::size_t bin_count,
                                       double time_constant_seconds)
    : data_(bin_count, 0.0) {
    if (!(sample_rate_hz > 0.0) || fft_hop == 0 || bin_count == 0 ||
        !(time_constant_seconds > 0.0) ||
        !std::isfinite(sample_rate_hz) ||
        !std::isfinite(time_constant_seconds)) {
        throw std::invalid_argument(
            "spectrum background needs positive finite rate/time constant, hop, and bin count");
    }
    const double delta_seconds =
        static_cast<double>(fft_hop) / sample_rate_hz;
    n_runin_ = static_cast<std::size_t>(
        time_constant_seconds / delta_seconds);
    alpha_ = delta_seconds / time_constant_seconds;
}

const std::vector<double>& SpectrumBackground::process(
    const std::vector<double>& magnitude_squared) {
    if (magnitude_squared.size() != data_.size()) {
        throw std::invalid_argument(
            "spectrum background input width changed");
    }
    ++n_done_;
    double alpha = alpha_;
    if (n_done_ < n_runin_) {
        alpha = 1.0 / static_cast<double>(n_done_);
    }
    const double retained = 1.0 - alpha;
    for (std::size_t bin = 0; bin < data_.size(); ++bin) {
        if (!std::isfinite(magnitude_squared[bin])) {
            continue;
        }
        data_[bin] *= retained;
        data_[bin] += alpha * magnitude_squared[bin];
    }
    return data_;
}

const std::vector<double>& SpectrumBackground::data() const noexcept {
    return data_;
}

std::size_t SpectrumBackground::processed_slices() const noexcept {
    return n_done_;
}

void SpectrumBackground::reset() {
    n_done_ = 0;
    std::fill(data_.begin(), data_.end(), 0.0);
}

} // namespace pamguard::detectors
