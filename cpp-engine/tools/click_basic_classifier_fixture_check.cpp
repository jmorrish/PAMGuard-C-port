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

struct ClassifierFixtureRow {
    std::string case_name;
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

std::vector<ClassifierFixtureRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<ClassifierFixtureRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line == "case,clickType,discard") {
            continue;
        }

        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 3) {
            throw std::runtime_error("fixture row must have case, clickType, and discard columns: " + line);
        }
        rows.push_back(ClassifierFixtureRow{cells[0], std::stoi(cells[1]), cells[2] == "true"});
    }
    if (rows.empty()) {
        throw std::runtime_error("fixture did not contain any case rows");
    }
    return rows;
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

pamguard::detectors::BasicClickTypeConfig selection_only_type(int species_code, std::uint32_t which_selections) {
    auto type = passing_type();
    type.species_code = species_code;
    type.discard = false;
    type.which_selections = which_selections;
    return type;
}

pamguard::detectors::BasicClickTypeConfig band_diff_fail_type() {
    auto type = selection_only_type(8, pamguard::detectors::EnableEnergyBand);
    type.band_energy_difference_db = 15.0;
    return type;
}

pamguard::detectors::BasicClickTypeConfig peak_pos_fail_type() {
    auto type = selection_only_type(11, pamguard::detectors::EnablePeakFreqPos);
    type.peak_frequency_range_hz = pamguard::detectors::FrequencyRange{11000.0, 12000.0};
    return type;
}

pamguard::detectors::BasicClickTypeConfig peak_width_fail_type() {
    auto type = selection_only_type(14, pamguard::detectors::EnablePeakFreqWidth);
    type.peak_width_hz = pamguard::detectors::FrequencyRange{100.0, 500.0};
    return type;
}

pamguard::detectors::BasicClickTypeConfig length_fail_type() {
    auto type = selection_only_type(15, pamguard::detectors::EnableClickLength);
    type.click_length_ms = pamguard::detectors::FrequencyRange{0.05, 0.10};
    return type;
}

pamguard::detectors::BasicClickTypeConfig length_zero_max_type() {
    auto type = selection_only_type(13, pamguard::detectors::EnableClickLength);
    type.click_length_ms = pamguard::detectors::FrequencyRange{0.05, 0.0};
    return type;
}

struct ClassifierCase {
    std::string name;
    std::vector<pamguard::detectors::BasicClickTypeConfig> click_types;
};

// Case catalogue shared by name with the PAMGuard Java fixture exporter
// (reference-tools/.../ClickBasicClassifierFixtureExporter.java).
// Both sides must build identical type lists per case.
std::vector<ClassifierCase> case_catalogue() {
    using pamguard::detectors::EnableMeanFrequency;
    using pamguard::detectors::EnablePeakFreqPos;
    return {
        {"all-pass", {failing_type(), passing_type()}},
        {"band1-range-fail", {failing_type()}},
        {"band-diff-fail", {band_diff_fail_type()}},
        {"peak-pos-only-pass", {selection_only_type(11, EnablePeakFreqPos)}},
        {"peak-pos-only-fail", {peak_pos_fail_type()}},
        {"peak-width-only-fail", {peak_width_fail_type()}},
        {"mean-freq-only-pass", {selection_only_type(12, EnableMeanFrequency)}},
        {"length-only-fail", {length_fail_type()}},
        {"length-zero-max-pass", {length_zero_max_type()}},
        {"order-first-wins", {selection_only_type(21, EnableMeanFrequency), selection_only_type(22, EnablePeakFreqPos)}},
        {"no-selections-pass", {selection_only_type(30, 0)}},
    };
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

        const auto catalogue = case_catalogue();
        if (fixture.size() != catalogue.size()) {
            std::cerr << "Fixture case count mismatch: fixture=" << fixture.size()
                      << " catalogue=" << catalogue.size() << "\n";
            return 1;
        }

        for (std::size_t i = 0; i < catalogue.size(); ++i) {
            const auto& classifier_case = catalogue[i];
            const auto& expected = fixture[i];
            if (expected.case_name != classifier_case.name) {
                std::cerr << "Fixture case order mismatch at row " << i << ": fixture=" << expected.case_name
                          << " catalogue=" << classifier_case.name << "\n";
                return 1;
            }

            pamguard::detectors::BasicClickClassifierConfig config;
            config.sample_rate_hz = sample_rate_hz;
            config.click_types = classifier_case.click_types;

            pamguard::detectors::BasicClickClassifier classifier(config);
            const auto actual = classifier.identify(click);
            if (actual.click_type != expected.click_type || actual.discard != expected.discard) {
                std::cerr << "Basic click classifier parity failed for case " << classifier_case.name << "\n";
                std::cerr << "expected clickType/discard=" << expected.click_type << "/" << expected.discard << "\n";
                std::cerr << "actual   clickType/discard=" << actual.click_type << "/" << actual.discard << "\n";
                return 1;
            }
        }

        std::cout << "Basic click classifier parity passed\n";
        std::cout << "cases=" << catalogue.size() << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
