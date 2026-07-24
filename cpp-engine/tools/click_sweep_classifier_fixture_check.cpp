#include "pamguard/detectors/SweepClickClassifier.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using pamguard::detectors::BasicClickStandardType;
using pamguard::detectors::ClickDetectionResult;
using pamguard::detectors::SweepChannelChoice;
using pamguard::detectors::SweepClickClassifier;
using pamguard::detectors::SweepClickClassifierConfig;
using pamguard::detectors::SweepClickTypeConfig;
using pamguard::detectors::SweepFftFilterBand;
using pamguard::detectors::standard_sweep_click_type;

namespace {

std::vector<std::string> split(const std::string& line, char delimiter = ',') {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, delimiter)) fields.push_back(field);
    if (!line.empty() && line.back() == delimiter) fields.emplace_back();
    return fields;
}

SweepClickTypeConfig base(int species) {
    SweepClickTypeConfig type;
    type.name = "fixture-" + std::to_string(species);
    type.species_code = species;
    type.enable_length = false;
    type.restrict_length = true;
    type.restricted_bins = 128;
    type.length_smoothing = 5;
    type.length_db = 6.0;
    type.enable_bearing_limits = true;
    return type;
}

SweepClickTypeConfig pass(int species) {
    auto type = base(species);
    type.discard = true;
    type.enable_length = true;
    type.length_ms = {0.0, 100.0};
    type.test_amplitude = true;
    type.amplitude_range_db = {-1000.0, 1000.0};
    type.enable_energy_bands = true;
    type.test_energy_band_hz = {7000.0, 12000.0};
    type.control_energy_band_0_hz = {1000.0, 4000.0};
    type.control_energy_band_1_hz = {18000.0, 22000.0};
    type.energy_threshold_0_db = -1000.0;
    type.energy_threshold_1_db = -1000.0;
    type.enable_peak = true;
    type.enable_width = true;
    type.enable_mean = true;
    type.peak_search_range_hz = {1000.0, 23000.0};
    type.peak_range_hz = {0.0, 24000.0};
    type.peak_width_range_hz = {0.0, 24000.0};
    type.mean_range_hz = {0.0, 24000.0};
    type.enable_zero_crossings = true;
    type.zero_crossing_count = {0.0, 1000.0};
    type.enable_sweep = true;
    type.zero_crossing_sweep_khz_per_ms = {-1e9, 1e9};
    type.enable_min_cross_correlation = true;
    type.min_correlation = -1e300;
    type.enable_peak_cross_correlation = true;
    type.correlation_factor = -1.0;
    return type;
}

SweepClickTypeConfig single_failure(const std::string& name, int species) {
    auto type = base(species);
    if (name == "length-fail") {
        type.enable_length = true;
        type.length_ms = {100.0, 200.0};
    }
    else if (name == "amplitude-fail") {
        type.test_amplitude = true;
        type.amplitude_range_db = {100.0, 200.0};
    }
    else if (name == "energy-fail") {
        type.enable_energy_bands = true;
        type.test_energy_band_hz = {7000.0, 12000.0};
        type.control_energy_band_0_hz = {1000.0, 4000.0};
        type.control_energy_band_1_hz = {18000.0, 22000.0};
        type.energy_threshold_0_db = 1000.0;
        type.energy_threshold_1_db = 1000.0;
    }
    else if (name == "peak-fail") {
        type.enable_peak = true;
        type.peak_search_range_hz = {1000.0, 23000.0};
        type.peak_range_hz = {1.0, 100.0};
    }
    else if (name == "width-fail") {
        type.enable_width = true;
        type.peak_search_range_hz = {1000.0, 23000.0};
        type.peak_width_range_hz = {20000.0, 24000.0};
    }
    else if (name == "mean-fail") {
        type.enable_mean = true;
        type.peak_search_range_hz = {1000.0, 23000.0};
        type.mean_range_hz = {1.0, 100.0};
    }
    else if (name == "zero-crossing-fail") {
        type.enable_zero_crossings = true;
        type.zero_crossing_count = {1000.0, 2000.0};
    }
    else if (name == "sweep-fail") {
        type.enable_sweep = true;
        type.zero_crossing_sweep_khz_per_ms = {1e9, 2e9};
    }
    else if (name == "min-correlation-fail") {
        type.enable_min_cross_correlation = true;
        type.min_correlation = 1e300;
    }
    else if (name == "peak-correlation-fail") {
        type.enable_peak_cross_correlation = true;
        type.correlation_factor = 1e300;
    }
    return type;
}

SweepClickTypeConfig amplitude_choice(int species, SweepChannelChoice choice) {
    auto type = base(species);
    type.channel_choice = choice;
    type.test_amplitude = true;
    type.amplitude_range_db = choice == SweepChannelChoice::UseMeans
        ? pamguard::detectors::SweepRange{-12.0, -5.0}
        : pamguard::detectors::SweepRange{-3.0, 3.0};
    return type;
}

std::vector<SweepClickTypeConfig> types_for_case(const std::string& name) {
    if (name == "all-pass") return {single_failure("length-fail", 7), pass(42)};
    if (name == "require-one-pass") return {amplitude_choice(17, SweepChannelChoice::RequireOne)};
    if (name == "require-all-fail") return {amplitude_choice(18, SweepChannelChoice::RequireAll)};
    if (name == "use-means-pass") return {amplitude_choice(19, SweepChannelChoice::UseMeans)};
    if (name == "fft-filter-pass") {
        auto type = pass(20);
        type.enable_fft_filter = true;
        type.fft_filter.band = SweepFftFilterBand::HighPass;
        type.fft_filter.high_pass_freq_hz = 5000.0;
        return {type};
    }
    if (name == "fft-filter-amplitude-unfiltered-pass") {
        auto type = amplitude_choice(21, SweepChannelChoice::RequireOne);
        type.enable_fft_filter = true;
        type.fft_filter.band = SweepFftFilterBand::HighPass;
        type.fft_filter.high_pass_freq_hz = 20000.0;
        return {type};
    }
    if (name == "fft-filter-unrestricted-spectrum-pass") {
        auto type = base(22);
        type.restrict_length = false;
        type.enable_peak = true;
        type.peak_search_range_hz = {1000.0, 23000.0};
        type.peak_range_hz = {8000.0, 10000.0};
        type.enable_fft_filter = true;
        type.fft_filter.band = SweepFftFilterBand::HighPass;
        type.fft_filter.high_pass_freq_hz = 20000.0;
        return {type};
    }
    if (name == "disabled-skipped") {
        auto disabled = pass(40);
        disabled.enabled = false;
        return {disabled, pass(43)};
    }
    if (name == "check-all") {
        auto first = pass(41);
        first.discard = false;
        auto second = pass(42);
        second.discard = true;
        return {first, second};
    }
    static const std::unordered_map<std::string, int> failures{
        {"length-fail", 7}, {"amplitude-fail", 8}, {"energy-fail", 9},
        {"peak-fail", 10}, {"width-fail", 11}, {"mean-fail", 12},
        {"zero-crossing-fail", 13}, {"sweep-fail", 14},
        {"min-correlation-fail", 15}, {"peak-correlation-fail", 16},
    };
    const auto found = failures.find(name);
    if (found != failures.end()) return {single_failure(name, found->second)};
    throw std::runtime_error("unknown fixture case " + name);
}

ClickDetectionResult click() {
    ClickDetectionResult value;
    value.channel_bitmap = 0x3;
    value.channels = {0, 1};
    value.duration_samples = 96;
    value.waveform.assign(2, std::vector<double>(96, 0.0));
    constexpr double rate = 48000.0;
    for (int i = 0; i < 96; ++i) {
        const double envelope = std::exp(-0.5 * std::pow((i - 42.0) / 7.0, 2.0));
        value.waveform[0][static_cast<std::size_t>(i)] =
            0.03 * std::sin(i * 0.19) +
            envelope * (std::sin(2.0 * std::numbers::pi * 9000.0 * i / rate) +
                        0.45 * std::sin(2.0 * std::numbers::pi * 14000.0 * i / rate));
        value.waveform[1][static_cast<std::size_t>(i)] =
            0.2 * (0.02 * std::cos(i * 0.11 + 0.2) +
            0.82 * envelope *
                (std::sin(2.0 * std::numbers::pi * 9200.0 * i / rate + 0.4) +
                 0.25 * std::sin(2.0 * std::numbers::pi * 15000.0 * i / rate)));
    }
    return value;
}

bool close(double actual, double expected) {
    return std::abs(actual - expected) <= 1e-12 * std::max(1.0, std::abs(expected));
}

int check_defaults(const std::string& path) {
    std::ifstream input(path);
    std::string header;
    std::getline(input, header);
    int errors = 0;
    std::string line;
    while (std::getline(input, line)) {
        const auto f = split(line);
        if (f.size() != 28) {
            std::cerr << "bad defaults fixture row\n";
            return 1;
        }
        const bool porpoise = f[0] == "Porpoise";
        const auto type = standard_sweep_click_type(
            1, porpoise ? BasicClickStandardType::Porpoise : BasicClickStandardType::BeakedWhale);
        const auto expect_bool = [&](std::size_t index, bool actual, const char* name) {
            const bool expected = f[index] == "true";
            if (actual != expected) {
                std::cerr << f[0] << ' ' << name << " mismatch\n";
                ++errors;
            }
        };
        const auto expect_num = [&](std::size_t index, double actual, const char* name) {
            const double expected = std::stod(f[index]);
            if (!close(actual, expected)) {
                std::cerr << f[0] << ' ' << name << " mismatch: " << actual
                          << " != " << expected << '\n';
                ++errors;
            }
        };
        expect_bool(1, type.enable_length, "enableLength");
        expect_num(2, type.length_ms.low, "minLength");
        expect_num(3, type.length_ms.high, "maxLength");
        expect_num(4, type.length_db, "lengthDb");
        expect_bool(5, type.enable_energy_bands, "enableEnergy");
        expect_num(6, type.test_energy_band_hz.low, "testLow");
        expect_num(7, type.test_energy_band_hz.high, "testHigh");
        expect_num(8, type.control_energy_band_0_hz.low, "control0Low");
        expect_num(9, type.control_energy_band_0_hz.high, "control0High");
        expect_num(10, type.control_energy_band_1_hz.low, "control1Low");
        expect_num(11, type.control_energy_band_1_hz.high, "control1High");
        expect_num(12, type.energy_threshold_0_db, "threshold0");
        expect_num(13, type.energy_threshold_1_db, "threshold1");
        expect_bool(14, type.enable_peak, "enablePeak");
        expect_num(15, type.peak_search_range_hz.low, "searchLow");
        expect_num(16, type.peak_search_range_hz.high, "searchHigh");
        expect_num(17, type.peak_range_hz.low, "peakLow");
        expect_num(18, type.peak_range_hz.high, "peakHigh");
        expect_bool(19, type.enable_mean, "enableMean");
        expect_num(20, type.mean_range_hz.low, "meanLow");
        expect_num(21, type.mean_range_hz.high, "meanHigh");
        expect_bool(22, type.enable_zero_crossings, "enableZeroCrossings");
        expect_num(23, type.zero_crossing_count.low, "zcLow");
        expect_num(24, type.zero_crossing_count.high, "zcHigh");
        expect_bool(25, type.enable_sweep, "enableSweep");
        expect_num(26, type.zero_crossing_sweep_khz_per_ms.low, "sweepLow");
        expect_num(27, type.zero_crossing_sweep_khz_per_ms.high, "sweepHigh");
    }
    return errors;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: click_sweep_classifier_fixture_check classifications.csv defaults.csv\n";
        return 2;
    }
    std::ifstream input(argv[1]);
    if (!input) return 2;
    std::string header;
    std::getline(input, header);
    const auto detection = click();
    int errors = 0;
    std::string line;
    while (std::getline(input, line)) {
        const auto fields = split(line);
        if (fields.size() != 4) {
            std::cerr << "bad classification fixture row\n";
            return 2;
        }
        SweepClickClassifierConfig config;
        config.sample_rate_hz = 48000.0;
        config.check_all_classifiers = fields[0] == "check-all";
        config.click_types = types_for_case(fields[0]);
        const auto actual = SweepClickClassifier(config).identify(detection);
        const int expected_type = std::stoi(fields[1]);
        const bool expected_discard = fields[2] == "true";
        std::vector<int> expected_passed;
        if (!fields[3].empty()) {
            for (const auto& value : split(fields[3], ';')) expected_passed.push_back(std::stoi(value));
        }
        if (actual.click_type != expected_type || actual.discard != expected_discard ||
            actual.classifiers_passed != expected_passed) {
            std::cerr << fields[0] << " mismatch: got " << actual.click_type << ','
                      << actual.discard << " expected " << expected_type << ','
                      << expected_discard << '\n';
            ++errors;
        }
    }
    errors += check_defaults(argv[2]);
    if (errors != 0) return 1;
    std::cout << "Sweep classifier Java parity: all fixture cases passed\n";
    return 0;
}
