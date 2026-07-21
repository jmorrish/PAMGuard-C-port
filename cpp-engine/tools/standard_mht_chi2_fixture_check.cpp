#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/StandardMhtChi2.h"

namespace {

using pamguard::detectors::MhtChi2Unit;
using pamguard::detectors::MhtKernel;
using pamguard::detectors::StandardMhtChi2Params;
using pamguard::detectors::StandardMhtChi2Provider;

struct StackCase {
    std::string name;
    std::vector<double> times_ms;
    std::vector<double> amplitudes_db;
    std::vector<double> durations_ms;
    bool electrical_noise_filter = false;
};

/** Perfectly uniform detections: the electrical noise signature. */
StackCase uniform_case(const std::string& name, bool filter_on) {
    StackCase stack_case;
    stack_case.name = name;
    stack_case.electrical_noise_filter = filter_on;
    for (int i = 0; i < 34; ++i) {
        stack_case.times_ms.push_back(1000.0 + i * 50.0);
        stack_case.amplitudes_db.push_back(120.0);
        stack_case.durations_ms.push_back(0.30);
    }
    return stack_case;
}

// Case parameters shared by name with the PAMGuard Java fixture exporter
// (reference-tools/.../StandardMhtChi2FixtureExporter.java).
std::vector<StackCase> case_catalogue() {
    return {
        {"steady-train-then-gap",
         {1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 20000, 40000, 60000, 80000},
         {120, 121, 120.5, 121.5, 120.8, 121.2, 120.6, 121.1, 100, 101, 100.5, 101.5},
         {0.30, 0.32, 0.31, 0.33, 0.30, 0.32, 0.31, 0.30, 0.5, 0.52, 0.51, 0.50}},
        {"two-trains-amplitude-split",
         {1000, 1050, 1100, 1150, 1200, 1250, 1300, 1350, 1400, 1450, 1500, 1550},
         {120, 90, 121, 91, 120.5, 90.5, 121.5, 91.5, 120.8, 90.8, 121.2, 91.2},
         {0.30, 0.60, 0.32, 0.62, 0.31, 0.61, 0.33, 0.63, 0.30, 0.60, 0.32, 0.62}},
        uniform_case("uniform-noise-filter-off", false),
        uniform_case("uniform-noise-filter-on", true),
    };
}

struct FixtureRow {
    std::string case_name;
    std::string record;
    std::string value1;
    std::string value2;
    std::string value3;
};

std::vector<FixtureRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<FixtureRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line == "case,record,value1,value2,value3") {
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
        rows.push_back(FixtureRow{cells[0], cells[1], cells[2], cells[3], cells[4]});
    }
    if (rows.empty()) {
        throw std::runtime_error("fixture did not contain any rows");
    }
    return rows;
}

bool chi2_matches(double expected, double actual) {
    if (std::isnan(expected) && std::isnan(actual)) {
        return true;
    }
    const double error = std::abs(expected - actual);
    const double relative = std::abs(expected) > 1.0 ? error / std::abs(expected) : error;
    return relative <= 1e-9;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: standard_mht_chi2_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);
        std::size_t row_index = 0;

        for (const auto& stack_case : case_catalogue()) {
            StandardMhtChi2Params params;
            params.use_electrical_noise_filter = stack_case.electrical_noise_filter;
            pamguard::detectors::MhtKernelParams kernel_params;
            MhtKernel<MhtChi2Unit> kernel(
                std::make_unique<StandardMhtChi2Provider>(params, kernel_params), kernel_params);

            for (std::size_t k = 0; k < stack_case.times_ms.size(); ++k) {
                MhtChi2Unit unit;
                unit.time_ns = static_cast<std::int64_t>(stack_case.times_ms[k] * 1'000'000.0);
                unit.amplitude_db = stack_case.amplitudes_db[k];
                unit.duration_ms = stack_case.durations_ms[k];
                kernel.add_detection(unit);

                const auto& row = fixture[row_index++];
                if (row.case_name != stack_case.name || row.record != "step" ||
                    std::stoull(row.value1) != kernel.kcount() ||
                    std::stoull(row.value2) != kernel.possibility_count() ||
                    std::stoull(row.value3) != kernel.confirmed_track_count()) {
                    std::cerr << "Stack step mismatch at case " << stack_case.name << " k=" << kernel.kcount()
                              << ": fixture " << row.value1 << "/" << row.value2 << "/" << row.value3
                              << " actual " << kernel.kcount() << "/" << kernel.possibility_count() << "/"
                              << kernel.confirmed_track_count() << "\n";
                    return 1;
                }
            }

            kernel.confirm_remaining_tracks();
            const auto& final_row = fixture[row_index++];
            if (final_row.record != "final" || std::stoull(final_row.value2) != kernel.confirmed_track_count()) {
                std::cerr << "Stack final mismatch at case " << stack_case.name << ": fixture "
                          << final_row.value2 << " confirmed, actual " << kernel.confirmed_track_count() << "\n";
                return 1;
            }

            for (std::size_t i = 0; i < kernel.confirmed_track_count(); ++i) {
                const auto& row = fixture[row_index++];
                const auto& track = kernel.confirmed_track(i);
                const auto bits = track.bits.to_string(stack_case.times_ms.size());
                if (row.record != "confirmed" || row.value1 != bits ||
                    !chi2_matches(std::stod(row.value2), track.get_chi2()) ||
                    std::stoull(row.value3) != track.bits.cardinality()) {
                    std::cerr << "Confirmed track mismatch at case " << stack_case.name << " index " << i << "\n";
                    std::cerr << "expected bits/chi2/count=" << row.value1 << "/" << row.value2 << "/" << row.value3 << "\n";
                    std::cerr << "actual   bits/chi2/count=" << bits << "/" << track.get_chi2() << "/"
                              << track.bits.cardinality() << "\n";
                    return 1;
                }
            }
        }

        if (row_index != fixture.size()) {
            std::cerr << "Fixture had " << fixture.size() - row_index << " unconsumed rows\n";
            return 1;
        }

        std::cout << "Standard MHT chi2 stack parity passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
