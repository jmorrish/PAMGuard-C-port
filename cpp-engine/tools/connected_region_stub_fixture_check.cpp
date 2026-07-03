#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/ConnectedRegionTracker.h"

namespace {

std::string read_expected_peaks(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }
    std::string header;
    std::string peaks;
    std::getline(input, header);
    std::getline(input, peaks);
    if (peaks.empty()) {
        throw std::runtime_error("stub fixture did not contain a peak row");
    }
    return peaks;
}

std::vector<double> magnitudes() {
    std::vector<double> values(16);
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = 1.0 + static_cast<double>(i);
    }
    return values;
}

std::vector<bool> slice_with(std::initializer_list<int> bins) {
    std::vector<bool> active(16, false);
    for (const int bin : bins) {
        active[static_cast<std::size_t>(bin)] = true;
    }
    return active;
}

std::string peak_info(const pamguard::detectors::ConnectedRegionResult& region) {
    std::string out;
    bool first = true;
    for (const auto& slice : region.slices) {
        for (const auto& peak : slice.peak_info) {
            if (!first) {
                out += "|";
            }
            first = false;
            out += std::to_string(slice.slice_number) + ":" +
                std::to_string(peak[0]) + "-" +
                std::to_string(peak[1]) + "-" +
                std::to_string(peak[2]) + "-" +
                std::to_string(peak[3]);
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: connected_region_stub_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto expected = read_expected_peaks(argv[1]);

        pamguard::detectors::ConnectedRegionConfig config;
        config.channel = 0;
        config.slice_height = 16;
        config.sample_rate_hz = 48000;
        config.min_pixels = 6;
        config.min_length = 3;
        config.connect_type = 8;
        config.keep_shape_stubs = false;
        config.reject_first_quarter_second = false;

        pamguard::detectors::ConnectedRegionTracker tracker(config);
        std::vector<pamguard::detectors::ConnectedRegionResult> completed;
        auto append = [&](std::vector<pamguard::detectors::ConnectedRegionResult> regions) {
            completed.insert(completed.end(), regions.begin(), regions.end());
        };

        append(tracker.process_slice(20, 20000, 1020, slice_with({5, 6, 11}), magnitudes()));
        append(tracker.process_slice(21, 20064, 1021, slice_with({5, 6, 12}), magnitudes()));
        append(tracker.process_slice(22, 20128, 1022, slice_with({5, 6}), magnitudes()));
        append(tracker.process_slice(23, 20192, 1023, slice_with({5, 6}), magnitudes()));
        append(tracker.process_slice(24, 20256, 1024, slice_with({}), magnitudes()));

        if (completed.size() != 1) {
            std::cerr << "Expected one completed region, got " << completed.size() << "\n";
            return 1;
        }
        const auto actual = peak_info(completed.front());
        if (actual != expected) {
            std::cerr << "Connected region stub removal parity failed\n";
            std::cerr << "expected=" << expected << "\n";
            std::cerr << "actual  =" << actual << "\n";
            return 1;
        }

        std::cout << "Connected region stub removal parity passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
