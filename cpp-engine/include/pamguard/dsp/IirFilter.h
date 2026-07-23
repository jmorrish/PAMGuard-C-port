#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace pamguard::dsp {

/**
 * Port of PAMGuard's Filters package: Butterworth and Chebyshev IIR design
 * (Lynn & Fuerst bilinear method, `ButterworthMethod`/`ChebyshevMethod` via
 * `IIRFilterMethod`) plus the runtime the click detector actually uses —
 * `FastIIRFilter`, whose coefficients come from `getFastFilterCoefficients`
 * over the **sorted** pole/zero pairs and whose per-sample recursion this
 * class reproduces exactly.
 *
 * PAMGuard field-name conventions are kept, including the confusing one: for
 * a BANDPASS/BANDSTOP, `high_pass_freq_hz` is the **lower** band edge and
 * `low_pass_freq_hz` the **upper** (each edge is named for the filter action
 * it performs). Frequencies are floats because `FilterParams` stores floats,
 * and the float→double promotion is part of the reference's arithmetic.
 */
enum class IirFilterType { None, Butterworth, Chebyshev };
enum class IirFilterBand { LowPass, HighPass, BandPass, BandStop };

struct IirFilterParams {
    IirFilterType type = IirFilterType::None;
    IirFilterBand band = IirFilterBand::HighPass;
    int order = 4;
    float low_pass_freq_hz = 0.0F;
    float high_pass_freq_hz = 0.0F;
    /** Chebyshev only. */
    double pass_band_ripple_db = 2.0;
};

class FastIirFilter {
public:
    FastIirFilter(double sample_rate_hz, const IirFilterParams& params);

    /** False when type is None or the design produced no usable stages. */
    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] double run_sample(double sample);
    void run(const std::vector<double>& input, std::vector<double>& output);
    void reset();

    /** Exposed for the parity check: the packed a1,a2,b1,b2 stage table. */
    [[nodiscard]] const std::vector<double>& coefficients() const noexcept { return coefficients_; }
    [[nodiscard]] double gain_constant() const noexcept { return gain_; }

private:
    std::vector<double> coefficients_;
    std::vector<double> state_;
    double gain_ = 1.0;
    bool active_ = false;
};

} // namespace pamguard::dsp
