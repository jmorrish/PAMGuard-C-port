#pragma once

#include <span>
#include <stdexcept>
#include <vector>

namespace pamguard::dsp {

class WavInterpolatorError final : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

/**
 * Exact operational port of jpamutils 1.0.0 WavInterpolator.decimate used by
 * PAMGuard's matched-template classifier:
 *
 *  1. causal four-pole iirj Butterworth low-pass at targetRate / 2;
 *  2. natural cubic-spline interpolation over x=i/(N-1);
 *  3. output length int(N*target/source), sampled at x=i/outputLength.
 */
[[nodiscard]] std::vector<double> wav_interpolator_decimate(
    std::span<const double> input,
    double source_rate_hz,
    double target_rate_hz);

} // namespace pamguard::dsp
