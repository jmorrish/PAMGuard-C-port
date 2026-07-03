#include <fstream>
#include <iostream>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/ConnectedRegionTracker.h"

namespace {

struct FixtureRow {
    std::size_t region_number = 0;
    std::size_t channel = 0;
    std::size_t first_slice = 0;
    std::size_t num_slices = 0;
    std::size_t total_pixels = 0;
    int freq_low = 0;
    int freq_high = 0;
    std::string times;
    std::string peak_bins;
    std::string peaks;
};

std::vector<std::string> split(const std::string& line, char delimiter) {
    std::stringstream stream(line);
    std::string cell;
    std::vector<std::string> cells;
    while (std::getline(stream, cell, delimiter)) {
        cells.push_back(cell);
    }
    return cells;
}

FixtureRow read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.find("regionNumber,channel,firstSlice") != std::string::npos) {
            continue;
        }
        const auto cells = split(line, ',');
        if (cells.size() != 10) {
            continue;
        }
        FixtureRow row;
        row.region_number = static_cast<std::size_t>(std::stoull(cells[0]));
        row.channel = static_cast<std::size_t>(std::stoull(cells[1]));
        row.first_slice = static_cast<std::size_t>(std::stoull(cells[2]));
        row.num_slices = static_cast<std::size_t>(std::stoull(cells[3]));
        row.total_pixels = static_cast<std::size_t>(std::stoull(cells[4]));
        row.freq_low = std::stoi(cells[5]);
        row.freq_high = std::stoi(cells[6]);
        row.times = cells[7];
        row.peak_bins = cells[8];
        row.peaks = cells[9];
        return row;
    }
    throw std::runtime_error("fixture did not contain a connected region row");
}

std::string join_ints(const std::vector<int>& values) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out += "|";
        }
        out += std::to_string(values[i]);
    }
    return out;
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

bool near(double actual, double expected, double tolerance = 1.0e-9) {
    return std::fabs(actual - expected) <= tolerance;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: connected_region_fixture_check <fixture.csv> [close|flush]\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);
        const std::string mode = argc == 3 ? argv[2] : "close";

        pamguard::detectors::ConnectedRegionConfig config;
        config.channel = 0;
        config.slice_height = 16;
        config.sample_rate_hz = 48000;
        config.min_pixels = 3;
        config.min_length = 3;
        config.connect_type = 8;
        pamguard::detectors::ConnectedRegionTracker tracker(config);

        std::vector<pamguard::detectors::ConnectedRegionResult> completed;
        auto append = [&](std::vector<pamguard::detectors::ConnectedRegionResult> regions) {
            completed.insert(completed.end(), regions.begin(), regions.end());
        };

        append(tracker.process_slice(10, 13000, 1010, slice_with({5, 6}), magnitudes()));
        append(tracker.process_slice(11, 13064, 1011, slice_with({6, 7}), magnitudes()));
        append(tracker.process_slice(12, 13128, 1012, slice_with({7}), magnitudes()));
        if (mode == "close") {
            append(tracker.process_slice(13, 13192, 1013, slice_with({}), magnitudes()));
        }
        else if (mode == "flush") {
            append(tracker.flush());
        }
        else {
            throw std::invalid_argument("unknown connected region fixture mode: " + mode);
        }

        if (completed.size() != 1) {
            std::cerr << "Expected one completed region, got " << completed.size() << "\n";
            return 1;
        }

        const auto& region = completed.front();
        if (region.region_number != fixture.region_number ||
            region.channel != fixture.channel ||
            region.first_slice != fixture.first_slice ||
            region.slices.size() != fixture.num_slices ||
            region.total_pixels != fixture.total_pixels ||
            region.freq_range.size() != 2 ||
            region.freq_range[0] != fixture.freq_low ||
            region.freq_range[1] != fixture.freq_high ||
            join_ints(region.times_bins) != fixture.times ||
            join_ints(region.peak_freqs_bins) != fixture.peak_bins ||
            peak_info(region) != fixture.peaks) {
            std::cerr << "Connected region parity failed\n";
            std::cerr << "expected times/peaks=" << fixture.times << " / " << fixture.peaks << "\n";
            std::cerr << "actual   times/peaks=" << join_ints(region.times_bins) << " / " << peak_info(region) << "\n";
            return 1;
        }

        if (region.start_sample != 13000 ||
            region.last_start_sample != 13128 ||
            region.time_span_samples != 128 ||
            region.duration_samples != 192 ||
            region.time_span_ms != 2 ||
            !near(region.time_span_seconds, 128.0 / 48000.0) ||
            !near(region.duration_seconds, 192.0 / 48000.0) ||
            region.min_frequency_bin != 5 ||
            region.max_frequency_bin != 7 ||
            region.frequency_span_bins != 2 ||
            region.min_peak_bin != 6 ||
            region.max_peak_bin != 7 ||
            !near(region.mean_peak_bin, 20.0 / 3.0) ||
            region.start_peak_bin != 6 ||
            region.end_peak_bin != 7 ||
            !near(region.peak_sweep_rate_bins_per_second, 375.0)) {
            std::cerr << "Connected region summary metric check failed\n";
            return 1;
        }

        std::cout << "Connected region parity passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
