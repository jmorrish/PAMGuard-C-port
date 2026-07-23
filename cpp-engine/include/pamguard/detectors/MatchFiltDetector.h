#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pamguard::detectors {

/** MatchFiltParams + the kernel waveform (the reference reads a WAV file;
 * the engine carries the samples inline). */
struct MatchFiltConfig {
    bool enabled = false;
    /** Kernel samples, assumed at the session rate — the reference never
     * checks or resamples the kernel file's own rate. */
    std::vector<double> kernel;
    /**
     * Channels to run. Empty means channel 0 only — the reference's
     * behaviour when no channel groups are configured; with groups it runs
     * the FIRST channel of each group.
     */
    std::vector<std::size_t> channels;
    // IshDetParams peak-picking fields.
    double thresh = 1.0;
    double min_time_s = 0.0;
    double max_time_s = 99999.0;
    double refractory_time_s = 0.0;
};

/**
 * Port of IshmaelDetector.MatchFiltProcess2 (the live implementation;
 * MatchFiltProcess v1 is @Deprecated in the reference): overlap-save
 * normalised cross-correlation of raw audio against a kernel waveform.
 * The FFT length is nextBinaryExp(max(0.1 s, 2x kernel)); each full buffer
 * yields fftLength - kernelLength detection samples
 * xCorr[i]/sqrt(kernelEnergy * windowEnergy[i]) with the window energy
 * maintained as a sliding sum, and the buffer keeps a kernel's length of
 * overlap. The values feed the shared Ishmael peak picker per sample, with
 * 0..sr/2 as the reported band.
 *
 * Reference quirks preserved: conjTimes multiplies the PACKED spectra
 * blindly (the packed DC/Nyquist bin included); the kernel file's own
 * sample rate is ignored; the norm slides by subtracting before adding, so
 * a zero-energy window divides by zero into NaN/Inf exactly as Java does.
 */
class MatchFiltDetector {
public:
    /** One buffer's worth of detection-function samples. */
    struct FnBlock {
        std::int64_t start_sample = 0;
        std::vector<double> values;
    };

    MatchFiltDetector(double sample_rate_hz, const MatchFiltConfig& config);

    [[nodiscard]] bool valid() const noexcept { return fft_length_ != 0; }
    [[nodiscard]] std::size_t fft_length() const noexcept { return fft_length_; }
    [[nodiscard]] std::size_t useful_samples() const noexcept { return useful_samples_; }
    [[nodiscard]] double norm_const() const noexcept { return norm_const_; }

    /** Feed raw samples for one channel; returns completed blocks. */
    std::vector<FnBlock> process(std::size_t channel, const std::vector<double>& samples);

private:
    struct ChannelDetector {
        std::vector<double> data_buffer;
        std::size_t buffer_index = 0;
        std::int64_t total_samples = 0;
    };

    MatchFiltConfig config_;
    std::size_t fft_length_ = 0;
    std::size_t useful_samples_ = 0;
    double norm_const_ = 0.0;
    std::vector<double> complex_kernel_; // packed rfft of the padded kernel
    std::vector<ChannelDetector> channels_;

    ChannelDetector& channel_detector(std::size_t channel);
    FnBlock process_buffer(ChannelDetector& chan);
};

} // namespace pamguard::detectors
