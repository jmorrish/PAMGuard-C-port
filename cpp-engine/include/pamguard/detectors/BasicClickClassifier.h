#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pamguard/detectors/ClickDetectorEngine.h"
#include "pamguard/detectors/ClickFeatureExtractor.h"

namespace pamguard::detectors {

enum BasicClickSelection : std::uint32_t {
    EnableEnergyBand = 0x1,
    EnablePeakFreqWidth = 0x2,
    EnablePeakFreqPos = 0x4,
    EnableMeanFrequency = 0x8,
    EnableClickLength = 0x10
};

enum class BasicClickStandardType {
    BeakedWhale = 0,
    Porpoise = 1
};

struct BasicClickTypeConfig {
    int species_code = 0;
    bool discard = false;
    std::uint32_t which_selections = EnableEnergyBand | EnablePeakFreqPos;
    FrequencyRange band1_freq_hz;
    FrequencyRange band2_freq_hz;
    FrequencyRange band1_energy_db;
    FrequencyRange band2_energy_db;
    double band_energy_difference_db = 0.0;
    FrequencyRange peak_frequency_search_hz;
    FrequencyRange peak_frequency_range_hz;
    FrequencyRange peak_width_hz;
    double width_energy_fraction = 90.0;
    FrequencyRange mean_sum_range_hz;
    FrequencyRange mean_selection_range_hz;
    FrequencyRange click_length_ms;
    double length_energy_fraction = 90.0;
};

[[nodiscard]] BasicClickTypeConfig standard_basic_click_type(int species_code, BasicClickStandardType standard_type);

struct BasicClickClassifierConfig {
    double sample_rate_hz = 0.0;
    std::vector<BasicClickTypeConfig> click_types;
};

struct ClickClassificationResult {
    std::size_t click_index = 0;
    std::int64_t click_start_sample = 0;
    int click_type = 0;
    bool discard = false;
    /** SweepClassifierParameters.checkAllClassifiers output, in set order. */
    std::vector<int> classifiers_passed;
};

class BasicClickClassifier {
public:
    explicit BasicClickClassifier(BasicClickClassifierConfig config);

    [[nodiscard]] const BasicClickClassifierConfig& config() const noexcept;
    [[nodiscard]] ClickClassificationResult identify(const ClickDetectionResult& click) const;

private:
    BasicClickClassifierConfig config_;

    [[nodiscard]] bool is_this_type(const ClickDetectionResult& click, const BasicClickTypeConfig& type) const;
};

} // namespace pamguard::detectors
