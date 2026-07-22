#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace pamguard::detectors {

/**
 * Port of PAMGuard's spectrogramNoiseReduction chain — the four
 * SpecNoiseMethods run in SpectrogramNoiseProcess's fixed order over a copy of
 * each FFT slice: median filter, average subtraction, Gaussian kernel
 * smoothing, threshold. The whistle/moan detector consumes this chain's
 * output.
 *
 * Threshold output modes match SpectrogramThreshold: OUTPUT_BINARY writes
 * (1, 0) into surviving bins; OUTPUT_INPUT leaves the chain's values;
 * OUTPUT_RAW (the default) then copies the **un-noise-reduced input** back
 * into surviving bins via pickEarlierData, so downstream sees raw data where
 * something was detected and zeros elsewhere.
 */
struct SpectrogramNoiseConfig {
    bool run_median_filter = false;
    /** MedianFilterParams.filterLength; evenized lengths gain one, as the reference does. */
    int median_filter_length = 61;
    bool run_average_subtraction = false;
    /** AverageSubtractionParameters.updateConstant. */
    double average_update_constant = 0.02;
    bool run_kernel_smoothing = false;
    bool run_threshold = false;
    /** ThresholdParams.thresholdDB, applied as magsq < 10^(dB/10). */
    double threshold_db = 8.0;
    static constexpr int kOutputBinary = 0;
    static constexpr int kOutputInput = 1;
    static constexpr int kOutputRaw = 2;
    int threshold_final_output = kOutputRaw;
};

class SpectrogramNoiseReducer {
public:
    explicit SpectrogramNoiseReducer(SpectrogramNoiseConfig config);

    /** True when any method is switched on. */
    [[nodiscard]] bool active() const noexcept;

    /**
     * Process one FFT slice for one channel, returning the noise-reduced
     * slice. State (average, kernel history) is per channel; `channel` must be
     * < the channel count given at construction... channels are lazily grown.
     */
    [[nodiscard]] std::vector<std::complex<double>> process(std::size_t channel,
                                                            const std::vector<std::complex<double>>& slice);

private:
    struct ChannelState {
        /** AverageSubtraction.channelStorage + totalSlices. */
        std::vector<double> average_log;
        std::size_t average_total_slices = 0;
        /** KernelSmoothing.ChannelProcess: three columns of magsq (+pad) and complex. */
        std::vector<std::vector<double>> kernel_store;
        std::vector<std::vector<std::complex<double>>> kernel_complex;
        std::vector<bool> kernel_store_valid;
    };

    void median_filter(std::vector<std::complex<double>>& slice) const;
    void average_subtraction(ChannelState& state, std::vector<std::complex<double>>& slice) const;
    bool kernel_smoothing(ChannelState& state, std::vector<std::complex<double>>& slice) const;
    void threshold(std::vector<std::complex<double>>& slice) const;

    SpectrogramNoiseConfig config_;
    double power_threshold_ = 0.0;
    std::vector<ChannelState> channels_;
};

/**
 * Port of whistlesAndMoans.MedianFilter (Paul White's medfilt_prw_c.c):
 * running median with edge padding from the data itself, a descending-order
 * bubble sort — the comment says descending, the comparison sorts ascending;
 * behaviour, not comments, is what is ported — and incremental insert/delete.
 */
void pamguard_median_filter(const std::vector<double>& input, std::vector<double>& output, int filter_length);

} // namespace pamguard::detectors
