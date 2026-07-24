#include "pamguard/detectors/SpectrumBackground.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

double parse_java_double(const std::string& value) {
    if (value == "NaN") {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (value == "Infinity") {
        return std::numeric_limits<double>::infinity();
    }
    if (value == "-Infinity") {
        return -std::numeric_limits<double>::infinity();
    }
    return std::stod(value);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    std::ifstream input(argv[1]);
    if (!input) {
        return 2;
    }

    pamguard::detectors::SpectrumBackground background(8.0, 2, 4, 1.0);
    std::vector<double> power(4, 0.0);
    std::vector<double> expected(4, 0.0);
    std::size_t rows = 0;
    double max_error = 0.0;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#' ||
            line.rfind("slice,", 0) == 0) {
            continue;
        }
        const auto fields = split(line);
        const auto slice = static_cast<std::size_t>(std::stoul(fields.at(0)));
        const auto bin = static_cast<std::size_t>(std::stoul(fields.at(1)));
        const double re = parse_java_double(fields.at(2));
        const double im = parse_java_double(fields.at(3));
        power.at(bin) = re * re + im * im;
        expected.at(bin) = std::stod(fields.at(4));
        if (bin + 1 == power.size()) {
            const auto& actual = background.process(power);
            for (std::size_t compare_bin = 0; compare_bin < actual.size();
                 ++compare_bin) {
                const double error =
                    std::abs(actual[compare_bin] - expected[compare_bin]);
                max_error = std::max(max_error, error);
                if (error > 1e-14) {
                    std::cerr << "Spectrum background mismatch at slice "
                              << slice << " bin " << compare_bin << ": "
                              << error << "\n";
                    return 1;
                }
            }
        }
        ++rows;
    }
    if (rows != 32 || background.processed_slices() != 8) {
        std::cerr << "Unexpected fixture size or process count\n";
        return 1;
    }
    std::cout << "Spectrum background Java parity: rows=" << rows
              << " maxError=" << max_error << "\n";
    return 0;
}
