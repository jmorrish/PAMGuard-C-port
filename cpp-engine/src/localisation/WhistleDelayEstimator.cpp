#include "pamguard/localisation/WhistleDelayEstimator.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace pamguard::localisation {

WhistleDelayEstimator::WhistleDelayEstimator(std::size_t fft_length)
    : fft_length_(fft_length) {
    if (fft_length_ < 4 || (fft_length_ & (fft_length_ - 1)) != 0) {
        throw std::invalid_argument("whistle delay fft_length must be a power of two of at least 4");
    }
    clear();
}

std::size_t WhistleDelayEstimator::fft_length() const noexcept {
    return fft_length_;
}

void WhistleDelayEstimator::clear() {
    packed_cross_spectrum_.assign(fft_length_, 0.0);
    scale_1_ = 0.0;
    scale_2_ = 0.0;
}

void WhistleDelayEstimator::add_fft_data(std::complex<double> channel_1_bin, std::complex<double> channel_2_bin,
                                         std::size_t bin_index) {
    if (bin_index >= fft_length_ / 2) {
        throw std::invalid_argument("whistle delay bin_index must be below fft_length / 2");
    }
    scale_1_ += std::norm(channel_1_bin);
    scale_2_ += std::norm(channel_2_bin);
    // PAMGuard DelayMeasure.addFFTData accumulation: ch1 times conj(ch2).
    packed_cross_spectrum_[bin_index * 2] +=
        channel_1_bin.real() * channel_2_bin.real() + channel_1_bin.imag() * channel_2_bin.imag();
    packed_cross_spectrum_[bin_index * 2 + 1] +=
        channel_1_bin.imag() * channel_2_bin.real() - channel_2_bin.imag() * channel_1_bin.real();
}

TimeDelayData WhistleDelayEstimator::get_delay(double max_delay_samples) const {
    const double scale = std::sqrt(scale_1_ * scale_2_) * 2.0 / static_cast<double>(fft_length_);
    const auto correlation = jtransforms_real_inverse();
    return CorrelationDelayEstimator::interpolated_peak(correlation, scale, max_delay_samples);
}

std::vector<double> WhistleDelayEstimator::jtransforms_real_inverse() const {
    // JTransforms DoubleFFT_1D.realInverse(a, true) semantics on the packed
    // half spectrum: a[0] = Re[0], a[1] = Re[n/2], a[2k] = Re[k],
    // a[2k+1] = Im[k] (forward convention exp(-i 2 pi j k / n)).
    const auto n = fft_length_;
    const double re_0 = packed_cross_spectrum_[0];
    const double re_nyquist = packed_cross_spectrum_[1];
    std::vector<double> output(n, 0.0);
    for (std::size_t j = 0; j < n; ++j) {
        double sum = re_0 + ((j % 2 == 0) ? re_nyquist : -re_nyquist);
        for (std::size_t k = 1; k < n / 2; ++k) {
            const double theta = 2.0 * std::numbers::pi * static_cast<double>(j) * static_cast<double>(k)
                / static_cast<double>(n);
            sum += 2.0 * (packed_cross_spectrum_[k * 2] * std::cos(theta)
                - packed_cross_spectrum_[k * 2 + 1] * std::sin(theta));
        }
        output[j] = sum / static_cast<double>(n);
    }
    return output;
}

} // namespace pamguard::localisation
