#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/localisation/MlGridBearingLocaliser.h"

namespace {

using Vec3 = std::array<double, 3>;
using pamguard::localisation::ArrayShapeType;
using pamguard::localisation::MlGridBearingConfig;
using pamguard::localisation::MlGridHydrophone;

struct GridRow {
    std::string name;
    int array_type = 0;
    std::size_t theta_bins = 0;
    std::size_t phi_bins = 0;
    double theta_radians = 0.0;
    double theta_error_radians = 0.0;
    double phi_radians = 0.0;
    double phi_error_radians = 0.0;
    bool has_phi = false;
    double peak_log_likelihood = 0.0;
    std::vector<double> delays_seconds;
};

/**
 * Geometry, errors, and per-case constants mirroring
 * MlGridBearingFixtureExporter's catalogue. The delays themselves come from
 * the fixture, not from here.
 */
struct GridCase {
    std::vector<Vec3> positions;
    std::vector<Vec3> position_errors;
    double speed_of_sound_error_mps = 0.0;
    double timing_error_seconds = 0.0;
};

const std::vector<Vec3> kPlanePositions = {
    {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {2.0, 3.0, 0.0}};
const std::vector<Vec3> kVolumePositions = {
    {0.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {0.5, 1.0, 2.5}};
const std::vector<Vec3> kLinePositions = {{0.0, 0.0, 0.0}, {0.0, 2.0, 0.0}, {0.0, 4.0, 0.0}};
const std::vector<Vec3> kLineFour = {
    {0.0, 0.0, 0.0}, {0.0, 1.5, 0.0}, {0.0, 3.0, 0.0}, {0.0, 4.5, 0.0}};
const std::vector<Vec3> kErrors4 = {{0.1, 0.1, 0.1}, {0.1, 0.1, 0.1}, {0.1, 0.1, 0.1}, {0.1, 0.1, 0.1}};
const std::vector<Vec3> kErrors3 = {{0.1, 0.1, 0.1}, {0.1, 0.1, 0.1}, {0.1, 0.1, 0.1}};
const std::vector<Vec3> kAsymmetricErrors4 = {
    {0.05, 0.2, 0.3}, {0.1, 0.1, 0.1}, {0.4, 0.05, 0.15}, {0.2, 0.2, 0.02}};

std::map<std::string, GridCase> case_catalogue() {
    return {
        {"plane-broadside", {kPlanePositions, kErrors4, 0.0, 1.0e-5}},
        {"plane-diagonal", {kPlanePositions, kErrors4, 0.0, 1.0e-5}},
        {"plane-endfire", {kPlanePositions, kErrors4, 0.0, 1.0e-5}},
        {"plane-elevated", {kPlanePositions, kErrors4, 0.0, 1.0e-5}},
        {"plane-sos-error", {kPlanePositions, kErrors4, 30.0, 1.0e-5}},
        {"plane-asymmetric-errors", {kPlanePositions, kAsymmetricErrors4, 10.0, 2.0e-5}},
        {"volume-above", {kVolumePositions, kErrors4, 0.0, 1.0e-5}},
        {"volume-below", {kVolumePositions, kErrors4, 0.0, 1.0e-5}},
        {"volume-sos-error", {kVolumePositions, kErrors4, 25.0, 3.0e-5}},
        {"line-broadside", {kLinePositions, kErrors3, 0.0, 1.0e-5}},
        {"line-oblique", {kLinePositions, kErrors3, 0.0, 1.0e-5}},
        // Line-convention (MLLineBearingLocaliser2) cases only.
        {"line-endfire", {kLinePositions, kErrors3, 0.0, 1.0e-5}},
        {"line-four-oblique", {kLineFour, kErrors4, 20.0, 2.0e-5}},
    };
}

std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> cells;
    std::stringstream stream(text);
    std::string cell;
    while (std::getline(stream, cell, delimiter)) {
        cells.push_back(cell);
    }
    return cells;
}

std::vector<GridRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }
    std::vector<GridRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line.rfind("case,arrayType", 0) == 0) {
            continue;
        }
        const auto cells = split(line, ',');
        if (cells.size() != 11) {
            throw std::runtime_error("grid row must have eleven columns: " + line);
        }
        GridRow row;
        row.name = cells[0];
        row.array_type = std::stoi(cells[1]);
        row.theta_bins = static_cast<std::size_t>(std::stoul(cells[2]));
        row.phi_bins = static_cast<std::size_t>(std::stoul(cells[3]));
        row.theta_radians = std::stod(cells[4]);
        row.theta_error_radians = std::stod(cells[5]);
        row.phi_radians = std::stod(cells[6]);
        row.phi_error_radians = std::stod(cells[7]);
        row.has_phi = std::stoi(cells[8]) != 0;
        row.peak_log_likelihood = std::stod(cells[9]);
        for (const auto& delay : split(cells[10], ';')) {
            row.delays_seconds.push_back(std::stod(delay));
        }
        rows.push_back(row);
    }
    if (rows.empty()) {
        throw std::runtime_error("fixture did not contain any grid rows");
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: ml_grid_bearing_fixture_check <fixture.csv> [grid|line]\n";
        return 2;
    }
    // "line" selects MLLineBearingLocaliser2's theta convention, which the
    // reference reaches by subclassing and which changes the delay table as
    // well as the reported angle.
    const bool line_convention = argc == 3 && std::string(argv[2]) == "line";

    try {
        const auto fixture = read_fixture(argv[1]);
        const auto catalogue = case_catalogue();
        // Angles are interpolated from log likelihoods accumulated across the
        // whole surface, yet the observed spread is ~7e-17 — the ported matrix
        // inverse and peak search track Jama and PeakSearch essentially
        // bitwise, so the tolerance is set to catch any real drift.
        constexpr double tolerance = 1e-13;
        double max_abs_error = 0.0;

        for (const auto& row : fixture) {
            const auto found = catalogue.find(row.name);
            if (found == catalogue.end()) {
                std::cerr << "Fixture case " << row.name << " has no geometry in the check catalogue\n";
                return 1;
            }
            const auto& grid_case = found->second;

            MlGridBearingConfig config;
            for (std::size_t i = 0; i < grid_case.positions.size(); ++i) {
                MlGridHydrophone hydrophone;
                hydrophone.position_m = grid_case.positions[i];
                hydrophone.position_error_m = grid_case.position_errors[i];
                config.hydrophones.push_back(hydrophone);
            }
            config.speed_of_sound_mps = 1500.0;
            config.speed_of_sound_error_mps = grid_case.speed_of_sound_error_mps;
            config.timing_error_seconds = grid_case.timing_error_seconds;
            config.line_theta_convention = line_convention;

            const pamguard::localisation::MlGridBearingLocaliser localiser(config);
            if (!localiser.prepared()) {
                std::cerr << "Case " << row.name << " did not prepare a grid\n";
                return 1;
            }
            if (static_cast<int>(localiser.array_type()) != row.array_type) {
                std::cerr << "Case " << row.name << " array type mismatch: fixture=" << row.array_type
                          << " ported=" << static_cast<int>(localiser.array_type()) << "\n";
                return 1;
            }
            if (localiser.theta_bin_count() != row.theta_bins || localiser.phi_bin_count() != row.phi_bins) {
                std::cerr << "Case " << row.name << " grid size mismatch: fixture=" << row.theta_bins << "x"
                          << row.phi_bins << " ported=" << localiser.theta_bin_count() << "x"
                          << localiser.phi_bin_count() << "\n";
                return 1;
            }

            const auto result = localiser.localise(row.delays_seconds);
            if (!result.has_value()) {
                std::cerr << "Case " << row.name << " produced no bearing\n";
                return 1;
            }
            if (result->has_phi != row.has_phi) {
                std::cerr << "Case " << row.name << " elevation presence mismatch\n";
                return 1;
            }

            const std::array<std::pair<const char*, std::pair<double, double>>, 4> comparisons = {{
                {"theta", {row.theta_radians, result->theta_radians}},
                {"thetaError", {row.theta_error_radians, result->theta_error_radians}},
                {"phi", {row.phi_radians, result->phi_radians}},
                {"phiError", {row.phi_error_radians, result->phi_error_radians}},
            }};
            for (const auto& [label, values] : comparisons) {
                const double error = std::abs(values.first - values.second);
                max_abs_error = std::max(max_abs_error, error);
                if (error > tolerance) {
                    std::cerr << "Case " << row.name << " " << label << " mismatch: fixture=" << values.first
                              << " ported=" << values.second << "\n";
                    return 1;
                }
            }
        }

        // A sub-array with no spatial extent has no grid to search.
        MlGridBearingConfig point_config;
        point_config.hydrophones = {MlGridHydrophone{{1.0, 2.0, 3.0}, {0.1, 0.1, 0.1}, 0},
                                    MlGridHydrophone{{1.0, 2.0, 3.0}, {0.1, 0.1, 0.1}, 0}};
        const pamguard::localisation::MlGridBearingLocaliser point_localiser(point_config);
        if (point_localiser.prepared() || point_localiser.array_type() != ArrayShapeType::Point) {
            std::cerr << "A co-located pair should be a point array with no prepared grid\n";
            return 1;
        }

        // Zero delay errors would divide by zero across the whole surface.
        MlGridBearingConfig zero_error_config;
        zero_error_config.hydrophones = {MlGridHydrophone{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 0},
                                         MlGridHydrophone{{2.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 0},
                                         MlGridHydrophone{{0.0, 3.0, 0.0}, {0.0, 0.0, 0.0}, 0}};
        const pamguard::localisation::MlGridBearingLocaliser zero_error_localiser(zero_error_config);
        if (zero_error_localiser.localise({0.0, 0.0, 0.0}).has_value()) {
            std::cerr << "A grid with zero delay errors should not report a bearing\n";
            return 1;
        }

        // A delay set of the wrong length is rejected rather than padded.
        const pamguard::localisation::MlGridBearingLocaliser plane_localiser([] {
            MlGridBearingConfig config;
            for (std::size_t i = 0; i < kPlanePositions.size(); ++i) {
                config.hydrophones.push_back(MlGridHydrophone{kPlanePositions[i], kErrors4[i], 0});
            }
            config.timing_error_seconds = 1.0e-5;
            return config;
        }());
        if (plane_localiser.localise({0.0, 0.0, 0.0}).has_value()) {
            std::cerr << "A delay set of the wrong length should be rejected\n";
            return 1;
        }

        std::cout << (line_convention ? "ML line bearing parity passed\n" : "ML grid bearing parity passed\n");
        std::cout << "cases=" << fixture.size() << " max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
