#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pamguard/detectors/BasicClickClassifier.h"
#include "pamguard/detectors/ClickDetectorEngine.h"

namespace pamguard::detectors {

enum class SweepChannelChoice {
    RequireAll = 0,
    RequireOne = 1,
    UseMeans = 2,
};

enum class SweepRestrictedBinType {
    ClickCenter = 0,
    ClickStart = 1,
};

enum class SweepFftFilterBand {
    HighPass,
    LowPass,
    BandPass,
    BandStop,
};

struct SweepRange {
    double low = 0.0;
    double high = 0.0;
};

struct SweepFftFilterConfig {
    SweepFftFilterBand band = SweepFftFilterBand::HighPass;
    double low_pass_freq_hz = 0.0;
    double high_pass_freq_hz = 0.0;
};

/**
 * Field-for-field runtime counterpart of SweepClassifierSet in PAMGuard
 * 2.02.18e. Units match the Java settings: Hz, milliseconds, dB and radians.
 */
struct SweepClickTypeConfig {
    std::string name;
    int species_code = 0;
    bool discard = false;
    bool enabled = true;

    SweepChannelChoice channel_choice = SweepChannelChoice::RequireAll;

    bool restrict_length = true;
    int restricted_bins = 128;
    SweepRestrictedBinType restricted_bin_type = SweepRestrictedBinType::ClickCenter;

    bool enable_length = true;
    int length_smoothing = 5;
    double length_db = 6.0;
    SweepRange length_ms{0.0, 1.0};

    bool enable_energy_bands = false;
    SweepRange test_energy_band_hz;
    SweepRange control_energy_band_0_hz;
    SweepRange control_energy_band_1_hz;
    double energy_threshold_0_db = 0.0;
    double energy_threshold_1_db = 0.0;

    bool test_amplitude = false;
    SweepRange amplitude_range_db;

    bool enable_fft_filter = false;
    SweepFftFilterConfig fft_filter;

    bool enable_peak = false;
    bool enable_width = false;
    bool enable_mean = false;
    SweepRange peak_search_range_hz;
    SweepRange peak_range_hz;
    SweepRange peak_width_range_hz;
    SweepRange mean_range_hz;
    int peak_smoothing = 5;
    double peak_width_threshold_db = 6.0;

    bool enable_zero_crossings = false;
    SweepRange zero_crossing_count;
    bool enable_sweep = false;
    SweepRange zero_crossing_sweep_khz_per_ms;

    bool enable_min_cross_correlation = false;
    bool enable_peak_cross_correlation = false;
    double min_correlation = 0.0;
    double correlation_factor = 1.0;

    bool enable_bearing_limits = false;
    bool exclude_bearing_limits = false;
    SweepRange bearing_limits_radians{-3.14159265358979323846, 3.14159265358979323846};
};

struct SweepClickClassifierConfig {
    double sample_rate_hz = 0.0;
    bool check_all_classifiers = false;
    /**
     * Added to 20*log10(rotation-corrected peak) for each physical channel.
     * AnalysisSession derives this from the acquisition/hydrophone calibration.
     */
    std::vector<double> amplitude_db_offset_by_channel;
    std::vector<SweepClickTypeConfig> click_types;
};

[[nodiscard]] SweepClickTypeConfig standard_sweep_click_type(
    int species_code, BasicClickStandardType standard_type);

class SweepClickClassifier {
public:
    explicit SweepClickClassifier(SweepClickClassifierConfig config);

    [[nodiscard]] const SweepClickClassifierConfig& config() const noexcept;
    [[nodiscard]] ClickClassificationResult identify(const ClickDetectionResult& click) const;

private:
    SweepClickClassifierConfig config_;

    [[nodiscard]] bool classify(const ClickDetectionResult& click,
                                const SweepClickTypeConfig& type) const;
};

} // namespace pamguard::detectors
