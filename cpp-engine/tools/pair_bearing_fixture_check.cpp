#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/localisation/PairBearingLocaliser.h"

namespace {

struct PairBearingFixtureRow {
    std::string case_name;
    double angle_radians = 0.0;
    double error_radians = 0.0;
};

struct PairBearingCase {
    std::string name;
    pamguard::localisation::PairBearingConfig config;
    std::vector<double> delays_seconds;
};

// Case parameters shared by name with the PAMGuard Java fixture exporter
// (reference-tools/.../PairBearingFixtureExporter.java).
std::vector<PairBearingCase> case_catalogue() {
    pamguard::localisation::PairBearingConfig base;
    base.spacing_m = 3.0;
    base.spacing_error_m = 0.01;
    base.speed_of_sound_mps = 1500.0;
    base.speed_of_sound_error_mps = 5.0;
    base.timing_error_seconds = 1.0e-5;
    base.wobble_radians = 0.001;

    auto negative_spacing = base;
    negative_spacing.spacing_m = -3.0;

    return {
        {"broadside", base, {0.0}},
        {"mid-cone-positive", base, {0.001}},
        {"mid-cone-negative", base, {-0.001}},
        {"near-endfire", base, {0.00199}},
        {"endfire-clamp", base, {0.0025}},
        {"negative-spacing", negative_spacing, {0.001}},
        {"vancouver-three-delays", base, {0.9, 0.001, 0.9}},
    };
}

std::vector<PairBearingFixtureRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<PairBearingFixtureRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line == "case,angleRadians,errorRadians") {
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
        rows.push_back(PairBearingFixtureRow{cells[0], std::stod(cells[1]), std::stod(cells[2])});
    }
    if (rows.empty()) {
        throw std::runtime_error("fixture did not contain any case rows");
    }
    return rows;
}

bool values_match(double expected, double actual, double tolerance) {
    if (std::isnan(expected) && std::isnan(actual)) {
        return true;
    }
    if (std::isinf(expected) || std::isinf(actual)) {
        return expected == actual;
    }
    return std::abs(expected - actual) <= tolerance;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: pair_bearing_fixture_check <fixture.csv>\n";
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

        bool rejected_zero_spacing = false;
        try {
            auto bad_config = catalogue.front().config;
            bad_config.spacing_m = 0.0;
            pamguard::localisation::PairBearingLocaliser bad_localiser(bad_config);
            (void)bad_localiser;
        }
        catch (const std::invalid_argument&) {
            rejected_zero_spacing = true;
        }
        if (!rejected_zero_spacing) {
            std::cerr << "Pair bearing localiser should reject zero spacing\n";
            return 1;
        }

        {
            pamguard::localisation::PairBearingLocaliser localiser(catalogue.front().config);
            if (localiser.localise({}).has_value()) {
                std::cerr << "Pair bearing localiser should return no result for empty delays\n";
                return 1;
            }
        }

        constexpr double tolerance = 1e-12;
        double max_abs_error = 0.0;
        for (std::size_t i = 0; i < catalogue.size(); ++i) {
            const auto& pair_case = catalogue[i];
            const auto& expected = fixture[i];
            if (expected.case_name != pair_case.name) {
                std::cerr << "Fixture case order mismatch at row " << i << ": fixture=" << expected.case_name
                          << " catalogue=" << pair_case.name << "\n";
                return 1;
            }

            pamguard::localisation::PairBearingLocaliser localiser(pair_case.config);
            const auto result = localiser.localise(pair_case.delays_seconds);
            if (!result.has_value()) {
                std::cerr << "Case " << pair_case.name << " unexpectedly produced no result\n";
                return 1;
            }

            if (!values_match(expected.angle_radians, result->angle_radians, tolerance) ||
                !values_match(expected.error_radians, result->error_radians, tolerance)) {
                std::cerr << "Pair bearing parity failed for case " << pair_case.name << "\n";
                std::cerr << "expected angle/error=" << expected.angle_radians << "/" << expected.error_radians << "\n";
                std::cerr << "actual   angle/error=" << result->angle_radians << "/" << result->error_radians << "\n";
                return 1;
            }

            if (std::isfinite(expected.angle_radians) && std::isfinite(result->angle_radians)) {
                max_abs_error = std::max(max_abs_error, std::abs(expected.angle_radians - result->angle_radians));
            }
            if (std::isfinite(expected.error_radians) && std::isfinite(result->error_radians)) {
                max_abs_error = std::max(max_abs_error, std::abs(expected.error_radians - result->error_radians));
            }
        }

        std::cout << "Pair bearing parity passed\n";
        std::cout << "cases=" << catalogue.size() << " max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
