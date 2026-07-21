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

using pamguard::detectors::MhtChi2Unit;
using pamguard::detectors::MhtTimeDelayChi2Config;
using pamguard::detectors::MhtTimeDelayChi2Delta;

struct Chi2FixtureRow {
    std::string case_name;
    double chi2 = 0.0;
};

struct DelayCase {
    std::string name;
    std::vector<double> times_ms;
    std::vector<std::vector<double>> delays_seconds;
};

// Case parameters shared by name with the PAMGuard Java fixture exporter
// (reference-tools/.../MhtTimeDelayChi2FixtureExporter.java).
std::vector<DelayCase> case_catalogue() {
    const std::vector<double> times{1000, 1100, 1200, 1300, 1400};
    return {
        {"constant-three-pairs", times,
         {{1e-4, 2e-4, 3e-4}, {1e-4, 2e-4, 3e-4}, {1e-4, 2e-4, 3e-4},
          {1e-4, 2e-4, 3e-4}, {1e-4, 2e-4, 3e-4}}},
        {"linear-drift", times,
         {{1e-4, 2e-4, 3e-4}, {1.1e-4, 2.1e-4, 3.1e-4}, {1.2e-4, 2.2e-4, 3.2e-4},
          {1.3e-4, 2.3e-4, 3.3e-4}, {1.4e-4, 2.4e-4, 3.4e-4}}},
        {"one-bad-pair", times,
         {{1e-4, 2e-4, 3e-4}, {1.1e-4, 2.1e-4, 9e-4}, {1.2e-4, 2.2e-4, 1e-5},
          {1.3e-4, 2.3e-4, 8e-4}, {1.4e-4, 2.4e-4, 2e-5}}},
        {"two-bad-pairs", times,
         {{1e-4, 5e-4, 3e-4}, {1.1e-4, 1e-5, 9e-4}, {1.2e-4, 6e-4, 1e-5},
          {1.3e-4, 2e-5, 8e-4}, {1.4e-4, 7e-4, 2e-5}}},
        {"single-pair", times, {{1e-4}, {5e-4}, {2e-4}, {9e-4}, {3e-4}}},
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
        std::cerr << "Usage: mht_time_delay_chi2_fixture_check <fixture.csv>\n";
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
            const auto& delay_case = catalogue[i];
            const auto& expected = fixture[i];
            if (expected.case_name != delay_case.name) {
                std::cerr << "Fixture case order mismatch at row " << i << ": fixture=" << expected.case_name
                          << " catalogue=" << delay_case.name << "\n";
                return 1;
            }

            MhtTimeDelayChi2Delta chi2_var{MhtTimeDelayChi2Config{}};
            double actual = 0.0;
            for (std::size_t k = 0; k < delay_case.times_ms.size(); ++k) {
                MhtChi2Unit unit;
                unit.time_ns = static_cast<std::int64_t>(delay_case.times_ms[k] * 1'000'000.0);
                actual = chi2_var.update_chi2(unit, delay_case.delays_seconds[k], true, k + 1, k + 1);
            }

            const double error = std::abs(actual - expected.chi2);
            const double relative = std::abs(expected.chi2) > 1.0 ? error / std::abs(expected.chi2) : error;
            max_rel_error = std::max(max_rel_error, relative);
            if (relative > tolerance) {
                std::cerr << "MHT time delay chi2 parity failed for case " << delay_case.name << "\n";
                std::cerr << "expected chi2=" << expected.chi2 << "\n";
                std::cerr << "actual   chi2=" << actual << "\n";
                return 1;
            }
        }

        std::cout << "MHT time delay chi2 parity passed\n";
        std::cout << "cases=" << catalogue.size() << " max_rel_error=" << max_rel_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
