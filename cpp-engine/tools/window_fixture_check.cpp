#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "pamguard/dsp/WindowFunction.h"

namespace {

struct FixtureData {
    std::vector<double> values;
    double gain = std::numeric_limits<double>::quiet_NaN();
};

pamguard::dsp::WindowType parse_window_type(const std::string& raw) {
    const int type = std::stoi(raw);
    if (type < 0 || type > 5) {
        throw std::invalid_argument("window type must be 0..5");
    }
    return static_cast<pamguard::dsp::WindowType>(type);
}

FixtureData read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    FixtureData fixture;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.find("index,value") != std::string::npos) {
            continue;
        }

        const auto comma = line.find(',');
        if (comma == std::string::npos) {
            continue;
        }

        const auto key = line.substr(0, comma);
        const auto value = line.substr(comma + 1);
        if (value == "value") {
            continue;
        }

        if (key == "#gain") {
            fixture.gain = std::stod(value);
            continue;
        }

        fixture.values.push_back(std::stod(value));
    }

    return fixture;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: window_fixture_check <windowType> <length> <fixture.csv>\n";
        return 2;
    }

    try {
        const auto type = parse_window_type(argv[1]);
        const auto length = static_cast<std::size_t>(std::stoull(argv[2]));
        const auto fixture = read_fixture(argv[3]);
        const auto actual = pamguard::dsp::make_window(type, length);
        const auto actual_gain = pamguard::dsp::window_gain(actual);

        if (fixture.values.size() != actual.size()) {
            std::cerr << "Length mismatch: fixture=" << fixture.values.size() << " actual=" << actual.size() << "\n";
            return 1;
        }

        double max_abs_error = 0.0;
        std::size_t max_index = 0;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const double error = std::abs(actual[i] - fixture.values[i]);
            if (error > max_abs_error) {
                max_abs_error = error;
                max_index = i;
            }
        }

        const double gain_error = std::abs(actual_gain - fixture.gain);
        constexpr double tolerance = 1e-12;
        if (max_abs_error > tolerance || gain_error > tolerance) {
            std::cerr << "Window parity failed\n";
            std::cerr << "max_abs_error=" << max_abs_error << " at index " << max_index << "\n";
            std::cerr << "gain_error=" << gain_error << "\n";
            return 1;
        }

        std::cout << "Window parity passed\n";
        std::cout << "max_abs_error=" << max_abs_error << "\n";
        std::cout << "gain_error=" << gain_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
