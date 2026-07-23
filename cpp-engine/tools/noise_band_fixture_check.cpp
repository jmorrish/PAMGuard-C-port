#include <cmath>
#include <fstream>
#include <iostream>
#include <array>
#include <map>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/NoiseBandMonitor.h"

namespace {

using pamguard::detectors::NoiseBandConfig;
using pamguard::detectors::NoiseBandMonitor;
using pamguard::detectors::NoiseBandType;

struct BandCase {
    NoiseBandType type;
    double min_freq;
    double max_freq;
    double reference;
};

std::map<std::string, BandCase> case_catalogue() {
    using T = NoiseBandType;
    return {
        {"third-octave-48k", {T::ThirdOctave, 10.0, 24000.0, 1000.0}},
        {"third-octave-narrow", {T::ThirdOctave, 100.0, 2000.0, 1000.0}},
        {"octave-96k", {T::Octave, 20.0, 48000.0, 1000.0}},
        {"decidecade-48k", {T::Decidecade, 10.0, 24000.0, 1000.0}},
        {"decade-500k", {T::Decade, 10.0, 250000.0, 1000.0}},
        {"tenth-octave-2k", {T::TenthOctave, 500.0, 2000.0, 1000.0}},
        {"twelfth-octave-4k", {T::TwelfthOctave, 800.0, 4000.0, 1000.0}},
    };
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: noise_band_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        std::ifstream input(argv[1]);
        if (!input) {
            throw std::runtime_error(std::string("could not open fixture: ") + argv[1]);
        }
        std::map<std::string, std::vector<std::array<double, 3>>> cases;
        std::string line;
        while (std::getline(input, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            if (line.empty() || line.rfind("case,", 0) == 0) {
                continue;
            }
            std::stringstream stream(line);
            std::string name;
            std::string cell;
            std::getline(stream, name, ',');
            std::getline(stream, cell, ','); // band index, implicit by order
            std::array<double, 3> row{};
            for (auto& value : row) {
                std::getline(stream, cell, ',');
                value = std::stod(cell);
            }
            cases[name].push_back(row);
        }

        const auto catalogue = case_catalogue();
        constexpr double tolerance = 1e-9;
        std::size_t checked_bands = 0;
        for (const auto& [name, expected] : cases) {
            const auto found = catalogue.find(name);
            if (found == catalogue.end()) {
                std::cerr << "Fixture case " << name << " has no catalogue entry\n";
                return 1;
            }
            const auto bands = pamguard::detectors::calculate_noise_bands(
                found->second.type, found->second.min_freq, found->second.max_freq, found->second.reference);
            if (bands.size() != expected.size()) {
                std::cerr << "Case " << name << " band count mismatch: fixture=" << expected.size()
                          << " ported=" << bands.size() << "\n";
                return 1;
            }
            for (std::size_t i = 0; i < bands.size(); ++i) {
                if (std::abs(bands[i].centre_hz - expected[i][0]) > tolerance ||
                    std::abs(bands[i].lo_edge_hz - expected[i][1]) > tolerance ||
                    std::abs(bands[i].hi_edge_hz - expected[i][2]) > tolerance) {
                    std::cerr << "Case " << name << " band " << i << " mismatch\n";
                    return 1;
                }
                ++checked_bands;
            }
        }

        // Runtime sanity over the parity-proven filters: a 1 kHz tone of
        // amplitude 0.5 lands in the 1 kHz third-octave band at RMS ~0.3536
        // and is strongly rejected two bands away; the decimator offset carry
        // survives odd-length chunks.
        NoiseBandConfig config;
        config.enabled = true;
        config.band_type = NoiseBandType::ThirdOctave;
        config.min_frequency_hz = 100.0;
        config.max_frequency_hz = 20000.0;
        config.output_interval_seconds = 1.0;
        NoiseBandMonitor monitor(48000.0, config);
        if (!monitor.valid()) {
            std::cerr << "Noise band monitor failed to build\n";
            return 1;
        }
        std::size_t tone_band = 0;
        for (std::size_t i = 0; i < monitor.bands().size(); ++i) {
            if (monitor.bands()[i].lo_edge_hz <= 1000.0 && 1000.0 < monitor.bands()[i].hi_edge_hz) {
                tone_band = i;
            }
        }
        std::optional<pamguard::detectors::NoiseBandLevels> levels;
        std::int64_t cursor = 0;
        // Deliberately odd chunk length: exercises the pick-every-Nth carry.
        const std::size_t chunk_length = 4801;
        while (!levels.has_value()) {
            std::vector<double> chunk(chunk_length);
            for (std::size_t i = 0; i < chunk.size(); ++i) {
                const double n = static_cast<double>(cursor + static_cast<std::int64_t>(i));
                chunk[i] = 0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * n / 48000.0);
            }
            levels = monitor.process(chunk, cursor, cursor / 48);
            cursor += static_cast<std::int64_t>(chunk.size());
        }
        const double tone_rms = levels->rms[tone_band];
        if (std::abs(tone_rms - 0.5 / std::sqrt(2.0)) > 0.02) {
            std::cerr << "1 kHz tone RMS in its own band was " << tone_rms << ", expected ~0.3536\n";
            return 1;
        }
        if (tone_band >= 2 && levels->rms[tone_band - 2] > tone_rms / 30.0) {
            std::cerr << "Adjacent-band rejection too weak: " << levels->rms[tone_band - 2] << "\n";
            return 1;
        }
        if (tone_band + 2 < levels->rms.size() && levels->rms[tone_band + 2] > tone_rms / 30.0) {
            std::cerr << "Upper-band rejection too weak: " << levels->rms[tone_band + 2] << "\n";
            return 1;
        }

        std::cout << "Noise band parity passed\n";
        std::cout << "cases=" << cases.size() << " bands=" << checked_bands
                  << " toneRms=" << tone_rms << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
