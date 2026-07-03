#include "pamguard/dsp/WindowFunction.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace pamguard::dsp {

std::string_view window_name(WindowType type) {
    switch (type) {
    case WindowType::Rectangular:
        return "Rectangular";
    case WindowType::Hamming:
        return "Hamming";
    case WindowType::Hann:
        return "Hann";
    case WindowType::Bartlett:
        return "Bartlett (Triangular)";
    case WindowType::Blackman:
        return "Blackman";
    case WindowType::BlackmanHarris:
        return "Blackman-Harris";
    }
    return "Rectangular";
}

std::vector<double> make_window(WindowType type, std::size_t length) {
    if (length == 0) {
        throw std::invalid_argument("window length must be non-zero");
    }

    std::vector<double> window(length, 1.0);
    if (type == WindowType::Rectangular) {
        return window;
    }

    if (length == 1) {
        window[0] = 1.0;
        return window;
    }

    switch (type) {
    case WindowType::Hamming:
        for (std::size_t i = 0; i < length; ++i) {
            window[i] = 0.54 - 0.46 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(length - 1));
        }
        break;
    case WindowType::Hann:
        for (std::size_t i = 0; i < length; ++i) {
            window[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(length));
        }
        break;
    case WindowType::Bartlett: {
        const double a = 2.0 / static_cast<double>(length - 1);
        std::size_t i = 0;
        for (; i < (length - 1) / 2; ++i) {
            window[i] = static_cast<double>(i) * a;
        }
        for (; i < length; ++i) {
            window[i] = 2.0 - a * static_cast<double>(i);
        }
        break;
    }
    case WindowType::Blackman: {
        const double arg = 2.0 * std::numbers::pi / static_cast<double>(length - 1);
        for (std::size_t i = 0; i < length; ++i) {
            window[i] = 0.42 - 0.5 * std::cos(arg * static_cast<double>(i)) + 0.08 * std::cos(2.0 * arg * static_cast<double>(i));
        }
        break;
    }
    case WindowType::BlackmanHarris: {
        const double arg = 2.0 * std::numbers::pi / static_cast<double>(length - 1);
        for (std::size_t i = 0; i < length; ++i) {
            const auto x = static_cast<double>(i);
            window[i] = 0.35875 - 0.48829 * std::cos(arg * x) + 0.14128 * std::cos(2.0 * arg * x) - 0.01168 * std::cos(3.0 * arg * x);
        }
        break;
    }
    case WindowType::Rectangular:
        break;
    }

    return window;
}

double window_gain(const std::vector<double>& window) {
    if (window.empty()) {
        throw std::invalid_argument("window must not be empty");
    }
    double total = 0.0;
    for (double value : window) {
        total += value * value;
    }
    return std::sqrt(total / static_cast<double>(window.size()));
}

double hann_scaling(std::size_t length) {
    const auto window = make_window(WindowType::Hann, length);
    double sum = 0.0;
    for (double value : window) {
        sum += value * value / (0.5 * 0.5);
    }
    return sum / static_cast<double>(length);
}

} // namespace pamguard::dsp

