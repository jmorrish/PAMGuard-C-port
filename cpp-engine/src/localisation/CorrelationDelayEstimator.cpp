#include "pamguard/localisation/CorrelationDelayEstimator.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pamguard::localisation {

TimeDelayData CorrelationDelayEstimator::estimate_delay(
    const std::vector<double>& signal_1,
    const std::vector<double>& signal_2,
    std::size_t fft_length,
    double max_delay_samples) {
    if (fft_length == 0) {
        throw std::invalid_argument("fft_length must be non-zero");
    }
    if (max_delay_samples < 0.0) {
        throw std::invalid_argument("max_delay_samples must be non-negative");
    }

    const auto padded_1 = padded_copy(signal_1, fft_length);
    const auto padded_2 = padded_copy(signal_2, fft_length);
    const auto spectrum_1 = fft_.forward(padded_1, fft_length);
    const auto spectrum_2 = fft_.forward(padded_2, fft_length);
    const auto scale = pamguard_spectral_scale(spectrum_1, spectrum_2, fft_length);
    const auto xcorr = circular_correlation(padded_1, padded_2);
    return interpolated_peak(xcorr, scale, max_delay_samples);
}

double CorrelationDelayEstimator::parabolic_correction(double y1, double y2, double y3) noexcept {
    const double bottom = 2.0 * y2 - y1 - y3;
    if (bottom == 0.0) {
        return 0.0;
    }
    return 0.5 * (y3 - y1) / bottom;
}

double CorrelationDelayEstimator::parabolic_height(double y1, double y2, double y3) noexcept {
    const double t = parabolic_correction(y1, y2, y3);
    const double a = (y1 + y3 - 2.0 * y2) / 2.0;
    const double b = (y3 - y1) / 2.0;
    const double c = y2;
    return a * t * t + b * t + c;
}

std::vector<double> CorrelationDelayEstimator::padded_copy(const std::vector<double>& signal, std::size_t fft_length) {
    std::vector<double> padded(fft_length, 0.0);
    const auto n = std::min(signal.size(), fft_length);
    std::copy_n(signal.begin(), n, padded.begin());
    return padded;
}

std::vector<double> CorrelationDelayEstimator::circular_correlation(
    const std::vector<double>& signal_1,
    const std::vector<double>& signal_2) {
    const auto n = signal_1.size();
    if (signal_2.size() != n) {
        throw std::invalid_argument("signals must have equal padded length");
    }

    std::vector<double> correlation(n, 0.0);
    for (std::size_t lag = 0; lag < n; ++lag) {
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            sum += signal_1[(i + lag) % n] * signal_2[i];
        }
        correlation[lag] = sum;
    }
    return correlation;
}

double CorrelationDelayEstimator::pamguard_spectral_scale(
    const dsp::ComplexSpectrum& spectrum_1,
    const dsp::ComplexSpectrum& spectrum_2,
    std::size_t fft_length) {
    if (spectrum_1.size() != spectrum_2.size() || spectrum_1.size() != (fft_length / 2 + 1)) {
        throw std::invalid_argument("spectra do not match fft_length");
    }

    double scale_1 = std::norm(spectrum_1[0]) + std::norm(spectrum_1[fft_length / 2]);
    double scale_2 = std::norm(spectrum_2[0]) + std::norm(spectrum_2[fft_length / 2]);
    for (std::size_t i = 1; i < fft_length / 2; ++i) {
        scale_1 += std::norm(spectrum_1[i]);
        scale_2 += std::norm(spectrum_2[i]);
    }

    return std::sqrt(scale_1 * scale_2) * 2.0 / static_cast<double>(fft_length);
}

TimeDelayData CorrelationDelayEstimator::interpolated_peak(
    const std::vector<double>& inverse_correlation,
    double scale,
    double max_delay_samples) {
    const auto fft_length = inverse_correlation.size();
    auto max_corr_len = static_cast<std::size_t>(std::ceil(max_delay_samples) + 2.0);
    max_corr_len = std::min(fft_length / 2, max_corr_len);

    std::vector<double> linear_correlation(max_corr_len * 2, 0.0);
    for (std::size_t i = 0; i < max_corr_len; ++i) {
        linear_correlation[i + max_corr_len] = inverse_correlation[i];
        linear_correlation[max_corr_len - 1 - i] = inverse_correlation[fft_length - 1 - i];
    }

    double peak_position = 0.0;
    double peak_height = 0.0;
    bool found_peak = false;
    for (std::size_t i = 1; i + 1 < linear_correlation.size(); ++i) {
        if (linear_correlation[i] > linear_correlation[i - 1] && linear_correlation[i] > linear_correlation[i + 1]) {
            const double current_peak = parabolic_height(linear_correlation[i - 1], linear_correlation[i], linear_correlation[i + 1]);
            if (current_peak > peak_height) {
                peak_position = static_cast<double>(i) + parabolic_correction(linear_correlation[i - 1], linear_correlation[i], linear_correlation[i + 1]);
                peak_height = current_peak;
                found_peak = true;
            }
        }
    }

    if (!found_peak) {
        return {};
    }

    if (scale != 0.0) {
        peak_height /= scale;
    }
    peak_position -= static_cast<double>(max_corr_len);
    peak_position = -peak_position;
    peak_position = std::max(-max_delay_samples, std::min(max_delay_samples, peak_position));

    TimeDelayData data;
    data.delay_samples = peak_position;
    data.delay_score = peak_height;
    return data;
}

} // namespace pamguard::localisation
