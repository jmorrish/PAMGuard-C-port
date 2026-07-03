#pragma once

#include <cstddef>
#include <vector>

#include "pamguard/dsp/RealFft.h"

namespace pamguard::localisation {

struct TimeDelayData {
    double delay_samples = 0.0;
    double delay_score = 0.0;
};

class CorrelationDelayEstimator {
public:
    TimeDelayData estimate_delay(
        const std::vector<double>& signal_1,
        const std::vector<double>& signal_2,
        std::size_t fft_length,
        double max_delay_samples);

    [[nodiscard]] static double parabolic_correction(double y1, double y2, double y3) noexcept;
    [[nodiscard]] static double parabolic_height(double y1, double y2, double y3) noexcept;

private:
    dsp::RealFft fft_;

    [[nodiscard]] static std::vector<double> padded_copy(const std::vector<double>& signal, std::size_t fft_length);
    [[nodiscard]] static std::vector<double> circular_correlation(
        const std::vector<double>& signal_1,
        const std::vector<double>& signal_2);
    [[nodiscard]] static double pamguard_spectral_scale(
        const dsp::ComplexSpectrum& spectrum_1,
        const dsp::ComplexSpectrum& spectrum_2,
        std::size_t fft_length);
    [[nodiscard]] static TimeDelayData interpolated_peak(
        const std::vector<double>& inverse_correlation,
        double scale,
        double max_delay_samples);
};

} // namespace pamguard::localisation
