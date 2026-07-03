#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pamguard/detectors/ClickDetectorEngine.h"

namespace pamguard::detectors {

struct FrequencyRange {
    double low_hz = 0.0;
    double high_hz = 0.0;
};

struct ClickFeatureConfig {
    double sample_rate_hz = 0.0;
    std::size_t fft_length = 0;
    double length_energy_fraction = 90.0;
    double width_energy_fraction = 90.0;
    std::vector<FrequencyRange> energy_bands_hz;
    FrequencyRange peak_frequency_search_hz;
    FrequencyRange mean_frequency_range_hz;
};

struct ClickChannelFeatures {
    std::size_t channel = 0;
    double length_seconds = 0.0;
    std::vector<double> power_spectrum;
};

struct ClickFeatureResult {
    std::size_t click_index = 0;
    std::int64_t click_start_sample = 0;
    std::size_t fft_length = 0;
    double click_length_seconds = 0.0;
    double peak_frequency_hz = 0.0;
    double peak_width_hz = 0.0;
    double mean_frequency_hz = 0.0;
    std::vector<double> band_energy_db;
    std::vector<double> total_power_spectrum;
    std::vector<ClickChannelFeatures> channels;
};

class ClickFeatureExtractor {
public:
    explicit ClickFeatureExtractor(ClickFeatureConfig config);

    [[nodiscard]] const ClickFeatureConfig& config() const noexcept;
    [[nodiscard]] ClickFeatureResult extract(const ClickDetectionResult& click) const;

private:
    ClickFeatureConfig config_;
};

} // namespace pamguard::detectors
