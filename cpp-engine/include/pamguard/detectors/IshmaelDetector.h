#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace pamguard::detectors {

// EnergySumParams + the IshDetParams peak-picking fields.
struct IshmaelEnergySumConfig {
    bool enabled = false;
    // Frequency range to sum over (Hz).
    double f0 = 0.0;
    double f1 = 1000.0;
    // Ratio band (Hz), used when use_ratio is set.
    double ratio_f0 = 1000.0;
    double ratio_f1 = 2000.0;
    bool use_ratio = false;
    // useLog: dB-scale sums (and ratio becomes a subtraction).
    bool use_log = false;
    // Adaptive noise floor (EnergySumParams.adaptiveThreshold).
    bool adaptive_threshold = false;
    double long_filter = 0.0001;
    double spike_decay = 100.0;
    // Output smoothing (EnergySumParams.outPutSmoothing/shortFilter).
    bool output_smoothing = false;
    double short_filter = 0.1;
    // IshDetParams: threshold and peak-picker times (seconds).
    double thresh = 1.0;
    double min_time_s = 0.0;
    double max_time_s = 99999.0;
    double refractory_time_s = 0.0;
};

// One detection-function sample (IshDetFnDataUnit.detData column).
struct IshmaelDetSample {
    double det_value = 0.0;       // detData[0][0]: what the peak picker sees
    double noise_floor = 0.0;     // detData[1][0], adaptive only
    double raw_value = 0.0;       // detData[2][0], adaptive only
    bool has_noise_floor = false;
};

// One picked detection (IshDetection equivalent). The reference computes an
// endMsec but IshDetection's constructor discards it, so it is not carried.
struct IshmaelDetection {
    std::size_t channel = 0;
    std::int64_t start_sample = 0;
    std::int64_t duration_samples = 0;
    std::int64_t peak_time_sample = 0;
    double peak_height = 0.0;
    std::int64_t start_time_ms = 0;
    double low_freq_hz = 0.0;
    double high_freq_hz = 0.0;
};

// Port of IshmaelDetector.EnergySumProcess: mean magnitude-squared (or mean
// log10 with the reference's +5 floor hack) over a bin band per FFT slice,
// optional ratio band, optional output smoothing, optional adaptive noise
// floor with spike decay.
//
// Reference quirks preserved: the smoothing state is written ONCE (the first
// result) and never updated, so "smoothing" blends every later result with
// the first one; the noise floor and smoothing state are shared across all
// channels of the instance, exactly as the single EnergySumProcess fields
// are; bin 0 is excluded ("FFT bin 0 has 0's in it"); the -Double.MIN_VALUE
// initialisation sentinels are kept verbatim.
class IshmaelEnergySum {
public:
    IshmaelEnergySum(double sample_rate_hz, const IshmaelEnergySumConfig& config);

    // One FFT slice's magnitude-squared bins (PAMGuard packed convention).
    IshmaelDetSample process_frame(const std::vector<double>& magnitude_squared);

private:
    double sample_rate_hz_;
    IshmaelEnergySumConfig config_;
    int saved_gram_height_ = -1;
    int lo_bin_ = 1;
    int hi_bin_ = 1;
    int lo_bin_ratio_ = 1;
    int hi_bin_ratio_ = 1;
    double noise_floor_;
    double smooth_result_;

    void prepare_params(int len);
    double calc_energy_peak(const std::vector<double>& magnitude_squared, int low_bin, int high_bin) const;
};

// Port of IshmaelDetector.IshPeakProcess: thresholds the detection function,
// requires min duration, caps max duration, applies the refractory gap, and
// reports the event with its peak.
//
// Reference quirks preserved: minTime and refractoryTime convert to
// detection-rate slices but are compared against RAW-sample durations and
// intervals (the reference mixes the units); an event still open at stream
// end is dropped (the reference has no flush).
class IshmaelPeakPicker {
public:
    IshmaelPeakPicker(double sample_rate_hz, std::size_t fft_hop,
                      const IshmaelEnergySumConfig& config);

    // One detection-function value for one channel at the given raw start
    // sample. Returns the completed detection when this value closes one.
    std::optional<IshmaelDetection> process(std::size_t channel, std::int64_t start_sample,
                                            double det_value);

private:
    struct PerChannelInfo {
        std::int64_t start_sample = 0;
        std::int64_t end_sample = 0;
        std::int64_t n_over_thresh = 0;
        std::int64_t n_under_thresh = 0;
        double peak_height = 0.0;
        std::int64_t peak_time_sample = 0;
        std::int64_t last_start_sample = -1;
        std::int64_t last_duration_samples = -1;

        void reset();
    };

    IshmaelEnergySumConfig config_;
    double sample_rate_hz_;
    float det_sample_rate_;
    std::int64_t min_time_n_ = 0;
    std::int64_t max_time_n_ = 0;
    std::int64_t refractory_time_sam_ = 0;
    std::vector<PerChannelInfo> channels_;

    PerChannelInfo& channel_info(std::size_t channel);
};

} // namespace pamguard::detectors
