#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "pamguard/detectors/IshmaelDetector.h"

namespace pamguard::detectors {

/** SgramCorrParams: segments are {t0, f0, t1, f1} time-frequency lines. */
struct SgramCorrConfig {
    bool enabled = false;
    std::vector<std::array<double, 4>> segments;
    double spread = 100.0;
    bool use_log = false;
    // IshDetParams peak-picking fields, as for the energy sum.
    double thresh = 1.0;
    double min_time_s = 0.0;
    double max_time_s = 99999.0;
    double refractory_time_s = 0.0;
};

/**
 * Port of IshmaelDetector.SgramCorrProcess — the Mellinger & Clark (2000)
 * spectrogram correlation detector. A kernel is built from time-frequency
 * segments (the Mexican-hat function across frequency around each
 * segment's line, summed over segments), incoming FFT slices accumulate in
 * a per-channel circular buffer, and once the buffer holds a kernel's
 * worth of slices every new slice yields the kernel/gram dot product as
 * the detection function — which feeds the SAME IshPeakProcess as the
 * energy sum (IshmaelPeakPicker here).
 *
 * Reference quirks preserved: the kernel's top bin clamps to gramHeight/2
 * (HALF the spectrum, so kernels never extend above sr/4); the 4-standard-
 * deviation spread margin; log mode floors magnitude-squared at 1.0; the
 * packed FFT bin 0 (DC²+Nyquist²) is read like any other bin when
 * binOffset is 0; the detection value emerges only once nFramesIn reaches
 * the kernel length.
 */
class SgramCorrDetector {
public:
    SgramCorrDetector(double sample_rate_hz, std::size_t fft_length, std::size_t fft_hop,
                      const SgramCorrConfig& config);

    /** Kernel time-slice count (durN). */
    [[nodiscard]] std::size_t kernel_length() const noexcept { return kernel_.size(); }
    /** Kernel frequency-bin count (nBin). */
    [[nodiscard]] std::size_t kernel_bins() const noexcept {
        return kernel_.empty() ? 0 : kernel_[0].size();
    }
    [[nodiscard]] int bin_offset() const noexcept { return bin_offset_; }
    [[nodiscard]] const std::vector<std::vector<double>>& kernel() const noexcept { return kernel_; }
    /** Kernel frequency span (getLoFreq/getHiFreq). */
    [[nodiscard]] double min_frequency_hz() const noexcept { return min_f_; }
    [[nodiscard]] double max_frequency_hz() const noexcept { return max_f_; }

    /**
     * Feed one FFT slice's magnitude-squared bins for one channel. Returns
     * the detection-function value once the circular buffer is full.
     */
    std::optional<double> process_frame(std::size_t channel,
                                        const std::vector<double>& magnitude_squared);

private:
    struct PerChannelInfo {
        std::vector<std::vector<double>> stored_gram;
        std::int64_t n_frames_in = 0;
        std::size_t slice_ix = 0;
    };

    SgramCorrConfig config_;
    std::vector<std::vector<double>> kernel_;
    int bin_offset_ = 0;
    double min_f_ = 0.0;
    double max_f_ = 0.0;
    std::vector<PerChannelInfo> channels_;

    void make_kernel(double sample_rate_hz, double frame_rate_hz, int gram_height);
    PerChannelInfo& channel_info(std::size_t channel);
};

} // namespace pamguard::detectors
