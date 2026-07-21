#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/localisation/StreamerOrientation.h"

namespace {

struct OrientationRow {
    std::string name;
    double heading_deg = 0.0;
    double pitch_deg = 0.0;
    double roll_deg = 0.0;
    std::array<double, 3> vector{};
    std::array<double, 3> rotated{};
};

std::vector<OrientationRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<OrientationRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line.rfind("case,headingDeg", 0) == 0) {
            continue;
        }
        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 10) {
            throw std::runtime_error("orientation row must have ten columns: " + line);
        }
        OrientationRow row;
        row.name = cells[0];
        row.heading_deg = std::stod(cells[1]);
        row.pitch_deg = std::stod(cells[2]);
        row.roll_deg = std::stod(cells[3]);
        row.vector = {std::stod(cells[4]), std::stod(cells[5]), std::stod(cells[6])};
        row.rotated = {std::stod(cells[7]), std::stod(cells[8]), std::stod(cells[9])};
        rows.push_back(row);
    }
    if (rows.empty()) {
        throw std::runtime_error("fixture did not contain any orientation rows");
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: streamer_orientation_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);
        constexpr double deg = std::numbers::pi / 180.0;
        constexpr double tolerance = 1e-12;
        double max_abs_error = 0.0;

        for (const auto& row : fixture) {
            const auto rotated = pamguard::localisation::rotate_by_streamer_orientation(
                row.vector, row.heading_deg * deg, row.pitch_deg * deg, row.roll_deg * deg);
            for (std::size_t i = 0; i < 3; ++i) {
                const double error = std::abs(rotated[i] - row.rotated[i]);
                max_abs_error = std::max(max_abs_error, error);
                if (error > tolerance) {
                    std::cerr << "Streamer orientation parity failed for case " << row.name
                              << " component " << i << ": fixture=" << row.rotated[i]
                              << " ported=" << rotated[i] << "\n";
                    return 1;
                }
            }
        }

        // A zero rotation must leave a vector untouched apart from rounding.
        const auto identity = pamguard::localisation::rotate_by_streamer_orientation({3.0, -4.0, 5.0}, 0.0, 0.0, 0.0);
        if (std::abs(identity[0] - 3.0) > 1e-12 || std::abs(identity[1] + 4.0) > 1e-12 ||
            std::abs(identity[2] - 5.0) > 1e-12) {
            std::cerr << "Zero orientation should leave the vector unchanged\n";
            return 1;
        }

        std::cout << "Streamer orientation parity passed\n";
        std::cout << "cases=" << fixture.size() << " max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
