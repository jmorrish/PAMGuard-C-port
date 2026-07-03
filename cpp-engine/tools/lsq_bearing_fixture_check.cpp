#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/localisation/LsqBearingLocaliser.h"

namespace {

struct LsqFixtureRow {
    std::string case_name;
    double azimuth_radians = 0.0;
    double elevation_radians = 0.0;
    double azimuth_error_radians = 0.0;
    double elevation_error_radians = 0.0;
};

struct LsqCase {
    std::string name;
    std::vector<std::array<double, 3>> hydrophones_m;
    std::vector<std::array<double, 3>> pair_error_vectors_m;
    double speed_of_sound_mps = 0.0;
    double speed_of_sound_error_mps = 0.0;
    double timing_error_seconds = 0.0;
    std::vector<double> delays_seconds;
};

// Case parameters shared by name with the PAMGuard Java fixture exporter
// (reference-tools/.../LsqBearingFixtureExporter.java).
std::vector<LsqCase> case_catalogue() {
    return {
        {"volumetric-4ch",
         {{0.0, 0.0, 0.0}, {2.5, 0.0, 0.0}, {0.0, 2.5, 0.0}, {0.0, 0.0, 2.5}},
         {{0.018, 0.003, 0.002},
          {0.002, 0.02, 0.004},
          {0.003, 0.002, 0.022},
          {0.012, 0.011, 0.002},
          {0.013, 0.003, 0.012},
          {0.002, 0.014, 0.015}},
         1500.0, 5.0, 1.0e-5,
         {0.0008, -0.0009, 0.0005, -0.0011, -0.0002, 0.0009}},
        {"towed-4ch",
         {{0.0, 0.0, 0.0}, {0.05, 3.0, 0.02}, {-0.03, 6.0, 0.04}, {0.02, 9.0, 0.5}},
         {{0.002, 0.02, 0.005},
          {0.001, 0.03, 0.004},
          {0.003, 0.025, 0.006},
          {0.002, 0.02, 0.004},
          {0.001, 0.03, 0.008},
          {0.002, 0.025, 0.01}},
         1500.0, 5.0, 1.0e-5,
         {-0.0012, -0.0026, -0.0014, -0.0015, -0.0028, -0.0013}},
        {"volumetric-4ch-alt-weights",
         {{0.0, 0.0, 0.0}, {2.5, 0.0, 0.0}, {0.0, 2.5, 0.0}, {0.0, 0.0, 2.5}},
         {{0.004, 0.001, 0.001},
          {0.001, 0.03, 0.002},
          {0.002, 0.001, 0.012},
          {0.02, 0.018, 0.003},
          {0.006, 0.002, 0.02},
          {0.001, 0.008, 0.007}},
         1480.0, 10.0, 5.0e-5,
         {0.0008, -0.0009, 0.0005, -0.0011, -0.0002, 0.0009}},
        {"wide-aperture-4ch",
         {{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {0.0, 12.0, 0.0}, {3.0, 4.0, 8.0}},
         {{0.03, 0.005, 0.004},
          {0.006, 0.04, 0.005},
          {0.012, 0.01, 0.03},
          {0.025, 0.02, 0.006},
          {0.02, 0.006, 0.024},
          {0.005, 0.022, 0.02}},
         1500.0, 5.0, 1.0e-5,
         {0.004, -0.005, 0.002, -0.006, -0.001, 0.0045}},
    };
}

pamguard::localisation::LsqBearingConfig config_for(const LsqCase& lsq_case) {
    pamguard::localisation::LsqBearingConfig config;
    config.speed_of_sound_mps = lsq_case.speed_of_sound_mps;
    config.speed_of_sound_error_mps = lsq_case.speed_of_sound_error_mps;
    config.timing_error_seconds = lsq_case.timing_error_seconds;

    const auto hydrophone_count = lsq_case.hydrophones_m.size();
    std::size_t row = 0;
    for (std::size_t i = 0; i < hydrophone_count; ++i) {
        for (std::size_t j = i + 1; j < hydrophone_count; ++j) {
            pamguard::localisation::LsqPairGeometry pair;
            for (std::size_t e = 0; e < 3; ++e) {
                pair.baseline_m[e] = lsq_case.hydrophones_m[j][e] - lsq_case.hydrophones_m[i][e];
            }
            pair.error_m = lsq_case.pair_error_vectors_m[row];
            config.pairs.push_back(pair);
            ++row;
        }
    }
    return config;
}

std::vector<LsqFixtureRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<LsqFixtureRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() ||
            line == "case,azimuthRadians,elevationRadians,azimuthErrorRadians,elevationErrorRadians") {
            continue;
        }

        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 5) {
            throw std::runtime_error("fixture row must have five columns: " + line);
        }
        rows.push_back(LsqFixtureRow{cells[0], std::stod(cells[1]), std::stod(cells[2]),
                                     std::stod(cells[3]), std::stod(cells[4])});
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
        std::cerr << "Usage: lsq_bearing_fixture_check <fixture.csv>\n";
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
            // Any 3-hydrophone pair set is rank deficient: the third baseline
            // equals the difference of the other two, where PAMGuard's Jama
            // solve throws. The port must report no result.
            LsqCase degenerate;
            degenerate.hydrophones_m = {{0.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {0.0, 6.0, 0.0}};
            degenerate.pair_error_vectors_m = {{0.0, 0.02, 0.0}, {0.0, 0.03, 0.0}, {0.0, 0.025, 0.0}};
            degenerate.speed_of_sound_mps = 1500.0;
            degenerate.speed_of_sound_error_mps = 5.0;
            degenerate.timing_error_seconds = 1.0e-5;
            degenerate.delays_seconds = {-0.0012, -0.0026, -0.0014};
            pamguard::localisation::LsqBearingLocaliser localiser(config_for(degenerate));
            if (localiser.localise(degenerate.delays_seconds).has_value()) {
                std::cerr << "Collinear three-hydrophone geometry should be rank deficient and yield no result\n";
                return 1;
            }
        }

        {
            pamguard::localisation::LsqBearingLocaliser localiser(config_for(catalogue.front()));
            bool rejected_wrong_count = false;
            try {
                (void)localiser.localise({0.001});
            }
            catch (const std::invalid_argument&) {
                rejected_wrong_count = true;
            }
            if (!rejected_wrong_count) {
                std::cerr << "LSQ bearing localiser should reject delay/pair count mismatch\n";
                return 1;
            }
        }

        constexpr double tolerance = 1e-9;
        double max_abs_error = 0.0;
        for (std::size_t i = 0; i < catalogue.size(); ++i) {
            const auto& lsq_case = catalogue[i];
            const auto& expected = fixture[i];
            if (expected.case_name != lsq_case.name) {
                std::cerr << "Fixture case order mismatch at row " << i << ": fixture=" << expected.case_name
                          << " catalogue=" << lsq_case.name << "\n";
                return 1;
            }

            pamguard::localisation::LsqBearingLocaliser localiser(config_for(lsq_case));
            const auto result = localiser.localise(lsq_case.delays_seconds);
            if (!result.has_value()) {
                std::cerr << "Case " << lsq_case.name << " unexpectedly produced no result\n";
                return 1;
            }

            const std::array<std::pair<double, double>, 4> comparisons{{
                {expected.azimuth_radians, result->azimuth_radians},
                {expected.elevation_radians, result->elevation_radians},
                {expected.azimuth_error_radians, result->azimuth_error_radians},
                {expected.elevation_error_radians, result->elevation_error_radians},
            }};
            for (const auto& [expected_value, actual_value] : comparisons) {
                if (!values_match(expected_value, actual_value, tolerance)) {
                    std::cerr << "LSQ bearing parity failed for case " << lsq_case.name << "\n";
                    std::cerr << "expected az/el/azErr/elErr=" << expected.azimuth_radians << "/"
                              << expected.elevation_radians << "/" << expected.azimuth_error_radians << "/"
                              << expected.elevation_error_radians << "\n";
                    std::cerr << "actual   az/el/azErr/elErr=" << result->azimuth_radians << "/"
                              << result->elevation_radians << "/" << result->azimuth_error_radians << "/"
                              << result->elevation_error_radians << "\n";
                    return 1;
                }
                if (std::isfinite(expected_value) && std::isfinite(actual_value)) {
                    max_abs_error = std::max(max_abs_error, std::abs(expected_value - actual_value));
                }
            }
        }

        std::cout << "LSQ bearing parity passed\n";
        std::cout << "cases=" << catalogue.size() << " max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
