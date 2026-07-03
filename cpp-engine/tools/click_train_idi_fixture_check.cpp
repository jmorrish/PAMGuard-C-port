#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/ClickTrainTracker.h"

namespace {

struct IdiFixtureRow {
    std::string scenario;
    std::size_t unit_count = 0;
    double mean_idi_seconds = 0.0;
    double median_idi_seconds = 0.0;
    double std_idi_seconds = 0.0;
};

struct IdiScenario {
    std::string name;
    std::vector<std::int64_t> click_times_ms;
};

constexpr double sample_rate_hz = 48000.0;

// Scenario click times shared by name with the PAMGuard Java fixture exporter
// (reference-tools/.../ClickTrainIdiFixtureExporter.java). The unsorted Java
// scenario is fed in time order here: the streaming tracker's ingest contract
// is ordered clicks, and the fixture row pins that PAMGuard's internal sort
// yields the same statistics as the sorted series.
std::vector<IdiScenario> scenario_catalogue() {
    return {
        {"regular-100ms", {1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700}},
        {"jittered-even-idis", {1000, 1080, 1200, 1295, 1400, 1510, 1600}},
        {"jittered-odd-idis", {1000, 1080, 1200, 1295, 1400, 1510, 1600, 1700}},
        {"three-click-minimum", {1000, 1120, 1250}},
        {"unsorted-jittered-even-idis", {1000, 1080, 1200, 1295, 1400, 1510, 1600}},
    };
}

std::vector<IdiFixtureRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<IdiFixtureRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line == "scenario,nUnits,meanIdiSeconds,medianIdiSeconds,stdIdiSeconds") {
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

        IdiFixtureRow row;
        row.scenario = cells[0];
        row.unit_count = static_cast<std::size_t>(std::stoull(cells[1]));
        row.mean_idi_seconds = std::stod(cells[2]);
        row.median_idi_seconds = std::stod(cells[3]);
        row.std_idi_seconds = std::stod(cells[4]);
        rows.push_back(row);
    }
    if (rows.empty()) {
        throw std::runtime_error("fixture did not contain any scenario rows");
    }
    return rows;
}

std::vector<pamguard::detectors::ClickDetectionResult> clicks_for(const std::vector<std::int64_t>& click_times_ms) {
    std::vector<pamguard::detectors::ClickDetectionResult> clicks;
    clicks.reserve(click_times_ms.size());
    for (const auto time_ms : click_times_ms) {
        pamguard::detectors::ClickDetectionResult click;
        click.channel_bitmap = 0x1;
        click.trigger_bitmap = 0x1;
        click.time_unix_ms = time_ms;
        // 48 samples per millisecond keeps the tracker's sample-based ICI
        // bitwise identical to PAMGuard's nanosecond-derived IDI values.
        click.start_sample = time_ms * 48;
        click.duration_samples = 16;
        clicks.push_back(click);
    }
    return clicks;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: click_train_idi_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);
        const auto catalogue = scenario_catalogue();
        if (fixture.size() != catalogue.size()) {
            std::cerr << "Fixture scenario count mismatch: fixture=" << fixture.size()
                      << " catalogue=" << catalogue.size() << "\n";
            return 1;
        }

        double max_abs_error = 0.0;
        for (std::size_t i = 0; i < catalogue.size(); ++i) {
            const auto& scenario = catalogue[i];
            const auto& expected = fixture[i];
            if (expected.scenario != scenario.name) {
                std::cerr << "Fixture scenario order mismatch at row " << i << ": fixture=" << expected.scenario
                          << " catalogue=" << scenario.name << "\n";
                return 1;
            }

            pamguard::detectors::ClickTrainConfig config;
            config.sample_rate_hz = sample_rate_hz;
            config.max_ici_seconds = 0.5;
            config.min_clicks = 3;

            pamguard::detectors::ClickTrainTracker tracker(config);
            (void)tracker.process(clicks_for(scenario.click_times_ms));
            const auto completed = tracker.flush();
            if (completed.size() != 1) {
                std::cerr << "Scenario " << scenario.name << " should flush exactly one train, got "
                          << completed.size() << "\n";
                return 1;
            }

            const auto& summary = completed.front();
            if (summary.click_count != expected.unit_count) {
                std::cerr << "Scenario " << scenario.name << " click count mismatch: fixture=" << expected.unit_count
                          << " actual=" << summary.click_count << "\n";
                return 1;
            }

            const double mean_error = std::abs(summary.mean_ici_seconds - expected.mean_idi_seconds);
            const double median_error = std::abs(summary.median_ici_seconds - expected.median_idi_seconds);
            const double std_error = std::abs(summary.std_ici_seconds - expected.std_idi_seconds);
            max_abs_error = std::max({max_abs_error, mean_error, median_error, std_error});
            if (mean_error != 0.0 || median_error != 0.0 || std_error != 0.0) {
                std::cerr << "IDI statistics parity failed for scenario " << scenario.name << "\n";
                std::cerr << "expected mean/median/std=" << expected.mean_idi_seconds << "/"
                          << expected.median_idi_seconds << "/" << expected.std_idi_seconds << "\n";
                std::cerr << "actual   mean/median/std=" << summary.mean_ici_seconds << "/"
                          << summary.median_ici_seconds << "/" << summary.std_ici_seconds << "\n";
                return 1;
            }
        }

        std::cout << "Click train IDI statistics parity passed\n";
        std::cout << "scenarios=" << catalogue.size() << " max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
