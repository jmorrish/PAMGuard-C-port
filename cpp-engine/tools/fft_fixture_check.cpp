#include <cmath>
#include <complex>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "pamguard/dsp/RealFft.h"
#include "pamguard/dsp/WindowFunction.h"

namespace {

struct FftRow {
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

std::vector<FftRow> read_fixture(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open fixture: " + path);
    }

    std::vector<FftRow> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.find("index,real,imag,magsq") != std::string::npos) {
            continue;
        }

        std::stringstream stream(line);
        std::string cell;
        std::vector<std::string> cells;
        while (std::getline(stream, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() != 4 || cells[1] == "real") {
            continue;
        }

        FftRow row;
        row.real = std::stod(cells[1]);
        row.imag = std::stod(cells[2]);
        row.magsq = std::stod(cells[3]);
        rows.push_back(row);
    }
    return rows;
}

std::vector<FftRow> pamguard_pack(const pamguard::dsp::ComplexSpectrum& normal_bins) {
    if (normal_bins.size() < 2) {
        throw std::runtime_error("normal FFT bins must include DC and Nyquist");
    }

    const std::size_t fft_length = (normal_bins.size() - 1) * 2;
    std::vector<FftRow> packed(fft_length / 2);
    packed[0].real = normal_bins[0].real();
    packed[0].imag = normal_bins[fft_length / 2].real();
    packed[0].magsq = packed[0].real * packed[0].real + packed[0].imag * packed[0].imag;

    for (std::size_t i = 1; i < packed.size(); ++i) {
        packed[i].real = normal_bins[i].real();
        packed[i].imag = normal_bins[i].imag();
        packed[i].magsq = packed[i].real * packed[i].real + packed[i].imag * packed[i].imag;
    }
    return packed;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: fft_fixture_check <windowType> <fftLength> <fixture.csv>\n";
        return 2;
    }

    try {
        const auto window_type = parse_window_type(argv[1]);
        const auto fft_length = static_cast<std::size_t>(std::stoull(argv[2]));
        const auto fixture = read_fixture(argv[3]);

        const auto window = pamguard::dsp::make_window(window_type, fft_length);
        std::vector<double> samples(fft_length);
        for (std::size_t i = 0; i < fft_length; ++i) {
            samples[i] = synthetic_sample(i) * window[i];
        }

        pamguard::dsp::RealFft fft;
        const auto packed = pamguard_pack(fft.forward(samples, fft_length));

        if (packed.size() != fixture.size()) {
            std::cerr << "Length mismatch: fixture=" << fixture.size() << " actual=" << packed.size() << "\n";
            return 1;
        }

        double max_abs_error = 0.0;
        std::size_t max_index = 0;
        for (std::size_t i = 0; i < packed.size(); ++i) {
            const double real_error = std::abs(packed[i].real - fixture[i].real);
            const double imag_error = std::abs(packed[i].imag - fixture[i].imag);
            const double magsq_error = std::abs(packed[i].magsq - fixture[i].magsq);
            const double error = std::max(real_error, std::max(imag_error, magsq_error));
            if (error > max_abs_error) {
                max_abs_error = error;
                max_index = i;
            }
        }

        constexpr double tolerance = 1e-10;
        if (max_abs_error > tolerance) {
            std::cerr << "FFT parity failed\n";
            std::cerr << "max_abs_error=" << max_abs_error << " at index " << max_index << "\n";
            return 1;
        }

        std::cout << "FFT parity passed\n";
        std::cout << "max_abs_error=" << max_abs_error << "\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}

