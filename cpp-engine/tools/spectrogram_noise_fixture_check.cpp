#include <cmath>
#include <complex>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/SpectrogramNoiseReducer.h"

namespace {

using pamguard::detectors::SpectrogramNoiseConfig;
using pamguard::detectors::SpectrogramNoiseReducer;

using Slices = std::vector<std::vector<std::complex<double>>>;

std::map<std::string, Slices> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }
    std::map<std::string, Slices> cases;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line.rfind("case,", 0) == 0) {
            continue;
        }
        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 5) {
            throw std::runtime_error("noise row must have five columns: " + line);
        }
        auto& slices = cases[cells[0]];
        const auto slice = static_cast<std::size_t>(std::stoul(cells[1]));
        const auto bin = static_cast<std::size_t>(std::stoul(cells[2]));
        if (slices.size() <= slice) {
            slices.resize(slice + 1);
        }
        if (slices[slice].size() <= bin) {
            slices[slice].resize(bin + 1);
        }
        slices[slice][bin] = {std::stod(cells[3]), std::stod(cells[4])};
    }
    if (cases.empty()) {
        throw std::runtime_error("fixture did not contain any rows");
    }
    return cases;
}

SpectrogramNoiseConfig config_for(const std::string& name) {
    SpectrogramNoiseConfig config;
    if (name == "median-only") {
        config.run_median_filter = true;
    }
    else if (name == "average-only") {
        config.run_average_subtraction = true;
    }
    else if (name == "kernel-only") {
        config.run_kernel_smoothing = true;
    }
    else if (name == "threshold-raw" || name == "threshold-binary" || name == "threshold-input") {
        config.run_threshold = true;
        config.threshold_db = 8.0;
        config.threshold_final_output = name == "threshold-binary" ? SpectrogramNoiseConfig::kOutputBinary
                                        : name == "threshold-input" ? SpectrogramNoiseConfig::kOutputInput
                                                                    : SpectrogramNoiseConfig::kOutputRaw;
    }
    else if (name == "full-chain-raw" || name == "full-chain-binary") {
        config.run_median_filter = true;
        config.run_average_subtraction = true;
        config.average_update_constant = 0.05;
        config.run_kernel_smoothing = true;
        config.run_threshold = true;
        config.threshold_db = 6.0;
        config.threshold_final_output = name == "full-chain-binary" ? SpectrogramNoiseConfig::kOutputBinary
                                                                    : SpectrogramNoiseConfig::kOutputRaw;
    }
    else {
        throw std::runtime_error("no config for fixture case " + name);
    }
    return config;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: spectrogram_noise_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto cases = read_fixture(argv[1]);
        const auto input_case = cases.find("input");
        if (input_case == cases.end()) {
            throw std::runtime_error("fixture is missing the input case");
        }
        const auto& input_slices = input_case->second;

        // Both sides run real reference-shaped state machines over identical
        // inputs, so the tolerance is near machine precision.
        constexpr double tolerance = 1e-12;
        double max_abs_error = 0.0;
        std::size_t checked_cases = 0;

        for (const auto& [name, expected] : cases) {
            if (name == "input") {
                continue;
            }
            SpectrogramNoiseReducer reducer(config_for(name));
            for (std::size_t slice = 0; slice < input_slices.size(); ++slice) {
                const auto produced = reducer.process(0, input_slices[slice]);
                if (produced.size() != expected[slice].size()) {
                    std::cerr << "Case " << name << " slice " << slice << " size mismatch\n";
                    return 1;
                }
                for (std::size_t bin = 0; bin < produced.size(); ++bin) {
                    const double error = std::max(std::abs(produced[bin].real() - expected[slice][bin].real()),
                                                  std::abs(produced[bin].imag() - expected[slice][bin].imag()));
                    max_abs_error = std::max(max_abs_error, error);
                    if (error > tolerance) {
                        std::cerr << "Case " << name << " slice " << slice << " bin " << bin
                                  << ": fixture=(" << expected[slice][bin].real() << ", "
                                  << expected[slice][bin].imag() << ") ported=(" << produced[bin].real() << ", "
                                  << produced[bin].imag() << ")\n";
                        return 1;
                    }
                }
            }
            ++checked_cases;
        }

        std::cout << "Spectrogram noise reduction parity passed\n";
        std::cout << "cases=" << checked_cases << " slices=" << input_slices.size()
                  << " max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
