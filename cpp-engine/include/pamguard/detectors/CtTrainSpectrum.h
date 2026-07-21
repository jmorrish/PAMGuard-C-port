#pragma once

#include <cstddef>
#include <vector>

namespace pamguard::detectors {

/**
 * Port of the spectrum half of PAMGuard's AverageWaveform: each click
 * waveform is zero-padded to the FFT length, transformed, and its magnitude-
 * squared spectrum summed bin-wise across the train.
 *
 * Two reference behaviours preserved:
 * - Spectra are summed, never divided by the click count. The template
 *   classifier L2-normalises before correlating, so the distinction does not
 *   affect classification, but the returned values are sums.
 * - Spectra are summed **without** the time-delay alignment used for the
 *   average waveform. PAMGuard does this deliberately so constructive or
 *   destructive interference in waveform averaging cannot distort the
 *   spectrum.
 *
 * The FFT and magnitude-squared packing are the engine's existing
 * PAMGuard-compatible path, which has click feature fixture parity.
 */
[[nodiscard]] std::vector<double> ct_train_average_spectrum(
    const std::vector<std::vector<double>>& click_waveforms,
    std::size_t fft_length);

} // namespace pamguard::detectors
