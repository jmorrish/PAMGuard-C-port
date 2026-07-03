#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace pamguard::dsp {

enum class WindowType {
    Rectangular = 0,
    Hamming = 1,
    Hann = 2,
    Bartlett = 3,
    Blackman = 4,
    BlackmanHarris = 5
};

std::string_view window_name(WindowType type);
std::vector<double> make_window(WindowType type, std::size_t length);
double window_gain(const std::vector<double>& window);
double hann_scaling(std::size_t length);

} // namespace pamguard::dsp

