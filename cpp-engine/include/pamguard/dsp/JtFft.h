#pragma once

#include <cstddef>
#include <vector>

namespace pamguard::dsp {

/**
 * The FFT conventions PAMGuard's FastFFT inherits from JTransforms'
 * DoubleFFT_1D, for ARBITRARY lengths (the matched-template classifier and
 * PamInterp.interpWaveform run FFTs whose lengths are set by waveform and
 * template sizes, not powers of two).
 *
 * real_forward reproduces DoubleFFT_1D.realForward's packed layout:
 *   n even: a[2k]=Re[k] (k<n/2), a[2k+1]=Im[k] (0<k<n/2), a[1]=Re[n/2]
 *   n odd:  a[2k]=Re[k] (k<=(n-1)/2), a[2k+1]=Im[k] (0<k<(n-1)/2),
 *           a[1]=Im[(n-1)/2]
 * PAMGuard wraps that packed array in a ComplexArray and happily multiplies
 * the packed bin 0 as if it were an ordinary complex number — consumers of
 * this class are expected to preserve that behaviour, not repair it.
 *
 * complex_inverse reproduces DoubleFFT_1D.complexInverse over n interleaved
 * complex pairs: x[j] = sum_k X[k] e^{+2*pi*i*j*k/n}, divided by n only
 * when scale is set.
 *
 * Implementation: iterative radix-2 for powers of two, direct DFT
 * otherwise. Values match JTransforms to floating-point accuracy (same
 * mathematics, different rounding), which the fixture tolerances cover.
 */
class JtFft {
public:
    /** DoubleFFT_1D.realForward on x zero-padded/truncated to n samples. */
    [[nodiscard]] static std::vector<double> real_forward(const std::vector<double>& x, std::size_t n);

    /**
     * DoubleFFT_1D.complexInverse over n complex pairs; data is resized to
     * 2n (zero padded) as FastFFT.ifft does.
     */
    static void complex_inverse(std::vector<double>& data, std::size_t n, bool scale);
};

} // namespace pamguard::dsp
