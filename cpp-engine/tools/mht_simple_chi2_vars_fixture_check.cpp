#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/MhtSimpleChi2Vars.h"

namespace {

struct Chi2FixtureRow {
    std::string case_name;
    double chi2 = 0.0;
};

struct VarCase {
    std::string name;
    bool length_variable = false;
    bool update = false;
    std::vector<double> times_ms;
    std::vector<double> amplitudes_db;
    std::vector<double> durations_ms;
};

// Case parameters shared by name with the PAMGuard Java fixture exporter
// (reference-tools/.../MhtSimpleChi2VarsFixtureExporter.java).
std::vector<VarCase> case_catalogue() {
    const std::vector<double> steady_times{1000, 1100, 1200, 1300, 1400, 1500};
    const std::vector<double> steady_amps{120, 121, 120.5, 121.5, 120.8, 121.2};
    const std::vector<double> ramp_amps{110, 114, 118, 122, 126, 130};
    const std::vector<double> jump_amps{120, 121, 120.5, 135, 120.8, 121.2};
    const std::vector<double> steady_lengths{0.30, 0.32, 0.31, 0.33, 0.30, 0.32};
    const std::vector<double> wild_lengths{0.30, 0.90, 0.25, 1.10, 0.20, 0.95};
    return {
        {"length-steady-calc", true, false, steady_times, steady_amps, steady_lengths},
        {"length-wild-calc", true, false, steady_times, steady_amps, wild_lengths},
        {"length-steady-update", true, true, steady_times, steady_amps, steady_lengths},
        {"length-wild-update", true, true, steady_times, steady_amps, wild_lengths},
        {"amplitude-steady-update", false, true, steady_times, steady_amps, steady_lengths},
        {"amplitude-ramp-update", false, true, steady_times, ramp_amps, steady_lengths},
        {"amplitude-jump-update", false, true, steady_times, jump_amps, steady_lengths},
        {"amplitude-steady-calc", false, false, steady_times, steady_amps, steady_lengths},
    };
}

std::vector<pamguard::detectors::MhtChi2Unit> units_for(const VarCase& var_case) {
    std::vector<pamguard::detectors::MhtChi2Unit> units;
    units.reserve(var_case.times_ms.size());
    for (std::size_t i = 0; i < var_case.times_ms.size(); ++i) {
        pamguard::detectors::MhtChi2Unit unit;
        unit.time_ns = static_cast<std::int64_t>(var_case.times_ms[i] * 1'000'000.0);
        unit.amplitude_db = var_case.amplitudes_db[i];
        unit.duration_ms = var_case.durations_ms[i];
        units.push_back(unit);
    }
    return units;
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
        std::cerr << "Usage: mht_simple_chi2_vars_fixture_check <fixture.csv>\n";
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

        constexpr double tolerance = 1e-9;
        double max_rel_error = 0.0;
        for (std::size_t i = 0; i < catalogue.size(); ++i) {
            const auto& var_case = catalogue[i];
            const auto& expected = fixture[i];
            if (expected.case_name != var_case.name) {
                std::cerr << "Fixture case order mismatch at row " << i << ": fixture=" << expected.case_name
                          << " catalogue=" << var_case.name << "\n";
                return 1;
            }

            const auto units = units_for(var_case);
            double actual = 0.0;
            if (var_case.length_variable) {
                pamguard::detectors::MhtLengthChi2 chi2_var{pamguard::detectors::MhtLengthChi2Config{}};
                if (var_case.update) {
                    for (std::size_t k = 0; k < units.size(); ++k) {
                        actual = chi2_var.update_chi2(units[k], true, k + 1, k + 1);
                    }
                }
                else {
                    actual = chi2_var.calc_chi2(units);
                }
            }
            else {
                pamguard::detectors::MhtAmplitudeChi2 chi2_var{pamguard::detectors::MhtAmplitudeChi2Config{}};
                if (var_case.update) {
                    for (std::size_t k = 0; k < units.size(); ++k) {
                        actual = chi2_var.update_chi2(units[k], true, k + 1, k + 1);
                    }
                }
                else {
                    actual = chi2_var.calc_chi2(units);
                }
            }

            const double error = std::abs(actual - expected.chi2);
            const double relative = std::abs(expected.chi2) > 1.0 ? error / std::abs(expected.chi2) : error;
            max_rel_error = std::max(max_rel_error, relative);
            if (relative > tolerance) {
                std::cerr << "MHT simple chi2 parity failed for case " << var_case.name << "\n";
                std::cerr << "expected chi2=" << expected.chi2 << "\n";
                std::cerr << "actual   chi2=" << actual << "\n";
                return 1;
            }
        }

        std::cout << "MHT simple chi2 variable parity passed\n";
        std::cout << "cases=" << catalogue.size() << " max_rel_error=" << max_rel_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
