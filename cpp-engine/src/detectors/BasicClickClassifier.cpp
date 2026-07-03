#include "pamguard/detectors/BasicClickClassifier.h"

#include <stdexcept>
#include <utility>

namespace pamguard::detectors {

namespace {

bool selection_enabled(std::uint32_t selections, BasicClickSelection selection) {
    return (selections & static_cast<std::uint32_t>(selection)) != 0;
}

bool in_range(double value, FrequencyRange range) {
    return value >= range.low_hz && value <= range.high_hz;
}

ClickFeatureConfig feature_config_for_type(double sample_rate_hz, const BasicClickTypeConfig& type) {
    ClickFeatureConfig config;
    config.sample_rate_hz = sample_rate_hz;
    config.length_energy_fraction = type.length_energy_fraction;
    config.width_energy_fraction = type.width_energy_fraction;
    config.energy_bands_hz = {type.band1_freq_hz, type.band2_freq_hz};
    config.peak_frequency_search_hz = type.peak_frequency_search_hz;
    config.mean_frequency_range_hz = type.mean_sum_range_hz;
    return config;
}

} // namespace

BasicClickTypeConfig standard_basic_click_type(int species_code, BasicClickStandardType standard_type) {
    BasicClickTypeConfig type;
    type.species_code = species_code;
    type.which_selections = EnableEnergyBand | EnableMeanFrequency | EnablePeakFreqPos;
    type.band1_energy_db = FrequencyRange{0.0, 500.0};
    type.band2_energy_db = FrequencyRange{0.0, 500.0};
    type.band_energy_difference_db = 5.0;

    switch (standard_type) {
    case BasicClickStandardType::BeakedWhale:
        type.band2_freq_hz = FrequencyRange{10000.0, 23125.0};
        type.band1_freq_hz = FrequencyRange{25000.0, 40000.0};
        type.peak_frequency_search_hz = FrequencyRange{10000.0, 96000.0};
        type.peak_frequency_range_hz = FrequencyRange{25000.0, 45000.0};
        type.mean_sum_range_hz = FrequencyRange{10000.0, 96000.0};
        type.mean_selection_range_hz = FrequencyRange{25000.0, 45000.0};
        break;
    case BasicClickStandardType::Porpoise:
        type.band2_freq_hz = FrequencyRange{40000.0, 90000.0};
        type.band1_freq_hz = FrequencyRange{100000.0, 150000.0};
        type.peak_frequency_search_hz = FrequencyRange{20000.0, 250000.0};
        type.peak_frequency_range_hz = FrequencyRange{100000.0, 150000.0};
        type.mean_sum_range_hz = FrequencyRange{20000.0, 250000.0};
        type.mean_selection_range_hz = FrequencyRange{100000.0, 150000.0};
        break;
    }

    return type;
}

BasicClickClassifier::BasicClickClassifier(BasicClickClassifierConfig config)
    : config_(std::move(config)) {
    if (config_.sample_rate_hz <= 0.0) {
        throw std::invalid_argument("basic click classifier sample_rate_hz must be positive");
    }
}

const BasicClickClassifierConfig& BasicClickClassifier::config() const noexcept {
    return config_;
}

ClickClassificationResult BasicClickClassifier::identify(const ClickDetectionResult& click) const {
    ClickClassificationResult result;
    result.click_start_sample = click.start_sample;

    for (const auto& type : config_.click_types) {
        if (is_this_type(click, type)) {
            result.click_type = type.species_code;
            result.discard = type.discard;
            return result;
        }
    }

    return result;
}

bool BasicClickClassifier::is_this_type(const ClickDetectionResult& click, const BasicClickTypeConfig& type) const {
    const ClickFeatureExtractor extractor(feature_config_for_type(config_.sample_rate_hz, type));
    const auto features = extractor.extract(click);

    if (selection_enabled(type.which_selections, EnableEnergyBand)) {
        const double band1_energy = features.band_energy_db[0];
        if (band1_energy < type.band1_energy_db.low_hz || band1_energy > type.band1_energy_db.high_hz) {
            return false;
        }
        const double band2_energy = features.band_energy_db[1];
        if (band2_energy < type.band2_energy_db.low_hz || band2_energy > type.band2_energy_db.high_hz) {
            return false;
        }
        if (band1_energy - band2_energy < type.band_energy_difference_db) {
            return false;
        }
    }

    if (selection_enabled(type.which_selections, EnablePeakFreqPos) || selection_enabled(type.which_selections, EnablePeakFreqWidth)) {
        if (selection_enabled(type.which_selections, EnablePeakFreqPos) && !in_range(features.peak_frequency_hz, type.peak_frequency_range_hz)) {
            return false;
        }
        if (selection_enabled(type.which_selections, EnablePeakFreqWidth) && !in_range(features.peak_width_hz, type.peak_width_hz)) {
            return false;
        }
    }

    if (selection_enabled(type.which_selections, EnableMeanFrequency) && !in_range(features.mean_frequency_hz, type.mean_selection_range_hz)) {
        return false;
    }

    if (selection_enabled(type.which_selections, EnableClickLength) && type.click_length_ms.high_hz > 0.0) {
        const double click_length_ms = features.click_length_seconds * 1000.0;
        if (click_length_ms < type.click_length_ms.low_hz || click_length_ms > type.click_length_ms.high_hz) {
            return false;
        }
    }

    return true;
}

} // namespace pamguard::detectors
