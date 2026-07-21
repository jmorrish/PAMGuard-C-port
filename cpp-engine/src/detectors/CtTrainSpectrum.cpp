#include "pamguard/detectors/CtTrainSpectrum.h"

#include <algorithm>
#include <complex>

#include "pamguard/dsp/RealFft.h"

namespace pamguard::detectors {

namespace {

/** JTransforms-packed magnitude squared, as ComplexArray.magsq over rfft. */
std::vector<double> packed_magnitude_squared(const dsp::ComplexSpectrum& bins) {
    if (bins.size() < 2) {
        return {};
    }
    const auto fft_length = (bins.size() - 1) * 2;
    std::vector<double> magsq(fft_length / 2, 0.0);
    magsq[0] = bins[0].real() * bins[0].real() + bins[fft_length / 2].real() * bins[fft_length / 2].real();
    for (std::size_t i = 1; i < magsq.size(); ++i) {
        magsq[i] = std::norm(bins[i]);
    }
    return magsq;
}

} // namespace

std::vector<double> ct_train_average_spectrum(const std::vector<std::vector<double>>& click_waveforms,
                                              std::size_t fft_length) {
    if (click_waveforms.empty() || fft_length < 4 || (fft_length & (fft_length - 1)) != 0) {
        return {};
    }

    dsp::RealFft fft;
    std::vector<double> summed;
    for (const auto& waveform : click_waveforms) {
        if (waveform.empty()) {
            continue;
        }
        std::vector<double> padded(fft_length, 0.0);
        const auto copy_count = std::min(waveform.size(), fft_length);
        std::copy_n(waveform.begin(), copy_count, padded.begin());
        const auto magsq = packed_magnitude_squared(fft.forward(padded, fft_length));
        if (summed.empty()) {
            summed = magsq;
            continue;
        }
        for (std::size_t i = 0; i < summed.size() && i < magsq.size(); ++i) {
            summed[i] += magsq[i];
        }
    }
    return summed;
}

} // namespace pamguard::detectors
