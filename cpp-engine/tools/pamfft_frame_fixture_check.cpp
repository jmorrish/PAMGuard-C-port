#include <algorithm>
#include <cmath>
#include <complex>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/core/AudioFrame.h"
#include "pamguard/dsp/SpectrogramEngine.h"
#include "pamguard/dsp/WindowFunction.h"

namespace {

struct FrameBinRow {
    std::size_t frame = 0;
    std::size_t channel = 0;
    std::size_t fft_slice = 0;
    std::int64_t start_sample = 0;
    std::int64_t time_ms = 0;
    std::size_t bin = 0;
    double real = 0.0;
    double imag = 0.0;
    double magsq = 0.0;
};

pamguard::dsp::WindowType parse_window_type(const std::string& raw) {
    const int type = std::stoi(raw);
    if (type < 0 || type > 5) {
        throw std::invalid_argument("window type must be 0..5");
    }
    return static_cast<pamguard::dsp::WindowType>(type);
}

double synthetic_sample(std::size_t index) {
    return std::sin(static_cast<double>(index) * 0.2) + 0.25 * std::cos(static_cast<double>(index) * 0.7);
}

std::vector<FrameBinRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<FrameBinRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.find("frame,channel,fftSlice,startSample,timeMs,bin,real,imag,magsq") != std::string::npos) {
            continue;
        }

        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 9) {
            continue;
        }

        FrameBinRow row;
        row.frame = static_cast<std::size_t>(std::stoull(cells[0]));
        row.channel = static_cast<std::size_t>(std::stoull(cells[1]));
        row.fft_slice = static_cast<std::size_t>(std::stoull(cells[2]));
        row.start_sample = std::stoll(cells[3]);
        row.time_ms = std::stoll(cells[4]);
        row.bin = static_cast<std::size_t>(std::stoull(cells[5]));
        row.real = std::stod(cells[6]);
        row.imag = std::stod(cells[7]);
        row.magsq = std::stod(cells[8]);
        rows.push_back(row);
    }
    return rows;
}

std::vector<FrameBinRow> flatten_frames(const std::vector<pamguard::dsp::SpectrogramFrame>& frames) {
    std::vector<FrameBinRow> rows;
    for (std::size_t frame_index = 0; frame_index < frames.size(); ++frame_index) {
        const auto& frame = frames[frame_index];
        if (frame.bins.size() < 2) {
            throw std::runtime_error("normal FFT bins must include DC and Nyquist");
        }
        const std::size_t fft_length = (frame.bins.size() - 1) * 2;
        const std::size_t packed_bin_count = fft_length / 2;
        for (std::size_t bin = 0; bin < packed_bin_count; ++bin) {
            FrameBinRow row;
            row.frame = frame_index;
            row.channel = frame.channel;
            row.fft_slice = frame.fft_slice;
            row.start_sample = frame.start_sample;
            row.time_ms = frame.time_unix_ms;
            row.bin = bin;
            if (bin == 0) {
                row.real = frame.bins[0].real();
                row.imag = frame.bins[fft_length / 2].real();
            }
            else {
                row.real = frame.bins[bin].real();
                row.imag = frame.bins[bin].imag();
            }
            row.magsq = row.real * row.real + row.imag * row.imag;
            rows.push_back(row);
        }
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "Usage: pamfft_frame_fixture_check <windowType> <fftLength> <fftHop> <chunkLength> <fixture.csv>\n";
        return 2;
    }

    try {
        pamguard::core::FftConfig config;
        config.window_type = parse_window_type(argv[1]);
        config.fft_length = static_cast<std::size_t>(std::stoull(argv[2]));
        config.fft_hop = static_cast<std::size_t>(std::stoull(argv[3]));
        const auto chunk_length = static_cast<std::size_t>(std::stoull(argv[4]));
        const auto fixture = read_fixture(argv[5]);

        pamguard::core::AudioChunk chunk;
        chunk.start_sample = 0;
        chunk.time_unix_ms = 1000;
        chunk.sample_rate_hz = 8000;
        chunk.channel_count = 1;
        chunk.interleaved_pcm.resize(chunk_length);
        for (std::size_t i = 0; i < chunk_length; ++i) {
            chunk.interleaved_pcm[i] = synthetic_sample(i);
        }

        pamguard::dsp::SpectrogramEngine engine(config);
        const auto actual = flatten_frames(engine.process(chunk));

        if (actual.size() != fixture.size()) {
            std::cerr << "Row count mismatch: fixture=" << fixture.size() << " actual=" << actual.size() << "\n";
            return 1;
        }

        double max_abs_error = 0.0;
        std::size_t max_index = 0;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const auto& got = actual[i];
            const auto& expected = fixture[i];

            if (got.frame != expected.frame || got.channel != expected.channel || got.fft_slice != expected.fft_slice ||
                got.start_sample != expected.start_sample || got.time_ms != expected.time_ms || got.bin != expected.bin) {
                std::cerr << "Metadata mismatch at row " << i << "\n";
                std::cerr << "expected frame/channel/slice/start/time/bin="
                          << expected.frame << "/" << expected.channel << "/" << expected.fft_slice << "/"
                          << expected.start_sample << "/" << expected.time_ms << "/" << expected.bin << "\n";
                std::cerr << "actual   frame/channel/slice/start/time/bin="
                          << got.frame << "/" << got.channel << "/" << got.fft_slice << "/"
                          << got.start_sample << "/" << got.time_ms << "/" << got.bin << "\n";
                return 1;
            }

            const double real_error = std::abs(got.real - expected.real);
            const double imag_error = std::abs(got.imag - expected.imag);
            const double magsq_error = std::abs(got.magsq - expected.magsq);
            const double error = std::max(real_error, std::max(imag_error, magsq_error));
            if (error > max_abs_error) {
                max_abs_error = error;
                max_index = i;
            }
        }

        constexpr double tolerance = 1e-10;
        if (max_abs_error > tolerance) {
            std::cerr << "PamFFT frame parity failed\n";
            std::cerr << "max_abs_error=" << max_abs_error << " at row " << max_index << "\n";
            return 1;
        }

        std::cout << "PamFFT frame parity passed\n";
        std::cout << "max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
