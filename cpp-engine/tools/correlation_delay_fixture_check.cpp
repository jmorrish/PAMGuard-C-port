#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/localisation/CorrelationDelayEstimator.h"

namespace {

struct FixtureRow {
    double delay_samples = 0.0;
    double delay_score = 0.0;
};

bool close(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}

double pulse(std::size_t sample, double centre) {
    const double x = static_cast<double>(sample) - centre;
    const double envelope = std::exp(-(x * x) / (2.0 * 10.0));
    return envelope * std::cos(0.54 * x) + 0.01 * std::sin(static_cast<double>(sample) * 0.17);
}

FixtureRow read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.find("delaySamples,delayScore") != std::string::npos) {
            continue;
        }

        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() == 2) {
            FixtureRow row;
            row.delay_samples = std::stod(cells[0]);
            row.delay_score = std::stod(cells[1]);
            return row;
        }
    }

    throw std::runtime_error("fixture did not contain a delay row");
}

void run_edge_checks() {
    pamguard::localisation::CorrelationDelayEstimator estimator;

    bool threw_bad_fft = false;
    try {
        (void)estimator.estimate_delay({1.0}, {1.0}, 0, 1.0);
    }
    catch (const std::invalid_argument&) {
        threw_bad_fft = true;
    }
    if (!threw_bad_fft) {
        throw std::runtime_error("correlation delay estimator should reject zero fft_length");
    }

    bool threw_bad_delay = false;
    try {
        (void)estimator.estimate_delay({1.0}, {1.0}, 4, -1.0);
    }
    catch (const std::invalid_argument&) {
        threw_bad_delay = true;
    }
    if (!threw_bad_delay) {
        throw std::runtime_error("correlation delay estimator should reject negative max_delay_samples");
    }

    const auto silent = estimator.estimate_delay({}, {}, 8, 3.0);
    if (!close(silent.delay_samples, 0.0) || !close(silent.delay_score, 0.0)) {
        throw std::runtime_error("silent correlation delay should produce zero delay and score");
    }

    std::vector<double> channel_0(64);
    std::vector<double> channel_1(64);
    for (std::size_t i = 0; i < channel_0.size(); ++i) {
        channel_0[i] = pulse(i, 24.0);
        channel_1[i] = 0.92 * pulse(i, 29.0);
    }

    const auto clamped = estimator.estimate_delay(channel_0, channel_1, 64, 0.0);
    if (!close(clamped.delay_samples, 0.0)) {
        throw std::runtime_error("zero max_delay_samples should clamp delay to zero");
    }

    const auto identical = estimator.estimate_delay(channel_0, channel_0, 64, 16.0);
    if (std::abs(identical.delay_samples) > 1e-8 || identical.delay_score <= 0.0) {
        throw std::runtime_error("identical signals should produce a positive zero-delay correlation");
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: correlation_delay_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);
        run_edge_checks();

        std::vector<double> channel_0(64);
        std::vector<double> channel_1(64);
        for (std::size_t i = 0; i < channel_0.size(); ++i) {
            channel_0[i] = pulse(i, 24.0);
            channel_1[i] = 0.92 * pulse(i, 29.0);
        }

        pamguard::localisation::CorrelationDelayEstimator estimator;
        const auto actual = estimator.estimate_delay(channel_0, channel_1, 64, 16.0);

        const double delay_error = std::abs(actual.delay_samples - fixture.delay_samples);
        const double score_error = std::abs(actual.delay_score - fixture.delay_score);
        constexpr double delay_tolerance = 1e-8;
        constexpr double score_tolerance = 1e-6;
        if (delay_error > delay_tolerance || score_error > score_tolerance) {
            std::cerr << "Correlation delay parity failed\n";
            std::cerr << "expected delay/score=" << fixture.delay_samples << "/" << fixture.delay_score << "\n";
            std::cerr << "actual   delay/score=" << actual.delay_samples << "/" << actual.delay_score << "\n";
            std::cerr << "errors  delay/score=" << delay_error << "/" << score_error << "\n";
            return 1;
        }

        std::cout << "Correlation delay parity passed\n";
        std::cout << "delay_error=" << delay_error << " score_error=" << score_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
