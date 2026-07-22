#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/core/AnalysisSession.h"
#include "pamguard/detectors/SimpleEchoDetector.h"

namespace {

struct EchoRow {
    std::string scenario;
    double sample_rate_hz = 0.0;
    double max_interval_seconds = 0.0;
    std::int64_t start_sample = 0;
    bool is_echo = false;
};

std::vector<EchoRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }
    std::vector<EchoRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty() || line.rfind("scenario,", 0) == 0) {
            continue;
        }
        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 5) {
            throw std::runtime_error("echo row must have five columns: " + line);
        }
        EchoRow row;
        row.scenario = cells[0];
        row.sample_rate_hz = std::stod(cells[1]);
        row.max_interval_seconds = std::stod(cells[2]);
        row.start_sample = std::stoll(cells[3]);
        row.is_echo = std::stoi(cells[4]) != 0;
        rows.push_back(row);
    }
    if (rows.empty()) {
        throw std::runtime_error("fixture did not contain any echo rows");
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: simple_echo_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);

        // One detector per scenario, fed the rows in file order so the
        // anchor state evolves exactly as the Java run's did.
        std::map<std::string, pamguard::detectors::SimpleEchoDetector> detectors;
        std::size_t checked = 0;
        for (const auto& row : fixture) {
            auto found = detectors.find(row.scenario);
            if (found == detectors.end()) {
                found = detectors.emplace(row.scenario,
                                          pamguard::detectors::SimpleEchoDetector(
                                              row.sample_rate_hz, row.max_interval_seconds)).first;
            }
            const bool echo = found->second.is_echo(row.start_sample);
            if (echo != row.is_echo) {
                std::cerr << "Scenario " << row.scenario << " sample " << row.start_sample
                          << ": fixture=" << row.is_echo << " ported=" << echo << "\n";
                return 1;
            }
            ++checked;
        }

        // Session-level gate placement: two transients inside the echo
        // window. Discard mode removes the second click before ANY consumer
        // sees it (features, classifier, trains); flag mode keeps it, marked.
        const auto make_config = [](bool echo_enabled, bool discard) {
            pamguard::core::AnalysisConfig config;
            config.session_id = "echo-gate-check";
            config.sample_rate_hz = 48000;
            config.channel_count = 1;
            config.detector.click_detector_enabled = true;
            config.detector.click.channel_bitmap = 1;
            config.detector.click.trigger_bitmap = 1;
            config.detector.click.threshold_db = 10.0;
            config.detector.click.short_filter = 0.1;
            config.detector.click.long_filter = 0.00001;
            config.detector.click.pre_sample = 4;
            config.detector.click.post_sample = 4;
            config.detector.click.min_sep = 8;
            config.detector.click.max_length = 64;
            config.detector.click.min_trigger_channels = 1;
            config.detector.click_echo_enabled = echo_enabled;
            config.detector.click_echo_discard = discard;
            config.detector.click_echo_max_interval_seconds = 0.01; // 480 samples
            return config;
        };
        const auto make_chunk = [] {
            pamguard::core::AudioChunk chunk;
            chunk.start_sample = 0;
            chunk.sample_rate_hz = 48000;
            chunk.channel_count = 1;
            chunk.interleaved_pcm.assign(1024, 0.0);
            for (std::size_t sample = 0; sample < 1024; ++sample) {
                double value = 0.01 * (static_cast<double>(sample % 7) / 7.0 - 0.5);
                const bool in_first = sample >= 200 && sample <= 206;
                const bool in_second = sample >= 400 && sample <= 406;
                if (in_first || in_second) {
                    value += ((sample & 1u) == 0 ? 1.0 : -1.0);
                }
                chunk.interleaved_pcm[sample] = value;
            }
            return chunk;
        };

        pamguard::core::AnalysisSession plain_session(make_config(false, false));
        const auto plain = plain_session.process(make_chunk());
        if (plain.clicks.size() != 2) {
            std::cerr << "Echo gate scenario should produce two clicks without the gate, got "
                      << plain.clicks.size() << "\n";
            return 1;
        }
        if (plain.clicks[0].echo || plain.clicks[1].echo) {
            std::cerr << "Echo flags should stay false when the gate is off\n";
            return 1;
        }

        pamguard::core::AnalysisSession flag_session(make_config(true, false));
        const auto flagged = flag_session.process(make_chunk());
        if (flagged.clicks.size() != 2 || flagged.clicks[0].echo || !flagged.clicks[1].echo) {
            std::cerr << "Flag mode should keep both clicks and mark only the second as an echo\n";
            return 1;
        }

        pamguard::core::AnalysisSession discard_session(make_config(true, true));
        const auto discarded = discard_session.process(make_chunk());
        if (discarded.clicks.size() != 1 || discarded.clicks[0].echo) {
            std::cerr << "Discard mode should drop the echo before any consumer sees it, got "
                      << discarded.clicks.size() << " clicks\n";
            return 1;
        }

        std::cout << "Simple echo parity passed\n";
        std::cout << "scenarios=" << detectors.size() << " decisions=" << checked << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
