#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/detectors/WhistlePeakDetector.h"

namespace {

struct PeakRow {
    std::size_t index = 0;
    std::size_t min_freq = 0;
    std::size_t peak_freq = 0;
    std::size_t max_freq = 0;
    double max_amp = 0.0;
    double signal = 0.0;
    double noise = 0.0;
    bool ok = false;
};

std::vector<double> synthetic_spectrum(std::size_t half, bool with_peak) {
    std::vector<double> spectrum(half);
    for (std::size_t i = 0; i < half; ++i) {
        spectrum[i] = 1.0 + 0.01 * std::sin(static_cast<double>(i) * 0.33);
    }
    if (with_peak) {
        spectrum[20] = 22.0;
        spectrum[21] = 26.0;
        spectrum[22] = 31.0;
        spectrum[23] = 24.0;
        spectrum[24] = 18.0;
    }
    return spectrum;
}

std::vector<PeakRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<PeakRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.find("index,minFreq,peakFreq,maxFreq,maxAmp,signal,noise,ok") != std::string::npos) {
            continue;
        }

        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 8) {
            continue;
        }

        PeakRow row;
        row.index = static_cast<std::size_t>(std::stoull(cells[0]));
        row.min_freq = static_cast<std::size_t>(std::stoull(cells[1]));
        row.peak_freq = static_cast<std::size_t>(std::stoull(cells[2]));
        row.max_freq = static_cast<std::size_t>(std::stoull(cells[3]));
        row.max_amp = std::stod(cells[4]);
        row.signal = std::stod(cells[5]);
        row.noise = std::stod(cells[6]);
        row.ok = cells[7] == "true";
        rows.push_back(row);
    }
    return rows;
}

std::vector<PeakRow> to_rows(const std::vector<pamguard::detectors::WhistlePeak>& peaks) {
    std::vector<PeakRow> rows;
    for (std::size_t i = 0; i < peaks.size(); ++i) {
        const auto& peak = peaks[i];
        rows.push_back(PeakRow{
            i,
            peak.min_freq,
            peak.peak_freq,
            peak.max_freq,
            peak.max_amp,
            peak.signal,
            peak.noise,
            peak.ok,
        });
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: whistle_peak_fixture_check <fixture.csv>\n";
        return 2;
    }

    try {
        const auto fixture = read_fixture(argv[1]);

        pamguard::detectors::WhistlePeakConfig config;
        config.fft_length = 128;
        config.fft_hop = 64;
        config.sample_rate_hz = 48000;
        config.search_bin0 = 1;
        config.search_bin1 = 62;

        bool rejected_bad_fft = false;
        try {
            auto bad_config = config;
            bad_config.fft_length = 7;
            pamguard::detectors::WhistlePeakDetector bad_detector(bad_config);
            (void)bad_detector;
        }
        catch (const std::invalid_argument&) {
            rejected_bad_fft = true;
        }
        if (!rejected_bad_fft) {
            std::cerr << "Whistle peak detector should reject invalid FFT length\n";
            return 1;
        }

        bool rejected_bad_rate = false;
        try {
            auto bad_config = config;
            bad_config.sample_rate_hz = 0.0;
            pamguard::detectors::WhistlePeakDetector bad_detector(bad_config);
            (void)bad_detector;
        }
        catch (const std::invalid_argument&) {
            rejected_bad_rate = true;
        }
        if (!rejected_bad_rate) {
            std::cerr << "Whistle peak detector should reject invalid sample rate\n";
            return 1;
        }

        auto default_search_config = config;
        default_search_config.search_bin1 = 0;
        pamguard::detectors::WhistlePeakDetector default_search_detector(default_search_config);
        if (default_search_detector.config().search_bin1 != (default_search_config.fft_length / 2) - 2) {
            std::cerr << "Whistle peak detector default search_bin1 mismatch\n";
            return 1;
        }

        pamguard::detectors::WhistlePeakDetector detector(config);
        bool rejected_bad_slice = false;
        try {
            (void)detector.process_magnitude_slice({1.0, 2.0}, 0, 0, 0);
        }
        catch (const std::invalid_argument&) {
            rejected_bad_slice = true;
        }
        if (!rejected_bad_slice) {
            std::cerr << "Whistle peak detector should reject wrong magnitude slice size\n";
            return 1;
        }

        std::vector<pamguard::detectors::WhistlePeak> peaks;
        for (std::size_t slice = 0; slice <= config.warmup_slices; ++slice) {
            peaks = detector.process_magnitude_slice(
                synthetic_spectrum(config.fft_length / 2, slice == config.warmup_slices),
                static_cast<std::int64_t>(slice * config.fft_hop),
                1000 + static_cast<std::int64_t>(slice),
                slice);
        }
        detector.reset();
        std::vector<pamguard::detectors::WhistlePeak> reset_peaks;
        for (std::size_t slice = 0; slice <= config.warmup_slices; ++slice) {
            reset_peaks = detector.process_magnitude_slice(
                synthetic_spectrum(config.fft_length / 2, slice == config.warmup_slices),
                static_cast<std::int64_t>(slice * config.fft_hop),
                1000 + static_cast<std::int64_t>(slice),
                slice);
        }
        if (reset_peaks.size() != peaks.size()) {
            std::cerr << "Whistle peak detector reset reproducibility mismatch\n";
            return 1;
        }

        auto suppress_config = config;
        suppress_config.max_percent_over_threshold = 1.0;
        pamguard::detectors::WhistlePeakDetector suppress_detector(suppress_config);
        std::vector<pamguard::detectors::WhistlePeak> suppressed;
        for (std::size_t slice = 0; slice <= suppress_config.warmup_slices; ++slice) {
            suppressed = suppress_detector.process_magnitude_slice(
                synthetic_spectrum(suppress_config.fft_length / 2, slice == suppress_config.warmup_slices),
                static_cast<std::int64_t>(slice * suppress_config.fft_hop),
                2000 + static_cast<std::int64_t>(slice),
                slice);
        }
        if (!suppressed.empty()) {
            std::cerr << "Whistle peak detector should suppress overly broad over-threshold slices\n";
            return 1;
        }

        auto width_config = config;
        width_config.min_peak_width = 10;
        pamguard::detectors::WhistlePeakDetector width_detector(width_config);
        std::vector<pamguard::detectors::WhistlePeak> width_rejected;
        for (std::size_t slice = 0; slice <= width_config.warmup_slices; ++slice) {
            width_rejected = width_detector.process_magnitude_slice(
                synthetic_spectrum(width_config.fft_length / 2, slice == width_config.warmup_slices),
                static_cast<std::int64_t>(slice * width_config.fft_hop),
                3000 + static_cast<std::int64_t>(slice),
                slice);
        }
        if (!width_rejected.empty()) {
            std::cerr << "Whistle peak detector should reject peaks narrower than min_peak_width\n";
            return 1;
        }

        const auto actual = to_rows(peaks);
        if (actual.size() != fixture.size()) {
            std::cerr << "Peak count mismatch: fixture=" << fixture.size() << " actual=" << actual.size() << "\n";
            return 1;
        }

        constexpr double tolerance = 1e-10;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const auto& got = actual[i];
            const auto& expected = fixture[i];
            if (got.index != expected.index || got.min_freq != expected.min_freq || got.peak_freq != expected.peak_freq ||
                got.max_freq != expected.max_freq || got.ok != expected.ok) {
                std::cerr << "Whistle peak metadata mismatch at row " << i << "\n";
                return 1;
            }
            const double error = std::max(
                std::abs(got.max_amp - expected.max_amp),
                std::max(std::abs(got.signal - expected.signal), std::abs(got.noise - expected.noise)));
            if (error > tolerance) {
                std::cerr << "Whistle peak parity failed at row " << i << " error=" << error << "\n";
                return 1;
            }
        }

        std::cout << "Whistle peak parity passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
