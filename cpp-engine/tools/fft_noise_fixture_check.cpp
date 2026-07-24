#include "pamguard/detectors/FftNoiseMonitor.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) fields.push_back(field);
    return fields;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream input(argv[1]);
    if (!input) return 2;
    pamguard::detectors::FftNoiseConfig config;
    config.enabled = true;
    config.channels = {0, 1};
    config.measurement_interval_seconds = 1;
    config.use_all = true;
    config.bands = {{"fractional", 0.5, 2.5}, {"full", 0.0, 4.0}};
    pamguard::detectors::FftNoiseMonitor monitor(8.0, 8, 2, config);
    std::vector<pamguard::detectors::FftNoisePeriod> pending;
    std::string line;
    std::size_t outputs = 0;
    double max_error = 0.0;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split(line);
        if (fields[0] == "in") {
            std::vector<double> power;
            for (std::size_t i = 4; i + 1 < fields.size(); i += 2) {
                const double re = std::stod(fields[i]);
                const double im = std::stod(fields[i + 1]);
                power.push_back(re * re + im * im);
            }
            auto emitted = monitor.process_frame(
                std::stoul(fields[3]), std::stoll(fields[1]),
                std::stoll(fields[2]), power);
            pending.insert(pending.end(), emitted.begin(), emitted.end());
        }
        else if (fields[0] == "out") {
            if (pending.empty()) {
                std::cerr << "Java emitted an unpaired output\n";
                return 1;
            }
            const auto actual = pending.front();
            pending.erase(pending.begin());
            if (actual.channel != std::stoul(fields[1]) ||
                actual.end_sample != std::stoll(fields[2]) ||
                actual.n_measurements != 4) {
                std::cerr << "Output metadata mismatch\n";
                return 1;
            }
            std::size_t field = 3;
            for (const auto& band : actual.bands) {
                const double values[] = {
                    band.mean, band.median, band.low_95, band.high_95,
                    band.minimum, band.maximum};
                for (double value : values) {
                    const double error =
                        std::abs(value - std::stod(fields.at(field++)));
                    max_error = std::max(max_error, error);
                    if (error > 1e-12) {
                        std::cerr << "Statistic mismatch: " << error << "\n";
                        return 1;
                    }
                }
            }
            ++outputs;
        }
    }
    if (!pending.empty() || outputs != 4) return 1;
    std::cout << "FFT noise Java parity: outputs=" << outputs
              << " maxError=" << max_error << "\n";
    return 0;
}
