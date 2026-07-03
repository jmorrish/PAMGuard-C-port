#include "pamguard/dsp/RealFft.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace pamguard::dsp {

namespace {

bool is_power_of_two(std::size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

void fft_in_place(ComplexSpectrum& data) {
    const std::size_t n = data.size();

    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
        const Complex w_len(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            Complex w(1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const Complex u = data[i + j];
                const Complex v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= w_len;
            }
        }
    }
}

} // namespace

ComplexSpectrum RealFft::forward(const std::vector<double>& input, std::size_t fft_length) const {
    if (!is_power_of_two(fft_length)) {
        throw std::invalid_argument("fft_length must be a power of two");
    }

    ComplexSpectrum data(fft_length, Complex(0.0, 0.0));
    const std::size_t n_copy = std::min(input.size(), fft_length);
    for (std::size_t i = 0; i < n_copy; ++i) {
        data[i] = Complex(input[i], 0.0);
    }

    fft_in_place(data);

    ComplexSpectrum half;
    half.reserve(fft_length / 2 + 1);
    for (std::size_t i = 0; i <= fft_length / 2; ++i) {
        half.push_back(data[i]);
    }
    return half;
}

} // namespace pamguard::dsp
