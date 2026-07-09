#pragma once

#include <complex>
#include <cstddef>
#include <vector>

#include "pamguard/localisation/CorrelationDelayEstimator.h"

namespace pamguard::localisation {

/**
 * Port of the DelayMeasure core inside PAMGuard's
 * whistlesAndMoans.WhistleDelays: accumulates the cross spectrum and
 * per-channel powers over a whistle contour's peak bins across slices,
 * then recovers the pair delay via the JTransforms-packed real inverse FFT
 * and PAMGuard's interpolated correlation peak.
 *
 * PAMGuard stores the accumulated bin-zero imaginary part in the packed slot
 * JTransforms reserves for the Nyquist real value; contour bins should not
 * include bin zero (real whistle contours never do), matching the reference.
 */
class WhistleDelayEstimator {
public:
    explicit WhistleDelayEstimator(std::size_t fft_length);

    [[nodiscard]] std::size_t fft_length() const noexcept;

    void clear();

    /** Accumulate one contour bin from a pair of per-channel FFT slices. */
    void add_fft_data(std::complex<double> channel_1_bin, std::complex<double> channel_2_bin, std::size_t bin_index);

    /** PAMGuard DelayMeasure.getDelay: peak of the scaled inverse correlation. */
    [[nodiscard]] TimeDelayData get_delay(double max_delay_samples) const;

private:
    std::size_t fft_length_ = 0;
    std::vector<double> packed_cross_spectrum_;
    double scale_1_ = 0.0;
    double scale_2_ = 0.0;

    [[nodiscard]] std::vector<double> jtransforms_real_inverse() const;
};

} // namespace pamguard::localisation
