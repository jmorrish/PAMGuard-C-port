#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>

#include "pamguard/detectors/BasicClickClassifier.h"

namespace {

struct ClassifierFixture {
    int click_type = 0;
    bool discard = false;
};

constexpr double sample_rate_hz = 48000.0;

std::vector<std::vector<double>> synthetic_waveform() {
    constexpr std::size_t length = 96;
    std::vector<std::vector<double>> waveform(2, std::vector<double>(length, 0.0));
    for (std::size_t i = 0; i < length; ++i) {
        const auto x = static_cast<double>(i);
        const double env = std::exp(-0.5 * std::pow((x - 42.0) / 7.0, 2.0));
        waveform[0][i] = 0.03 * std::sin(x * 0.19)
            + env * (std::sin(2.0 * std::numbers::pi * 9000.0 * x / sample_rate_hz)
            + 0.45 * std::sin(2.0 * std::numbers::pi * 14000.0 * x / sample_rate_hz));
        waveform[1][i] = 0.02 * std::cos(x * 0.11 + 0.2)
            + 0.82 * env * (std::sin(2.0 * std::numbers::pi * 9200.0 * x / sample_rate_hz + 0.4)
            + 0.25 * std::sin(2.0 * std::numbers::pi * 15000.0 * x / sample_rate_hz));
    }
    return waveform;
}

ClassifierFixture read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::string header;
    std::string line;
    std::getline(input, header);
    std::getline(input, line);
    if (line.empty()) {
        throw std::runtime_error("fixture did not contain a data row");
    }

    std::stringstream stream(line);
    std::string cell;
    std::vector<std::string> cells;
    while (std::getline(stream, cell, ',')) {
        cells.push_back(cell);
    }
    if (cells.size() != 2) {
        throw std::runtime_error("fixture row must have clickType and discard columns");
    }

    return ClassifierFixture{std::stoi(cells[0]), cells[1] == "true"};
}

pamguard::detectors::BasicClickTypeConfig passing_type() {
    using pamguard::detectors::FrequencyRange;
    pamguard::detectors::BasicClickTypeConfig type;
    type.species_code = 42;
    type.discard = true;
    type.which_selections = pamguard::detectors::EnableEnergyBand |
        pamguard::detectors::EnablePeakFreqWidth |
        pamguard::detectors::EnablePeakFreqPos |
        pamguard::detectors::EnableMeanFrequency |
        pamguard::detectors::EnableClickLength;
    type.band1_freq_hz = FrequencyRange{6000.0, 12000.0};
    type.band2_freq_hz = FrequencyRange{12000.0, 18000.0};
    type.band1_energy_db = FrequencyRange{198.0, 201.0};
    type.band2_energy_db = FrequencyRange{190.0, 193.0};
    type.band_energy_difference_db = 5.0;
    type.peak_frequency_search_hz = FrequencyRange{3000.0, 20000.0};
    type.peak_frequency_range_hz = FrequencyRange{8500.0, 9500.0};
    type.peak_width_hz = FrequencyRange{2500.0, 3500.0};
    type.width_energy_fraction = 80.0;
    type.mean_sum_range_hz = FrequencyRange{3000.0, 20000.0};
    type.mean_selection_range_hz = FrequencyRange{9500.0, 10000.0};
    type.click_length_ms = FrequencyRange{0.30, 0.38};
    type.length_energy_fraction = 90.0;
    return type;
}

pamguard::detectors::BasicClickTypeConfig failing_type() {
    auto type = passing_type();
    type.species_code = 7;
    type.discard = false;
    type.band1_energy_db = pamguard::detectors::FrequencyRange{210.0, 220.0};
    return type;
}

bool standard_presets_match_pamguard_source() {
    const auto beaked = pamguard::detectors::standard_basic_click_type(17, pamguard::detectors::BasicClickStandardType::BeakedWhale);
    const auto porpoise = pamguard::detectors::standard_basic_click_type(23, pamguard::detectors::BasicClickStandardType::Porpoise);
    return beaked.species_code == 17 &&
        beaked.which_selections == (pamguard::detectors::EnableEnergyBand | pamguard::detectors::EnableMeanFrequency | pamguard::detectors::EnablePeakFreqPos) &&
        beaked.band2_freq_hz.low_hz == 10000.0 &&
        beaked.band2_freq_hz.high_hz == 23125.0 &&
        beaked.band1_freq_hz.low_hz == 25000.0 &&
        beaked.band1_freq_hz.high_hz == 40000.0 &&
        beaked.peak_frequency_search_hz.high_hz == 96000.0 &&
        beaked.peak_frequency_range_hz.low_hz == 25000.0 &&
        beaked.mean_selection_range_hz.high_hz == 45000.0 &&
        porpoise.species_code == 23 &&
        porpoise.band2_freq_hz.low_hz == 40000.0 &&
        porpoise.band2_freq_hz.high_hz == 90000.0 &&
        porpoise.band1_freq_hz.low_hz == 100000.0 &&
        porpoise.band1_freq_hz.high_hz == 150000.0 &&
        porpoise.peak_frequency_search_hz.high_hz == 250000.0 &&
        porpoise.peak_frequency_range_hz.low_hz == 100000.0 &&
        porpoise.mean_selection_range_hz.high_hz == 150000.0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: click_basic_classifier_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);
        if (!standard_presets_match_pamguard_source()) {
            std::cerr << "Standard basic click classifier preset values do not match PAMGuard source constants\n";
            return 1;
        }

        pamguard::detectors::ClickDetectionResult click;
        click.start_sample = 0;
        click.duration_samples = 96;
        click.channels = {0, 1};
        click.waveform = synthetic_waveform();

        bool rejected_bad_config = false;
        try {
            pamguard::detectors::BasicClickClassifierConfig bad_config;
            bad_config.sample_rate_hz = 0.0;
            pamguard::detectors::BasicClickClassifier bad_classifier(bad_config);
            (void)bad_classifier;
        }
        catch (const std::invalid_argument&) {
            rejected_bad_config = true;
        }
        if (!rejected_bad_config) {
            std::cerr << "Basic click classifier should reject non-positive sample rate\n";
            return 1;
        }

        pamguard::detectors::BasicClickClassifierConfig empty_config;
        empty_config.sample_rate_hz = sample_rate_hz;
        pamguard::detectors::BasicClickClassifier empty_classifier(empty_config);
        const auto empty_result = empty_classifier.identify(click);
        if (empty_result.click_type != 0 || empty_result.discard || empty_result.click_start_sample != click.start_sample) {
            std::cerr << "Empty basic click classifier should return default no-type result\n";
            return 1;
        }

        pamguard::detectors::BasicClickClassifierConfig failing_config;
        failing_config.sample_rate_hz = sample_rate_hz;
        failing_config.click_types = {failing_type()};
        pamguard::detectors::BasicClickClassifier failing_classifier(failing_config);
        const auto failing_result = failing_classifier.identify(click);
        if (failing_result.click_type != 0 || failing_result.discard) {
            std::cerr << "Non-matching basic click classifier should return default no-type result\n";
            return 1;
        }

        pamguard::detectors::BasicClickClassifierConfig config;
        config.sample_rate_hz = sample_rate_hz;
        config.click_types = {failing_type(), passing_type()};

        pamguard::detectors::BasicClickClassifier classifier(config);
        const auto actual = classifier.identify(click);
        if (actual.click_type != fixture.click_type || actual.discard != fixture.discard) {
            std::cerr << "Basic click classifier parity failed\n";
            std::cerr << "expected clickType/discard=" << fixture.click_type << "/" << fixture.discard << "\n";
            std::cerr << "actual   clickType/discard=" << actual.click_type << "/" << actual.discard << "\n";
            return 1;
        }

        std::cout << "Basic click classifier parity passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
