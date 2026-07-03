#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/ClickFeatureExtractor.h"

namespace {

struct FeatureFixture {
    std::size_t fft_length = 0;
    double band0_db = 0.0;
    double band1_db = 0.0;
    double peak_frequency_hz = 0.0;
    double peak_width_hz = 0.0;
    double mean_frequency_hz = 0.0;
    double click_length_seconds = 0.0;
    double channel0_length_seconds = 0.0;
    double channel1_length_seconds = 0.0;
    std::vector<double> total_power_prefix;
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

FeatureFixture read_fixture(const std::string& path) {
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
    if (cells.size() < 10) {
        throw std::runtime_error("fixture row has too few columns");
    }

    FeatureFixture fixture;
    fixture.fft_length = static_cast<std::size_t>(std::stoull(cells[0]));
    fixture.band0_db = std::stod(cells[1]);
    fixture.band1_db = std::stod(cells[2]);
    fixture.peak_frequency_hz = std::stod(cells[3]);
    fixture.peak_width_hz = std::stod(cells[4]);
    fixture.mean_frequency_hz = std::stod(cells[5]);
    fixture.click_length_seconds = std::stod(cells[6]);
    fixture.channel0_length_seconds = std::stod(cells[7]);
    fixture.channel1_length_seconds = std::stod(cells[8]);
    for (std::size_t i = 9; i < cells.size(); ++i) {
        fixture.total_power_prefix.push_back(std::stod(cells[i]));
    }
    return fixture;
}

double max_error(const pamguard::detectors::ClickFeatureResult& actual, const FeatureFixture& expected) {
    double error = 0.0;
    error = std::max(error, std::abs(actual.band_energy_db[0] - expected.band0_db));
    error = std::max(error, std::abs(actual.band_energy_db[1] - expected.band1_db));
    error = std::max(error, std::abs(actual.peak_frequency_hz - expected.peak_frequency_hz));
    error = std::max(error, std::abs(actual.peak_width_hz - expected.peak_width_hz));
    error = std::max(error, std::abs(actual.mean_frequency_hz - expected.mean_frequency_hz));
    error = std::max(error, std::abs(actual.click_length_seconds - expected.click_length_seconds));
    error = std::max(error, std::abs(actual.channels[0].length_seconds - expected.channel0_length_seconds));
    error = std::max(error, std::abs(actual.channels[1].length_seconds - expected.channel1_length_seconds));
    for (std::size_t i = 0; i < expected.total_power_prefix.size(); ++i) {
        error = std::max(error, std::abs(actual.total_power_spectrum[i] - expected.total_power_prefix[i]));
    }
    return error;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: click_feature_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);

        pamguard::detectors::ClickDetectionResult click;
        click.start_sample = 0;
        click.duration_samples = 96;
        click.channels = {0, 1};
        click.waveform = synthetic_waveform();

        bool rejected_bad_sample_rate = false;
        try {
            pamguard::detectors::ClickFeatureConfig bad_config;
            bad_config.sample_rate_hz = 0.0;
            pamguard::detectors::ClickFeatureExtractor bad_extractor(bad_config);
            (void)bad_extractor;
        }
        catch (const std::invalid_argument&) {
            rejected_bad_sample_rate = true;
        }
        if (!rejected_bad_sample_rate) {
            std::cerr << "Click feature extractor should reject non-positive sample rate\n";
            return 1;
        }

        bool rejected_bad_fft_length = false;
        try {
            pamguard::detectors::ClickFeatureConfig bad_config;
            bad_config.sample_rate_hz = sample_rate_hz;
            bad_config.fft_length = 100;
            pamguard::detectors::ClickFeatureExtractor bad_extractor(bad_config);
            (void)bad_extractor;
        }
        catch (const std::invalid_argument&) {
            rejected_bad_fft_length = true;
        }
        if (!rejected_bad_fft_length) {
            std::cerr << "Click feature extractor should reject non-power-of-two FFT length\n";
            return 1;
        }

        pamguard::detectors::ClickFeatureConfig config;
        config.sample_rate_hz = sample_rate_hz;
        config.length_energy_fraction = 90.0;
        config.width_energy_fraction = 80.0;
        config.energy_bands_hz = {
            pamguard::detectors::FrequencyRange{6000.0, 12000.0},
            pamguard::detectors::FrequencyRange{12000.0, 18000.0},
        };
        config.peak_frequency_search_hz = pamguard::detectors::FrequencyRange{3000.0, 20000.0};
        config.mean_frequency_range_hz = pamguard::detectors::FrequencyRange{3000.0, 20000.0};

        pamguard::detectors::ClickFeatureExtractor extractor(config);
        bool rejected_empty_waveform = false;
        try {
            pamguard::detectors::ClickDetectionResult empty_click;
            (void)extractor.extract(empty_click);
        }
        catch (const std::invalid_argument&) {
            rejected_empty_waveform = true;
        }
        if (!rejected_empty_waveform) {
            std::cerr << "Click feature extractor should reject empty waveforms\n";
            return 1;
        }

        pamguard::detectors::ClickDetectionResult minimal_click;
        minimal_click.start_sample = 123;
        minimal_click.duration_samples = 3;
        minimal_click.waveform = {{1.0, 0.0, 0.0}};
        const auto minimal = extractor.extract(minimal_click);
        if (minimal.fft_length != 4 || minimal.channels.size() != 1 || minimal.channels[0].channel != 0 ||
            minimal.click_start_sample != 123) {
            std::cerr << "Click feature extractor minimal waveform defaults mismatch\n";
            return 1;
        }

        const auto actual = extractor.extract(click);
        if (actual.fft_length != fixture.fft_length || actual.band_energy_db.size() != 2 || actual.channels.size() != 2) {
            std::cerr << "Click feature metadata mismatch\n";
            return 1;
        }

        const double error = max_error(actual, fixture);
        constexpr double tolerance = 1e-8;
        if (error > tolerance) {
            std::cerr << "Click feature parity failed\n";
            std::cerr << "max_abs_error=" << error << "\n";
            return 1;
        }

        std::cout << "Click feature parity passed\n";
        std::cout << "max_abs_error=" << error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
