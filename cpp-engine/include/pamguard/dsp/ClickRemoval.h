#pragma once

#include <vector>

namespace pamguard::dsp {

/**
 * Port of fftManager.ClickRemoval.
 *
 * PAMGuard computes the sample standard deviation (n - 1), then attenuates
 * each sample with:
 *
 *   1 / (1 + ((sample - mean) / (threshold * stddev)) ^ power)
 */
[[nodiscard]] std::vector<double> remove_clicks(
    const std::vector<double>& source,
    double threshold,
    double power);

} // namespace pamguard::dsp
