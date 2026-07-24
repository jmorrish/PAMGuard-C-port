#pragma once

#include <cstddef>
#include <vector>

namespace pamguard::detectors {

/**
 * Spectrogram.SpectrumBackground: a per-bin decaying mean of raw FFT
 * magnitude-squared values, including Java's initial running-mean period.
 */
class SpectrumBackground {
public:
    SpectrumBackground(double sample_rate_hz, std::size_t fft_hop,
                       std::size_t bin_count, double time_constant_seconds = 10.0);

    const std::vector<double>& process(const std::vector<double>& magnitude_squared);
    [[nodiscard]] const std::vector<double>& data() const noexcept;
    [[nodiscard]] std::size_t processed_slices() const noexcept;
    void reset();

private:
    std::size_t n_runin_ = 0;
    std::size_t n_done_ = 0;
    double alpha_ = 0.0;
    std::vector<double> data_;
};

} // namespace pamguard::detectors
