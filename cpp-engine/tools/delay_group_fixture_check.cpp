#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/localisation/DelayGroupEstimator.h"

namespace {

struct FixtureRow {
    std::size_t pair_index = 0;
    std::size_t channel_a = 0;
    std::size_t channel_b = 0;
    double delay_samples = 0.0;
    double delay_score = 0.0;
};

double pulse(std::size_t sample, double centre) {
    const double x = static_cast<double>(sample) - centre;
    const double envelope = std::exp(-(x * x) / (2.0 * 10.0));
    return envelope * std::cos(0.54 * x) + 0.01 * std::sin(static_cast<double>(sample) * 0.17);
}

std::vector<FixtureRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<FixtureRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.find("pairIndex,channelA,channelB,delaySamples,delayScore") != std::string::npos) {
            continue;
        }

        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 5) {
            continue;
        }

        FixtureRow row;
        row.pair_index = static_cast<std::size_t>(std::stoull(cells[0]));
        row.channel_a = static_cast<std::size_t>(std::stoull(cells[1]));
        row.channel_b = static_cast<std::size_t>(std::stoull(cells[2]));
        row.delay_samples = std::stod(cells[3]);
        row.delay_score = std::stod(cells[4]);
        rows.push_back(row);
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "Usage: delay_group_fixture_check <fixture.csv> [raw|restricted|upsample|filter|envelope|leading|combined]\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);

        std::vector<std::vector<double>> waveforms(3, std::vector<double>(64));
        for (std::size_t i = 0; i < 64; ++i) {
            waveforms[0][i] = pulse(i, 24.0);
            waveforms[1][i] = 0.92 * pulse(i, 29.0);
            waveforms[2][i] = 0.78 * pulse(i, 21.0);
        }

        pamguard::localisation::DelayGroupEstimator estimator;
        pamguard::localisation::DelayMeasurementConfig config;
        const std::string mode = argc == 3 ? argv[2] : "raw";
        if (mode == "restricted") {
            config.use_restricted_bins = true;
            config.restricted_bins = 40;
        }
        else if (mode == "upsample") {
            config.up_sample = 2;
        }
        else if (mode == "filter") {
            config.filter_bearings = true;
            config.filter_band = pamguard::localisation::DelayFilterBand::BandPass;
            config.filter_high_pass_hz = 3500.0;
            config.filter_low_pass_hz = 14500.0;
        }
        else if (mode == "envelope") {
            config.envelope_bearings = true;
        }
        else if (mode == "leading") {
            config.envelope_bearings = true;
            config.use_leading_edge = true;
            config.leading_edge_search_start = 16;
            config.leading_edge_search_end = 32;
        }
        else if (mode == "combined") {
            config.use_restricted_bins = true;
            config.restricted_bins = 48;
            config.up_sample = 2;
            config.filter_bearings = true;
            config.filter_band = pamguard::localisation::DelayFilterBand::BandPass;
            config.filter_high_pass_hz = 3500.0;
            config.filter_low_pass_hz = 14500.0;
            config.envelope_bearings = true;
            config.use_leading_edge = true;
            config.leading_edge_search_start = 16;
            config.leading_edge_search_end = 32;
        }
        else if (mode != "raw") {
            throw std::invalid_argument("unknown delay mode: " + mode);
        }
        const auto actual = estimator.estimate_delays(
            waveforms, {16.0, 16.0, 16.0}, 48000.0, config);
        if (!estimator.estimate_delays({waveforms[0]}, {}).empty()) {
            std::cerr << "Single-channel delay group should not produce channel pairs\n";
            return 1;
        }
        bool threw_bad_delay_count = false;
        try {
            (void)estimator.estimate_delays(waveforms, {16.0});
        }
        catch (const std::invalid_argument&) {
            threw_bad_delay_count = true;
        }
        if (!threw_bad_delay_count) {
            std::cerr << "Delay group estimator should reject mismatched max_delay_samples\n";
            return 1;
        }
        if (actual.size() != fixture.size()) {
            std::cerr << "Delay count mismatch: fixture=" << fixture.size() << " actual=" << actual.size() << "\n";
            return 1;
        }

        constexpr double delay_tolerance = 1e-8;
        constexpr double score_tolerance = 1e-6;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const auto& got = actual[i];
            const auto& expected = fixture[i];
            if (got.pair_index != expected.pair_index || got.channel_a != expected.channel_a || got.channel_b != expected.channel_b) {
                std::cerr << "Pair metadata mismatch at row " << i << "\n";
                return 1;
            }

            const double delay_error = std::abs(got.delay.delay_samples - expected.delay_samples);
            const double score_error = std::abs(got.delay.delay_score - expected.delay_score);
            if (delay_error > delay_tolerance || score_error > score_tolerance) {
                std::cerr << "Delay group parity failed at row " << i << "\n";
                std::cerr << "expected delay/score=" << expected.delay_samples << "/" << expected.delay_score << "\n";
                std::cerr << "actual   delay/score=" << got.delay.delay_samples << "/" << got.delay.delay_score << "\n";
                std::cerr << "errors  delay/score=" << delay_error << "/" << score_error << "\n";
                return 1;
            }
        }

        std::cout << "Delay group parity passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
