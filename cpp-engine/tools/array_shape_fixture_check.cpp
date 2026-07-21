#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/localisation/ArrayShape.h"

namespace {

struct ArrayShapeFixtureRow {
    std::string case_name;
    int shape = 0;
    std::size_t vector_count = 0;
    std::array<double, 9> vectors{};
};

struct ArrayShapeCase {
    std::string name;
    std::vector<std::array<double, 3>> positions_m;
    /** Empty means a single streamer. */
    std::vector<int> streamer_ids;
};

// Case positions shared by name with the PAMGuard Java fixture exporter
// (reference-tools/.../ArrayShapeFixtureExporter.java).
std::vector<ArrayShapeCase> case_catalogue() {
    return {
        {"point-duplicate-2ch", {{1.0, 2.0, 3.0}, {1.0, 2.0, 3.0}}},
        {"two-ch-diagonal", {{0.0, 0.0, 0.0}, {1.0, 2.0, 0.5}}},
        {"line-3ch-y", {{0.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {0.0, 7.5, 0.0}}},
        {"line-4ch-negative-x", {{0.0, 0.0, 0.0}, {-2.0, 0.0, 0.0}, {-5.0, 0.0, 0.0}, {-9.0, 0.0, 0.0}}},
        {"plane-3ch-xy", {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 3.0, 0.0}}},
        {"plane-4ch-rect", {{0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {2.0, 3.0, 0.0}}},
        {"plane-4ch-tilted", {{0.0, 0.0, 0.0}, {2.0, 0.0, 1.0}, {0.0, 3.0, 0.5}, {2.0, 3.0, 1.5}}},
        {"volume-4ch-tetrahedron", {{0.0, 0.0, 0.0}, {2.5, 0.0, 0.0}, {0.0, 2.5, 0.0}, {0.0, 0.0, 2.5}}},
        {"volume-5ch-towed-cluster",
         {{0.0, 0.0, 0.0}, {0.05, 3.0, 0.02}, {-0.03, 6.0, 0.04}, {0.02, 9.0, 0.5}, {0.5, 12.0, 0.1}}},
        // Co-located phones on different streamers are not duplicates.
        {"streamers-colocated-pair",
         {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 5.0, 0.0}, {0.0, 5.0, 0.0}},
         {0, 1, 0, 1}},
        {"streamers-colocated-pair-single-streamer",
         {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 5.0, 0.0}, {0.0, 5.0, 0.0}},
         {0, 0, 0, 0}},
        {"streamers-two-towed-lines",
         {{0.0, 0.0, 0.0}, {0.0, 4.0, 0.0}, {0.0, 8.0, 0.0},
          {3.0, 0.0, 0.0}, {3.0, 4.0, 0.0}, {3.0, 8.0, 0.0}},
         {0, 0, 0, 1, 1, 1}},
    };
}

std::vector<ArrayShapeFixtureRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<ArrayShapeFixtureRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line.rfind("case,shape,", 0) == 0) {
            continue;
        }

        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 12) {
            throw std::runtime_error("fixture row must have twelve columns: " + line);
        }

        ArrayShapeFixtureRow row;
        row.case_name = cells[0];
        row.shape = std::stoi(cells[1]);
        row.vector_count = static_cast<std::size_t>(std::stoull(cells[2]));
        for (std::size_t i = 0; i < 9; ++i) {
            row.vectors[i] = std::stod(cells[3 + i]);
        }
        rows.push_back(row);
    }
    if (rows.empty()) {
        throw std::runtime_error("fixture did not contain any case rows");
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: array_shape_fixture_check <fixture.csv>\n";
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

        if (pamguard::localisation::array_shape({}) != pamguard::localisation::ArrayShapeType::None ||
            !pamguard::localisation::array_directions({}).empty()) {
            std::cerr << "Empty position list should give the none shape and no directions\n";
            return 1;
        }

        constexpr double tolerance = 1e-12;
        double max_abs_error = 0.0;
        for (std::size_t i = 0; i < catalogue.size(); ++i) {
            const auto& shape_case = catalogue[i];
            const auto& expected = fixture[i];
            if (expected.case_name != shape_case.name) {
                std::cerr << "Fixture case order mismatch at row " << i << ": fixture=" << expected.case_name
                          << " catalogue=" << shape_case.name << "\n";
                return 1;
            }

            const auto shape = pamguard::localisation::array_shape(shape_case.positions_m, shape_case.streamer_ids);
            if (static_cast<int>(shape) != expected.shape) {
                std::cerr << "Array shape mismatch for case " << shape_case.name << ": fixture=" << expected.shape
                          << " actual=" << static_cast<int>(shape) << "\n";
                return 1;
            }

            const auto directions = pamguard::localisation::array_directions(shape_case.positions_m, shape_case.streamer_ids);
            if (directions.size() != expected.vector_count) {
                std::cerr << "Direction count mismatch for case " << shape_case.name << ": fixture="
                          << expected.vector_count << " actual=" << directions.size() << "\n";
                return 1;
            }
            for (std::size_t v = 0; v < directions.size(); ++v) {
                for (std::size_t e = 0; e < 3; ++e) {
                    const double error = std::abs(directions[v][e] - expected.vectors[v * 3 + e]);
                    max_abs_error = std::max(max_abs_error, error);
                    if (error > tolerance) {
                        std::cerr << "Direction vector mismatch for case " << shape_case.name << " vector " << v
                                  << " element " << e << ": fixture=" << expected.vectors[v * 3 + e]
                                  << " actual=" << directions[v][e] << "\n";
                        return 1;
                    }
                }
            }
        }

        std::cout << "Array shape parity passed\n";
        std::cout << "cases=" << catalogue.size() << " max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
