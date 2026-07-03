#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/ConnectedRegionTracker.h"

namespace {

std::size_t read_expected_fragments(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }
    std::string header;
    std::string value;
    std::getline(input, header);
    std::getline(input, value);
    return static_cast<std::size_t>(std::stoull(value));
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

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: connected_region_discard_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto expected_fragments = read_expected_fragments(argv[1]);

        pamguard::detectors::ConnectedRegionConfig config;
        config.channel = 0;
        config.slice_height = 16;
        config.sample_rate_hz = 48000;
        config.min_pixels = 4;
        config.min_length = 3;
        config.connect_type = 8;
        config.keep_shape_stubs = true;
        config.fragmentation_method = 1;
        config.reject_first_quarter_second = false;

        pamguard::detectors::ConnectedRegionTracker tracker(config);
        std::vector<pamguard::detectors::ConnectedRegionResult> completed;
        auto append = [&](std::vector<pamguard::detectors::ConnectedRegionResult> regions) {
            completed.insert(completed.end(), regions.begin(), regions.end());
        };

        append(tracker.process_slice(30, 30000, 1030, slice_with({5, 6, 7, 8, 9, 10, 11}), magnitudes()));
        append(tracker.process_slice(31, 30064, 1031, slice_with({5, 6, 11}), magnitudes()));
        append(tracker.process_slice(32, 30128, 1032, slice_with({5, 6, 11}), magnitudes()));
        append(tracker.process_slice(33, 30192, 1033, slice_with({}), magnitudes()));

        if (completed.size() != expected_fragments) {
            std::cerr << "Connected region discard fragmenter parity failed\n";
            std::cerr << "expected fragments=" << expected_fragments << " actual=" << completed.size() << "\n";
            return 1;
        }

        std::cout << "Connected region discard fragmenter parity passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
