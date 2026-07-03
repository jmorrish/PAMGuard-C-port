#pragma once

#include <cstddef>
#include <vector>

#include "pamguard/dsp/ComplexSpectrum.h"

namespace pamguard::dsp {

class RealFft {
public:
    ComplexSpectrum forward(const std::vector<double>& input, std::size_t fft_length) const;
};

} // namespace pamguard::dsp

