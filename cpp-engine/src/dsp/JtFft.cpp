#include "pamguard/dsp/JtFft.h"

#include <cmath>
#include <complex>
#include <numbers>

namespace pamguard::dsp {

namespace {

bool is_power_of_two(std::size_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}

// In-place iterative radix-2 complex transform. sign = -1 forward, +1
// inverse (no scaling).
void radix2(std::vector<std::complex<double>>& a, int sign) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = sign * 2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// Direct DFT for arbitrary length. sign = -1 forward, +1 inverse.
std::vector<std::complex<double>> direct_dft(const std::vector<std::complex<double>>& a, int sign) {
    const std::size_t n = a.size();
    std::vector<std::complex<double>> out(n);
    const double base = sign * 2.0 * std::numbers::pi / static_cast<double>(n);
    for (std::size_t k = 0; k < n; ++k) {
        std::complex<double> sum(0.0, 0.0);
        for (std::size_t j = 0; j < n; ++j) {
            const double angle = base * static_cast<double>((j * k) % n);
            sum += a[j] * std::complex<double>(std::cos(angle), std::sin(angle));
        }
        out[k] = sum;
    }
    return out;
}

std::vector<std::complex<double>> transform(std::vector<std::complex<double>> a, int sign) {
    if (is_power_of_two(a.size())) {
        radix2(a, sign);
        return a;
    }
    return direct_dft(a, sign);
}

} // namespace

std::vector<double> JtFft::real_forward(const std::vector<double>& x, std::size_t n) {
    std::vector<std::complex<double>> a(n);
    for (std::size_t i = 0; i < n && i < x.size(); ++i) {
        a[i] = {x[i], 0.0};
    }
    const auto spectrum = transform(std::move(a), -1);

    std::vector<double> packed(n, 0.0);
    if (n == 0) {
        return packed;
    }
    if (n % 2 == 0) {
        const std::size_t half = n / 2;
        for (std::size_t k = 0; k < half; ++k) {
            packed[2 * k] = spectrum[k].real();
            if (k > 0) {
                packed[2 * k + 1] = spectrum[k].imag();
            }
        }
        packed[1] = spectrum[half].real();
    }
    else {
        const std::size_t half = (n - 1) / 2;
        for (std::size_t k = 0; k <= half; ++k) {
            if (2 * k < n) {
                packed[2 * k] = spectrum[k].real();
            }
            if (k > 0 && 2 * k + 1 < n) {
                packed[2 * k + 1] = spectrum[k].imag();
            }
        }
        packed[1] = spectrum[half].imag();
    }
    return packed;
}

void JtFft::complex_inverse(std::vector<double>& data, std::size_t n, bool scale) {
    data.resize(2 * n, 0.0);
    std::vector<std::complex<double>> a(n);
    for (std::size_t k = 0; k < n; ++k) {
        a[k] = {data[2 * k], data[2 * k + 1]};
    }
    const auto out = transform(std::move(a), +1);
    const double factor = scale ? 1.0 / static_cast<double>(n) : 1.0;
    for (std::size_t k = 0; k < n; ++k) {
        data[2 * k] = out[k].real() * factor;
        data[2 * k + 1] = out[k].imag() * factor;
    }
}

} // namespace pamguard::dsp
