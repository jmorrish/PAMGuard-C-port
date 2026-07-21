#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/MhtSimpleChi2Vars.h"

namespace {

using pamguard::detectors::MhtBearingChi2Config;
using pamguard::detectors::MhtBearingChi2Delta;
using pamguard::detectors::MhtChi2Unit;

struct Chi2FixtureRow {
    std::string case_name;
    double chi2 = 0.0;
};

struct BearingCase {
    std::string name;
    std::vector<double> times_ms;
    std::vector<double> bearings_deg;
    bool jump_enable = false;
};

// Case parameters shared by name with the PAMGuard Java fixture exporter
// (reference-tools/.../MhtBearingChi2FixtureExporter.java).
std::vector<BearingCase> case_catalogue() {
    const std::vector<double> times{1000, 1100, 1200, 1300, 1400, 1500};
    return {
        {"constant", times, {40, 40, 40, 40, 40, 40}, false},
        {"linear-sweep", times, {10, 14, 18, 22, 26, 30}, false},
        {"irregular", times, {10, 14, 25, 27, 40, 44}, false},
        {"wraparound", times, {350, 355, 2, 7, 12, 17}, false},
        {"jump-penalty", times, {10, 14, 60, 64, 110, 114}, true},
    };
}

std::vector<Chi2FixtureRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<Chi2FixtureRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line == "case,chi2") {
            continue;
        }
        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 2) {
            throw std::runtime_error("fixture row must have two columns: " + line);
        }
        rows.push_back(Chi2FixtureRow{cells[0], std::stod(cells[1])});
    }
    if (rows.empty()) {
        throw std::runtime_error("fixture did not contain any case rows");
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: mht_bearing_chi2_fixture_check <fixture.csv>\n";
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

        // The wraparound-aware difference is symmetric and never exceeds pi.
        if (std::abs(MhtBearingChi2Delta::bearing_difference(0.1, 6.2) -
                     MhtBearingChi2Delta::bearing_difference(6.2, 0.1)) > 1e-12) {
            std::cerr << "Bearing difference should be symmetric\n";
            return 1;
        }
        if (MhtBearingChi2Delta::bearing_difference(0.0, 4.0) > std::numbers::pi) {
            std::cerr << "Bearing difference should never exceed pi\n";
            return 1;
        }

        constexpr double tolerance = 1e-9;
        double max_rel_error = 0.0;
        for (std::size_t i = 0; i < catalogue.size(); ++i) {
            const auto& bearing_case = catalogue[i];
            const auto& expected = fixture[i];
            if (expected.case_name != bearing_case.name) {
                std::cerr << "Fixture case order mismatch at row " << i << ": fixture=" << expected.case_name
                          << " catalogue=" << bearing_case.name << "\n";
                return 1;
            }

            MhtBearingChi2Config config;
            config.bearing_jump_enable = bearing_case.jump_enable;
            MhtBearingChi2Delta chi2_var(config);

            double actual = 0.0;
            for (std::size_t k = 0; k < bearing_case.times_ms.size(); ++k) {
                MhtChi2Unit unit;
                unit.time_ns = static_cast<std::int64_t>(bearing_case.times_ms[k] * 1'000'000.0);
                unit.bearing_radians = bearing_case.bearings_deg[k] * std::numbers::pi / 180.0;
                actual = chi2_var.update_chi2(unit, true, k + 1, k + 1);
            }

            const double error = std::abs(actual - expected.chi2);
            const double relative = std::abs(expected.chi2) > 1.0 ? error / std::abs(expected.chi2) : error;
            max_rel_error = std::max(max_rel_error, relative);
            if (relative > tolerance) {
                std::cerr << "MHT bearing chi2 parity failed for case " << bearing_case.name << "\n";
                std::cerr << "expected chi2=" << expected.chi2 << "\n";
                std::cerr << "actual   chi2=" << actual << "\n";
                return 1;
            }
        }

        std::cout << "MHT bearing chi2 parity passed\n";
        std::cout << "cases=" << catalogue.size() << " max_rel_error=" << max_rel_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
