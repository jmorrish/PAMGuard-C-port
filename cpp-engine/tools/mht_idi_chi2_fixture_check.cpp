#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/MhtIdiChi2.h"

namespace {

struct Chi2FixtureRow {
    std::string case_name;
    double chi2 = 0.0;
};

struct Chi2Case {
    std::string name;
    bool update = false;
    std::vector<double> times_ms;
    std::vector<bool> in_track;
};

// Case parameters shared by name with the PAMGuard Java fixture exporter
// (reference-tools/.../MhtIdiChi2FixtureExporter.java).
std::vector<Chi2Case> case_catalogue() {
    const std::vector<bool> all_in_8(8, true);
    std::vector<bool> skip_fourth(8, true);
    skip_fourth[3] = false;
    return {
        {"steady-calc", false, {1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700}, {}},
        {"jittered-calc", false, {1000, 1100, 1205, 1295, 1405, 1500}, {}},
        {"steady-update", true, {1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700}, all_in_8},
        {"jittered-update", true, {1000, 1100, 1205, 1295, 1405, 1500, 1610, 1700}, all_in_8},
        {"skip-update", true, {1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700}, skip_fourth},
        {"junk-idi-update", true, {1000, 1100, 1200, 1200.2, 1300, 1400, 1500, 1600}, all_in_8},
    };
}

std::vector<std::int64_t> unit_times_ns(const std::vector<double>& times_ms) {
    std::vector<std::int64_t> times_ns;
    times_ns.reserve(times_ms.size());
    for (const double time_ms : times_ms) {
        times_ns.push_back(static_cast<std::int64_t>(time_ms * 1'000'000.0));
    }
    return times_ns;
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
        std::cerr << "Usage: mht_idi_chi2_fixture_check <fixture.csv>\n";
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
            bool rejected_bad_config = false;
            try {
                pamguard::detectors::MhtIdiChi2Config bad_config;
                bad_config.sample_rate_hz = 0.0;
                pamguard::detectors::MhtIdiChi2 bad_chi2(bad_config);
                (void)bad_chi2;
            }
            catch (const std::invalid_argument&) {
                rejected_bad_config = true;
            }
            if (!rejected_bad_config) {
                std::cerr << "MHT IDI chi2 should reject non-positive sample rate\n";
                return 1;
            }
        }

        constexpr double tolerance = 1e-9;
        double max_abs_error = 0.0;
        for (std::size_t i = 0; i < catalogue.size(); ++i) {
            const auto& chi2_case = catalogue[i];
            const auto& expected = fixture[i];
            if (expected.case_name != chi2_case.name) {
                std::cerr << "Fixture case order mismatch at row " << i << ": fixture=" << expected.case_name
                          << " catalogue=" << chi2_case.name << "\n";
                return 1;
            }

            const auto times_ns = unit_times_ns(chi2_case.times_ms);
            double actual;
            if (chi2_case.update) {
                pamguard::detectors::MhtIdiChi2 chi2_var{pamguard::detectors::MhtIdiChi2Config{}};
                std::size_t bitcount = 0;
                actual = 0.0;
                for (std::size_t k = 0; k < times_ns.size(); ++k) {
                    if (chi2_case.in_track[k]) {
                        ++bitcount;
                    }
                    actual = chi2_var.update_chi2(times_ns[k], chi2_case.in_track[k], bitcount, k + 1);
                }
            }
            else {
                const pamguard::detectors::MhtIdiChi2 chi2_var{pamguard::detectors::MhtIdiChi2Config{}};
                actual = chi2_var.calc_chi2(times_ns);
            }

            const double error = std::abs(actual - expected.chi2);
            const double relative = std::abs(expected.chi2) > 1.0 ? error / std::abs(expected.chi2) : error;
            max_abs_error = std::max(max_abs_error, relative);
            if (relative > tolerance) {
                std::cerr << "MHT IDI chi2 parity failed for case " << chi2_case.name << "\n";
                std::cerr << "expected chi2=" << expected.chi2 << "\n";
                std::cerr << "actual   chi2=" << actual << "\n";
                return 1;
            }
        }

        std::cout << "MHT IDI chi2 parity passed\n";
        std::cout << "cases=" << catalogue.size() << " max_rel_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
