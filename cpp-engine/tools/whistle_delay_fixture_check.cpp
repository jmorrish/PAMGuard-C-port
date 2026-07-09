#include <cmath>
#include <complex>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/localisation/WhistleDelayEstimator.h"

namespace {

constexpr std::size_t fft_length = 256;

struct WhistleDelayFixtureRow {
    std::string case_name;
    double delay_samples = 0.0;
    double delay_score = 0.0;
};

struct WhistleDelayCase {
    std::string name;
    double delay_samples = 0.0;
    double gain = 0.0;
    int slice_count = 0;
    int first_bin = 0;
    int bins_per_slice = 0;
    int bin_rise_per_slice = 0;
    double max_delay = 0.0;
};

// Case parameters shared by name with the PAMGuard Java fixture exporter
// (reference-tools/.../WhistleDelayFixtureExporter.java).
std::vector<WhistleDelayCase> case_catalogue() {
    return {
        {"zero-delay", 0.0, 0.9, 6, 30, 5, 0, 40.0},
        {"fractional-positive", 3.5, 0.9, 6, 30, 5, 0, 40.0},
        {"fractional-negative", -2.25, 0.85, 6, 30, 5, 0, 40.0},
        {"beyond-window-ambiguity", 8.0, 0.9, 6, 30, 5, 0, 5.0},
        {"rising-contour", 1.75, 0.8, 8, 28, 3, 2, 40.0},
    };
}

std::pair<std::complex<double>, std::complex<double>> synthetic_bins(int bin, int slice, const WhistleDelayCase& delay_case) {
    const double amplitude = 1.0 + 0.1 * std::sin(bin * 0.7 + slice * 0.5);
    const double phase = 0.31 * slice + 0.117 * bin;
    const double ch1r = amplitude * std::cos(phase);
    const double ch1i = amplitude * std::sin(phase);

    const double jitter = 0.02 * std::sin(static_cast<double>(slice + bin));
    const double rotation = -2.0 * std::numbers::pi * bin * delay_case.delay_samples / static_cast<double>(fft_length) + jitter;
    const double cos_r = std::cos(rotation);
    const double sin_r = std::sin(rotation);
    const double ch2r = delay_case.gain * (ch1r * cos_r - ch1i * sin_r);
    const double ch2i = delay_case.gain * (ch1r * sin_r + ch1i * cos_r);
    return {{ch1r, ch1i}, {ch2r, ch2i}};
}

std::vector<WhistleDelayFixtureRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<WhistleDelayFixtureRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line == "case,delaySamples,delayScore") {
            continue;
        }

        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 3) {
            throw std::runtime_error("fixture row must have three columns: " + line);
        }
        rows.push_back(WhistleDelayFixtureRow{cells[0], std::stod(cells[1]), std::stod(cells[2])});
    }
    if (rows.empty()) {
        throw std::runtime_error("fixture did not contain any case rows");
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: whistle_delay_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);
        const auto catalogue = case_catalogue();
        if (fixture.size() != catalogue.size()) {
            std::cerr << "Fixture case count mismatch: fixture=" << fixture.size()
                      << " catalogue=" << catalogue.size() << "\n";
            return 1;
        }

        {
            bool rejected_bad_length = false;
            try {
                pamguard::localisation::WhistleDelayEstimator bad_estimator(100);
                (void)bad_estimator;
            }
            catch (const std::invalid_argument&) {
                rejected_bad_length = true;
            }
            if (!rejected_bad_length) {
                std::cerr << "Whistle delay estimator should reject non-power-of-two FFT length\n";
                return 1;
            }

            pamguard::localisation::WhistleDelayEstimator estimator(fft_length);
            bool rejected_bad_bin = false;
            try {
                estimator.add_fft_data({1.0, 0.0}, {1.0, 0.0}, fft_length / 2);
            }
            catch (const std::invalid_argument&) {
                rejected_bad_bin = true;
            }
            if (!rejected_bad_bin) {
                std::cerr << "Whistle delay estimator should reject bins at or above fft_length/2\n";
                return 1;
            }
        }

        constexpr double tolerance = 1e-8;
        double max_abs_error = 0.0;
        for (std::size_t i = 0; i < catalogue.size(); ++i) {
            const auto& delay_case = catalogue[i];
            const auto& expected = fixture[i];
            if (expected.case_name != delay_case.name) {
                std::cerr << "Fixture case order mismatch at row " << i << ": fixture=" << expected.case_name
                          << " catalogue=" << delay_case.name << "\n";
                return 1;
            }

            pamguard::localisation::WhistleDelayEstimator estimator(fft_length);
            for (int slice = 0; slice < delay_case.slice_count; ++slice) {
                const int start_bin = delay_case.first_bin + slice * delay_case.bin_rise_per_slice;
                for (int b = 0; b < delay_case.bins_per_slice; ++b) {
                    const int bin = start_bin + b;
                    const auto [ch1, ch2] = synthetic_bins(bin, slice, delay_case);
                    estimator.add_fft_data(ch1, ch2, static_cast<std::size_t>(bin));
                }
            }

            const auto result = estimator.get_delay(delay_case.max_delay);
            const double delay_error = std::abs(result.delay_samples - expected.delay_samples);
            const double score_error = std::abs(result.delay_score - expected.delay_score);
            max_abs_error = std::max({max_abs_error, delay_error, score_error});
            if (delay_error > tolerance || score_error > tolerance) {
                std::cerr << "Whistle delay parity failed for case " << delay_case.name << "\n";
                std::cerr << "expected delay/score=" << expected.delay_samples << "/" << expected.delay_score << "\n";
                std::cerr << "actual   delay/score=" << result.delay_samples << "/" << result.delay_score << "\n";
                return 1;
            }
        }

        std::cout << "Whistle delay parity passed\n";
        std::cout << "cases=" << catalogue.size() << " max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
