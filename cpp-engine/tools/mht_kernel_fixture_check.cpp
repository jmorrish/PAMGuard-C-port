#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/MhtKernel.h"

namespace {

using pamguard::detectors::MhtBitset;
using pamguard::detectors::MhtChi2;
using pamguard::detectors::MhtChi2Provider;
using pamguard::detectors::MhtKernel;

/**
 * Deterministic test chi2, shared definition with the Java exporter
 * (reference-tools/.../MhtKernelFixtureExporter.java): IDI smoothness over
 * included units plus 0.1 per excluded unit; coasts are the trailing
 * excluded count.
 */
class TestChi2 final : public MhtChi2<std::int64_t> {
public:
    explicit TestChi2(std::shared_ptr<std::vector<std::int64_t>> all_times)
        : all_times_(std::move(all_times)) {}

    [[nodiscard]] double get_chi2() const override { return chi2_; }
    [[nodiscard]] int get_n_coasts() const override { return n_coasts_; }

    void update(const std::int64_t& detection, const MhtBitset& track_bits, std::size_t kcount) override {
        (void)detection;
        std::vector<std::int64_t> included;
        int last_included = -1;
        for (std::size_t i = 0; i < kcount; ++i) {
            if (track_bits.get(i)) {
                included.push_back((*all_times_)[i]);
                last_included = static_cast<int>(i);
            }
        }

        double value = 0.0;
        if (included.size() >= 3) {
            for (std::size_t j = 2; j < included.size(); ++j) {
                const double idi1 = static_cast<double>(included[j - 1] - included[j - 2]) / 1E9;
                const double idi2 = static_cast<double>(included[j] - included[j - 1]) / 1E9;
                value += std::pow(idi2 - idi1, 2.0) / std::pow(std::max(idi1 * 0.2, 5E-4), 2.0);
            }
            value = value / static_cast<double>(included.size() - 2);
        }
        value += 0.1 * static_cast<double>(kcount - included.size());
        chi2_ = value;
        n_coasts_ = last_included < 0 ? static_cast<int>(kcount) : static_cast<int>(kcount) - 1 - last_included;
    }

    [[nodiscard]] std::unique_ptr<MhtChi2<std::int64_t>> clone_chi2() const override {
        auto clone = std::make_unique<TestChi2>(all_times_);
        clone->chi2_ = chi2_;
        clone->n_coasts_ = n_coasts_;
        return clone;
    }

private:
    std::shared_ptr<std::vector<std::int64_t>> all_times_;
    double chi2_ = 0.0;
    int n_coasts_ = 0;
};

class TestChi2Provider final : public MhtChi2Provider<std::int64_t> {
public:
    TestChi2Provider() : all_times_(std::make_shared<std::vector<std::int64_t>>()) {}

    void add_detection(const std::int64_t& detection, std::size_t kcount) override {
        (void)kcount;
        all_times_->push_back(detection);
    }

    [[nodiscard]] std::unique_ptr<MhtChi2<std::int64_t>> new_chi2() override {
        return std::make_unique<TestChi2>(all_times_);
    }

    void clear() override { all_times_->clear(); }

private:
    std::shared_ptr<std::vector<std::int64_t>> all_times_;
};

struct KernelCase {
    std::string name;
    std::vector<double> times_ms;
};

std::vector<KernelCase> case_catalogue() {
    return {
        {"single-train-then-gap",
         {1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 20000, 40000, 60000, 80000}},
        {"two-interleaved-trains",
         {1000, 1040, 1100, 1170, 1200, 1300, 1400, 1430, 1500, 1560, 1600, 1690, 1700, 1820, 1800, 1950}},
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

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: mht_kernel_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);
        std::size_t row_index = 0;

        for (const auto& kernel_case : case_catalogue()) {
            MhtKernel<std::int64_t> kernel(std::make_unique<TestChi2Provider>());

            for (std::size_t k = 0; k < kernel_case.times_ms.size(); ++k) {
                kernel.add_detection(static_cast<std::int64_t>(kernel_case.times_ms[k] * 1'000'000.0));
                if (row_index >= fixture.size()) {
                    std::cerr << "Fixture ended early at case " << kernel_case.name << " step " << k + 1 << "\n";
                    return 1;
                }
                const auto& row = fixture[row_index++];
                if (row.case_name != kernel_case.name || row.record != "step" ||
                    std::stoull(row.value1) != kernel.kcount() ||
                    std::stoull(row.value2) != kernel.possibility_count() ||
                    std::stoull(row.value3) != kernel.confirmed_track_count()) {
                    std::cerr << "Kernel step mismatch at case " << kernel_case.name << " k=" << kernel.kcount()
                              << ": fixture " << row.value1 << "/" << row.value2 << "/" << row.value3
                              << " actual " << kernel.kcount() << "/" << kernel.possibility_count() << "/"
                              << kernel.confirmed_track_count() << "\n";
                    return 1;
                }
            }

            kernel.confirm_remaining_tracks();
            const auto& final_row = fixture[row_index++];
            if (final_row.record != "final" || std::stoull(final_row.value2) != kernel.confirmed_track_count()) {
                std::cerr << "Kernel final mismatch at case " << kernel_case.name << ": fixture "
                          << final_row.value2 << " confirmed, actual " << kernel.confirmed_track_count() << "\n";
                return 1;
            }

            for (std::size_t i = 0; i < kernel.confirmed_track_count(); ++i) {
                const auto& row = fixture[row_index++];
                const auto& track = kernel.confirmed_track(i);
                const auto bits = track.bits.to_string(kernel_case.times_ms.size());
                const double expected_chi2 = std::stod(row.value2);
                if (row.record != "confirmed" || row.value1 != bits ||
                    std::abs(expected_chi2 - track.get_chi2()) > 1e-12 ||
                    std::stoull(row.value3) != track.bits.cardinality()) {
                    std::cerr << "Confirmed track mismatch at case " << kernel_case.name << " index " << i << "\n";
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

        std::cout << "MHT kernel parity passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
