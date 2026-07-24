#include "pamguard/dsp/IirFilter.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "pamguard/dsp/JtFft.h"

namespace pamguard::dsp {

namespace {

constexpr double kPi = std::numbers::pi;

using Complex = std::complex<double>;

double magsq(const Complex& value) {
    return value.real() * value.real() + value.imag() * value.imag();
}

struct PoleZeroPair {
    Complex pole;
    Complex zero;
    bool odd_one = false;
};

/**
 * PoleZeroPair.compareTo: the odd (self-conjugate) pair sorts last; otherwise
 * descending pole imaginary part, then Complex.compareTo (magnitude squared)
 * on pole then zero.
 */
bool pair_less(const PoleZeroPair& a, const PoleZeroPair& b) {
    if (b.odd_one && !a.odd_one) {
        return true;
    }
    if (a.odd_one) {
        return false;
    }
    const double d = a.pole.imag() - b.pole.imag();
    if (d != 0.0) {
        return d > 0.0; // signum(-d) < 0 means a before b when a.imag larger
    }
    const double pole_compare = magsq(a.pole) - magsq(b.pole);
    if (pole_compare != 0.0) {
        return pole_compare < 0.0;
    }
    return magsq(a.zero) < magsq(b.zero);
}

struct Design {
    std::vector<Complex> poles;
    std::vector<Complex> zeros;
    int good_poles = 0;
    double omega1 = 0.0;
    double omega2 = 0.0;
    double omega3 = 0.0;
    double zero_value = 0.0;
    int pole_zero_count = 0;
};

/** IIRFilterMethod.calculateOmegaValues, floats promoting exactly as Java's. */
void calculate_omegas(Design& design, const IirFilterParams& params, double sample_rate_hz) {
    switch (params.band) {
    case IirFilterBand::LowPass:
        design.omega1 = static_cast<double>(params.low_pass_freq_hz) / sample_rate_hz * 2.0 * kPi;
        design.zero_value = -1.0;
        break;
    case IirFilterBand::HighPass:
        design.omega1 = static_cast<double>(params.high_pass_freq_hz) / sample_rate_hz * 2.0 * kPi;
        design.omega1 = kPi - design.omega1;
        design.zero_value = 1.0;
        break;
    case IirFilterBand::BandPass:
        design.omega2 = static_cast<double>(params.high_pass_freq_hz) / sample_rate_hz * 2.0 * kPi;
        design.omega3 = static_cast<double>(params.low_pass_freq_hz) / sample_rate_hz * 2.0 * kPi;
        design.omega1 = design.omega3 - design.omega2;
        design.zero_value = 0.0;
        break;
    case IirFilterBand::BandStop:
        design.omega2 = static_cast<double>(params.high_pass_freq_hz) / sample_rate_hz * 2.0 * kPi;
        design.omega3 = static_cast<double>(params.low_pass_freq_hz) / sample_rate_hz * 2.0 * kPi;
        design.omega1 = design.omega3 - design.omega2;
        design.zero_value = 1.0;
        break;
    }
}

double m_pi_term(int order, int m) {
    const double dn = order;
    const double dm = m;
    if (order % 2 == 0) {
        return (2.0 * dm + 1.0) * kPi / (2.0 * dn);
    }
    return dm * kPi / dn;
}

Complex first_bit(const Complex& value, double A) {
    return Complex(0.5 * A, 0.0) * (value + 1.0);
}

Complex second_bit(const Complex& value, double A) {
    // Java: (z+1)^2 * 0.25A^2 - z, then pow(0.5) — the principal square root.
    return std::pow(std::pow(value + 1.0, 2.0) * (0.25 * A * A) - value, 0.5);
}

/** IIRFilterMethod.doBandpassTransformation. */
int bandpass_transform(Design& design, int n_points) {
    std::vector<Complex> old_poles(design.poles.begin(), design.poles.begin() + n_points);
    std::vector<Complex> old_zeros(design.zeros.begin(), design.zeros.begin() + n_points);
    design.poles.assign(static_cast<std::size_t>(n_points) * 2, Complex());
    design.zeros.assign(static_cast<std::size_t>(n_points) * 2, Complex());
    const double A = std::cos((design.omega3 + design.omega2) / 2.0) /
                     std::cos((design.omega3 - design.omega2) / 2.0);
    for (int m = 0; m < n_points; ++m) {
        const int m1 = 2 * m;
        const int m2 = m1 + 1;
        const Complex first = first_bit(old_poles[static_cast<std::size_t>(m)], A);
        const Complex second = second_bit(old_poles[static_cast<std::size_t>(m)], A);
        design.poles[static_cast<std::size_t>(m1)] = first + second;
        design.zeros[static_cast<std::size_t>(m1)] = Complex(1.0, 0.0);
        design.poles[static_cast<std::size_t>(m2)] = first - second;
        design.zeros[static_cast<std::size_t>(m2)] = Complex(-1.0, 0.0);
    }
    return n_points * 2;
}

/** IIRFilterMethod.doBandStopTransformation. */
int bandstop_transform(Design& design, int n_points) {
    std::vector<Complex> old_poles(design.poles.begin(), design.poles.begin() + n_points);
    std::vector<Complex> old_zeros(design.zeros.begin(), design.zeros.begin() + n_points);
    design.poles.assign(static_cast<std::size_t>(n_points) * 2, Complex());
    design.zeros.assign(static_cast<std::size_t>(n_points) * 2, Complex());
    const double A = std::cos((design.omega3 + design.omega2) / 2.0) /
                     std::cos((design.omega3 - design.omega2) / 2.0);
    for (int m = 0; m < n_points; ++m) {
        const int m1 = 2 * m;
        const int m2 = m1 + 1;
        const Complex first = first_bit(old_poles[static_cast<std::size_t>(m)], A);
        const Complex second = second_bit(old_poles[static_cast<std::size_t>(m)], A);
        const Complex first_z = first_bit(old_zeros[static_cast<std::size_t>(m)], A);
        const Complex second_z = second_bit(old_zeros[static_cast<std::size_t>(m)], A);
        design.poles[static_cast<std::size_t>(m1)] = first + second;
        design.zeros[static_cast<std::size_t>(m1)] = first_z + second_z;
        design.poles[static_cast<std::size_t>(m2)] = first - second;
        design.zeros[static_cast<std::size_t>(m2)] = first_z - second_z;
    }
    return n_points * 2;
}

/** ButterworthMethod.calculateFilter (Lynn & Fuerst p179). */
int butterworth_design(Design& design, const IirFilterParams& params) {
    const int n_pole_zeros = params.order * 2;
    design.poles.assign(static_cast<std::size_t>(n_pole_zeros), Complex());
    design.zeros.assign(static_cast<std::size_t>(n_pole_zeros), Complex());
    int good_poles = 0;
    for (int m = 0; m <= params.order * 2 - 1; ++m) {
        const double mpiterm = m_pi_term(params.order, m);
        const double tan_half = std::tan(design.omega1 / 2.0);
        const double d = 1.0 - 2.0 * tan_half * std::cos(mpiterm) + std::pow(tan_half, 2.0);
        if (d == 0.0) {
            continue;
        }
        const double p_real = (1.0 - std::pow(tan_half, 2.0)) / d;
        const double p_imag = 2.0 * tan_half * std::sin(mpiterm) / d;
        // ButterworthMethod keeps poles with norm() (magnitude squared) inside
        // a slightly padded unit circle.
        if (p_real * p_real + p_imag * p_imag <= 1.0000000001) {
            design.poles[static_cast<std::size_t>(good_poles)] = Complex(p_real, p_imag);
            design.zeros[static_cast<std::size_t>(good_poles)] = Complex(design.zero_value, 0.0);
            ++good_poles;
        }
    }
    if (params.band == IirFilterBand::HighPass) {
        for (int i = 0; i < good_poles; ++i) {
            design.poles[static_cast<std::size_t>(i)] =
                Complex(-design.poles[static_cast<std::size_t>(i)].real(),
                        design.poles[static_cast<std::size_t>(i)].imag());
        }
    }
    else if (params.band == IirFilterBand::BandPass) {
        good_poles = bandpass_transform(design, good_poles);
    }
    else if (params.band == IirFilterBand::BandStop) {
        good_poles = bandstop_transform(design, good_poles);
    }
    return good_poles;
}

/** ChebyshevMethod.calculateFilter, including its historical `c` formula. */
int chebyshev_design(Design& design, const IirFilterParams& params) {
    const int n_pole_zeros = params.order * 2;
    design.poles.assign(static_cast<std::size_t>(n_pole_zeros), Complex());
    design.zeros.assign(static_cast<std::size_t>(n_pole_zeros), Complex());
    int good_poles = 0;
    const double dn = params.order;
    const double delta = 1.0 - std::pow(10.0, -params.pass_band_ripple_db / 20.0);
    const double epsilon = std::sqrt(1.0 / std::pow(1.0 - delta, 2.0) - 1.0);
    const double c = std::sqrt(1.0 + 1.0 / std::pow(epsilon, 2.0)) + 1.0 / epsilon;
    const double a = 0.5 * (std::pow(c, 1.0 / dn) - std::pow(c, -1.0 / dn));
    const double b = 0.5 * (std::pow(c, 1.0 / dn) + std::pow(c, -1.0 / dn));
    for (int m = 0; m <= params.order * 2 - 1; ++m) {
        const double mpiterm = m_pi_term(params.order, m);
        const double tan_half = std::tan(design.omega1 / 2.0);
        const double d = std::pow(1.0 - a * tan_half * std::cos(mpiterm), 2.0) +
                         std::pow(b * tan_half * std::sin(mpiterm), 2.0);
        if (d == 0.0) {
            continue;
        }
        const double p_real = 2.0 * (1.0 - a * tan_half * std::cos(mpiterm)) / d - 1.0;
        const double p_imag = 2.0 * b * tan_half * std::sin(mpiterm) / d;
        if (p_real * p_real + p_imag * p_imag <= 1.0) {
            design.poles[static_cast<std::size_t>(good_poles)] = Complex(p_real, p_imag);
            design.zeros[static_cast<std::size_t>(good_poles)] = Complex(design.zero_value, 0.0);
            ++good_poles;
        }
        if (good_poles >= params.order) {
            break;
        }
    }
    if (params.band == IirFilterBand::HighPass) {
        for (int i = 0; i < good_poles; ++i) {
            design.poles[static_cast<std::size_t>(i)] =
                Complex(-design.poles[static_cast<std::size_t>(i)].real(),
                        design.poles[static_cast<std::size_t>(i)].imag());
        }
    }
    else if (params.band == IirFilterBand::BandPass) {
        good_poles = bandpass_transform(design, good_poles);
    }
    else if (params.band == IirFilterBand::BandStop) {
        good_poles = bandstop_transform(design, good_poles);
    }
    return good_poles;
}

int pole_zero_count(const IirFilterParams& params) {
    if (params.band == IirFilterBand::BandPass || params.band == IirFilterBand::BandStop) {
        return params.order * 2;
    }
    return params.order;
}

int odd_pz_index(const IirFilterParams& params) {
    if (params.order % 2 == 0 || params.band == IirFilterBand::BandPass ||
        params.band == IirFilterBand::BandStop) {
        return -1;
    }
    return params.order / 2;
}

/** IIRFilterMethod.getFilterGain: |z - e^jw| over |p - e^jw| products. */
double filter_gain_at(const Design& design, const IirFilterParams& params, double omega) {
    const Complex frequency(std::cos(omega), std::sin(omega));
    double gain = 1.0;
    const int count = pole_zero_count(params);
    if (static_cast<int>(design.poles.size()) < count) {
        return 1.0;
    }
    for (int j = 0; j < count; ++j) {
        if (j >= design.good_poles) {
            return 1.0; // Java hits a null pole and returns 1.
        }
        gain /= std::sqrt(magsq(design.poles[static_cast<std::size_t>(j)] - frequency));
        gain *= std::sqrt(magsq(design.zeros[static_cast<std::size_t>(j)] - frequency));
    }
    return gain;
}

/** IIRFilterMethod.getFilterGainConstant, with the Chebyshev ripple bumps. */
double compute_gain_constant(const Design& design, const IirFilterParams& params, double sample_rate_hz) {
    double gain = 0.0;
    switch (params.band) {
    case IirFilterBand::LowPass:
        gain = filter_gain_at(design, params, 0.0);
        break;
    case IirFilterBand::HighPass:
        gain = filter_gain_at(design, params, kPi);
        break;
    case IirFilterBand::BandPass: {
        const double omega2 = static_cast<double>(params.high_pass_freq_hz) / sample_rate_hz * 2.0 * kPi;
        const double omega3 = static_cast<double>(params.low_pass_freq_hz) / sample_rate_hz * 2.0 * kPi;
        gain = filter_gain_at(design, params, std::sqrt(omega2 * omega3));
        break;
    }
    case IirFilterBand::BandStop:
        gain = filter_gain_at(design, params, 0.0);
        break;
    }
    if (params.type == IirFilterType::Chebyshev) {
        if (params.band == IirFilterBand::BandStop) {
            gain *= std::pow(10.0, params.pass_band_ripple_db / 20.0);
        }
        if (params.order % 2 == 0) {
            gain *= std::pow(10.0, params.pass_band_ripple_db / 20.0);
        }
    }
    return gain;
}

std::vector<double> cheby_window(std::size_t points, double gamma) {
    const double alpha =
        std::cosh(std::acosh(std::pow(10.0, gamma)) / static_cast<double>(points));
    std::vector<double> complex(points * 2, 0.0);
    for (std::size_t m = 0; m < points; ++m) {
        const double a = std::abs(
            alpha * std::cos(kPi * static_cast<double>(m) /
                             static_cast<double>(points)));
        const double sign = (m % 2 == 0) ? 1.0 : -1.0;
        complex[2 * m] =
            sign * (a > 1.0
                        ? std::cosh(static_cast<double>(points) * std::acosh(a))
                        : std::cos(static_cast<double>(points) * std::acos(a)));
    }
    JtFft::complex_inverse(complex, points, true);
    std::vector<double> window(points, 0.0);
    const double first_half = complex[0] / 2.0;
    for (std::size_t i = 0; i + 1 < points; ++i) {
        window[i] = complex[2 * (i + 1)];
    }
    window.back() = first_half;
    double maximum = 0.0;
    for (std::size_t i = 0; i + 1 < points; ++i) {
        maximum = std::max(maximum, window[i]);
    }
    for (std::size_t i = 0; i + 1 < points; ++i) {
        window[i] /= maximum;
    }
    return window;
}

void invert_fir_band(std::vector<double>& taps) {
    const auto middle = (taps.size() - 1) / 2;
    for (std::size_t i = 0; i < taps.size(); ++i) {
        taps[i] = i == middle ? 1.0 - taps[i] : -taps[i];
    }
}

std::vector<double> window_fir_taps(std::size_t taps_count, double frequency,
                                    double sample_rate_hz,
                                    const std::vector<double>& window,
                                    bool high_pass) {
    std::vector<double> taps(taps_count, 0.0);
    const double k = frequency / (sample_rate_hz / 2.0) * taps_count;
    for (std::size_t i = 0; i < taps_count; ++i) {
        const auto small_k =
            static_cast<long long>(i) - static_cast<long long>(taps_count / 2);
        if (small_k == 0) {
            taps[i] = k / taps_count * window[i];
        }
        else {
            taps[i] =
                std::sin(kPi * small_k * k / taps_count) /
                (taps_count * std::sin(kPi * small_k / taps_count)) *
                window[i];
        }
    }
    if (high_pass) {
        invert_fir_band(taps);
    }
    return taps;
}

std::vector<double> design_window_fir(double sample_rate_hz,
                                      const IirFilterParams& params) {
    const std::size_t points = std::size_t{1} << params.order;
    const std::size_t taps_count = points - 1;
    const auto window = cheby_window(points, params.cheby_gamma);
    const double lower =
        std::min<double>(params.low_pass_freq_hz, params.high_pass_freq_hz);
    const double upper =
        std::max<double>(params.low_pass_freq_hz, params.high_pass_freq_hz);
    switch (params.band) {
    case IirFilterBand::LowPass:
        return window_fir_taps(taps_count, params.low_pass_freq_hz,
                               sample_rate_hz, window, false);
    case IirFilterBand::HighPass:
        return window_fir_taps(taps_count, params.high_pass_freq_hz,
                               sample_rate_hz, window, true);
    case IirFilterBand::BandPass:
    case IirFilterBand::BandStop: {
        auto taps =
            window_fir_taps(taps_count, lower, sample_rate_hz, window, false);
        const auto high =
            window_fir_taps(taps_count, upper, sample_rate_hz, window, true);
        for (std::size_t i = 0; i < taps.size(); ++i) {
            taps[i] += high[i];
        }
        if (params.band == IirFilterBand::BandPass) {
            invert_fir_band(taps);
        }
        return taps;
    }
    }
    return {};
}

std::vector<double> design_arbitrary_fir(double sample_rate_hz,
                                         const IirFilterParams& params) {
    if (params.arbitrary_frequencies_hz.size() < 2 ||
        params.arbitrary_frequencies_hz.size() !=
            params.arbitrary_gains_db.size()) {
        return {};
    }
    const int filter_order = 1 << params.order;
    const int filter_length = filter_order - 1;
    const auto window =
        cheby_window(static_cast<std::size_t>(filter_order), params.cheby_gamma);
    const int lap = static_cast<int>(std::lround(filter_order / 25.0));
    std::vector<double> frequency(params.arbitrary_frequencies_hz.size());
    std::vector<double> gain(params.arbitrary_gains_db.size());
    for (std::size_t i = 0; i < frequency.size(); ++i) {
        frequency[i] =
            params.arbitrary_frequencies_hz[i] / (sample_rate_hz / 2.0);
        gain[i] = std::pow(10.0, params.arbitrary_gains_db[i] / 20.0);
    }
    std::vector<double> response(static_cast<std::size_t>(filter_order) + 1, 0.0);
    int begin = 0;
    int end = 0;
    for (std::size_t interval = 0; interval + 1 < frequency.size(); ++interval) {
        const double df = frequency[interval + 1] - frequency[interval];
        if (df == 0.0) {
            begin -= lap / 2;
            end = begin + lap;
        }
        else {
            end = static_cast<int>(
                      std::lround(frequency[interval + 1] * filter_length)) -
                  1;
        }
        if (begin < 0 || end > filter_order) {
            return {};
        }
        if (begin == end) {
            response[static_cast<std::size_t>(end)] = gain[interval];
        }
        else {
            for (int j = begin; j <= end; ++j) {
                const double inc =
                    static_cast<double>(j - begin) / (end - begin);
                response[static_cast<std::size_t>(j)] =
                    inc * gain[interval + 1] + (1.0 - inc) * gain[interval];
            }
        }
        begin = end + 1;
    }
    const double dt = 0.5 * (filter_length - 1);
    const std::size_t inverse_length =
        static_cast<std::size_t>(filter_order) * 2;
    std::vector<double> complex_response(inverse_length * 2, 0.0);
    for (int i = 0; i <= filter_order; ++i) {
        const double angle = -dt * kPi * i / filter_order;
        complex_response[2 * static_cast<std::size_t>(i)] =
            std::cos(angle) * response[static_cast<std::size_t>(i)];
        complex_response[2 * static_cast<std::size_t>(i) + 1] =
            std::sin(angle) * response[static_cast<std::size_t>(i)];
    }
    for (int i = 0, j = filter_order * 2 - 1; i < filter_order - 1;
         ++i, --j) {
        complex_response[2 * static_cast<std::size_t>(j)] =
            complex_response[2 * static_cast<std::size_t>(i + 1)];
        complex_response[2 * static_cast<std::size_t>(j) + 1] =
            -complex_response[2 * static_cast<std::size_t>(i + 1) + 1];
    }
    JtFft::complex_inverse(complex_response, inverse_length, false);
    std::vector<double> taps(static_cast<std::size_t>(filter_length), 0.0);
    for (int i = 0; i < filter_length; ++i) {
        taps[static_cast<std::size_t>(i)] =
            complex_response[2 * static_cast<std::size_t>(i)] *
            window[static_cast<std::size_t>(i)] / inverse_length;
    }
    return taps;
}

} // namespace

FastIirFilter::FastIirFilter(double sample_rate_hz, const IirFilterParams& params) {
    if (params.type == IirFilterType::None || sample_rate_hz <= 0.0 || params.order <= 0) {
        return;
    }
    params_ = params;
    sample_rate_hz_ = sample_rate_hz;
    if (params.type == IirFilterType::FirWindow ||
        params.type == IirFilterType::FirArbitrary) {
        if (params.order >= 31) {
            return;
        }
        fir_taps_ = params.type == IirFilterType::FirWindow
                        ? design_window_fir(sample_rate_hz, params)
                        : design_arbitrary_fir(sample_rate_hz, params);
        fir_history_.assign(fir_taps_.size() + 1, 0.0);
        active_ = !fir_taps_.empty();
        runtime_type_ = active_ ? RuntimeType::Fir : RuntimeType::None;
        return;
    }
    if (params.type == IirFilterType::Fft) {
        active_ = true;
        runtime_type_ = RuntimeType::Fft;
        return;
    }
    Design design;
    calculate_omegas(design, params, sample_rate_hz);
    design.good_poles = params.type == IirFilterType::Butterworth ? butterworth_design(design, params)
                                                                  : chebyshev_design(design, params);
    if (design.good_poles <= 0) {
        return;
    }

    // getPoleZeros: pairs over the contiguous non-null prefix, odd fudge,
    // then the stable sort whose order the coefficient loop depends on.
    const int odd_index = odd_pz_index(params);
    int max_index = 0;
    for (int i = 0; i < static_cast<int>(design.poles.size()); ++i) {
        if (i >= design.good_poles) {
            break;
        }
        max_index = i;
    }
    std::vector<PoleZeroPair> pairs;
    pairs.reserve(static_cast<std::size_t>(max_index) + 1);
    for (int i = 0; i <= max_index; ++i) {
        pairs.push_back({design.poles[static_cast<std::size_t>(i)],
                         design.zeros[static_cast<std::size_t>(i)], i == odd_index});
    }
    std::stable_sort(pairs.begin(), pairs.end(), pair_less);

    // getFastFilterCoefficients over the sorted pairs.
    const int n_pole_pairs = static_cast<int>(pairs.size()) / 2;
    const int n_odd_ones = static_cast<int>(pairs.size()) % 2;
    coefficients_.assign(static_cast<std::size_t>(n_pole_pairs + n_odd_ones) * 4, 0.0);
    int j = 0;
    int i = 0;
    for (i = 0; i < n_pole_pairs; ++i) {
        const Complex pole1 = pairs[static_cast<std::size_t>(i)].pole;
        const Complex zero1 = pairs[static_cast<std::size_t>(i)].zero;
        const Complex pole2 = std::conj(pole1);
        const Complex zero2 = zero1;
        double a1 = -(zero1.real() + zero2.real());
        double a2 = std::sqrt(magsq(zero1 * zero2)); // Complex.mag()
        if (params.band == IirFilterBand::BandPass) {
            a2 *= zero1.real() * zero2.real();
        }
        const double b1 = pole1.real() + pole2.real();
        const double b2 = -std::sqrt(magsq(pole1 * pole2));
        coefficients_[static_cast<std::size_t>(j) + 0] = a1;
        coefficients_[static_cast<std::size_t>(j) + 1] = a2;
        coefficients_[static_cast<std::size_t>(j) + 2] = b1;
        coefficients_[static_cast<std::size_t>(j) + 3] = b2;
        j += 4;
    }
    for (int odd = 0; odd < n_odd_ones; ++odd, ++i) {
        const Complex pole1 = pairs[static_cast<std::size_t>(i)].pole;
        const Complex zero1 = pairs[static_cast<std::size_t>(i)].zero;
        coefficients_[static_cast<std::size_t>(j) + 0] = -zero1.real();
        coefficients_[static_cast<std::size_t>(j) + 2] = pole1.real();
        j += 4;
    }

    gain_ = compute_gain_constant(design, params, sample_rate_hz);
    state_.assign(coefficients_.size(), 0.0);
    active_ = !coefficients_.empty() && gain_ != 0.0;
    runtime_type_ = active_ ? RuntimeType::Iir : RuntimeType::None;
}

double FastIirFilter::run_sample(double sample) {
    if (runtime_type_ == RuntimeType::Fir) {
        fir_history_[fir_taps_.size()] = sample;
        double value = 0.0;
        for (std::size_t i = 0; i < fir_taps_.size(); ++i) {
            fir_history_[i] = fir_history_[i + 1];
            value += fir_history_[i] * fir_taps_[i];
        }
        return value;
    }
    if (runtime_type_ == RuntimeType::Fft) {
        throw std::logic_error("FFT filter requires block run()");
    }
    // FastIIRFilter.runFilter(double): coefficient layout a1,a2,b1,b2 per
    // stage, two state slots per stage, gain divided once at the end.
    double x = sample;
    const std::size_t n_coefs = coefficients_.size();
    for (std::size_t i = 0, j = 0; i < n_coefs; i += 4, j += 2) {
        const double p0 = coefficients_[i + 2] * state_[j] + coefficients_[i + 3] * state_[j + 1];
        const double p1 = coefficients_[i] * state_[j] + coefficients_[i + 1] * state_[j + 1];
        state_[j + 1] = state_[j];
        state_[j] = x + p0;
        x += p0 + p1;
    }
    return x / gain_;
}

void FastIirFilter::run(const std::vector<double>& input, std::vector<double>& output) {
    if (runtime_type_ == RuntimeType::Fft) {
        if (input.empty()) {
            output.clear();
            return;
        }
        auto packed = JtFft::real_forward(input, input.size());
        const auto fft_bin = [&](double frequency) {
            return static_cast<std::size_t>(std::clamp<long long>(
                std::llround(frequency * input.size() / sample_rate_hz_), 0,
                static_cast<long long>(input.size() / 2 - 1)));
        };
        const auto zero_bin = [&](std::size_t bin) {
            packed[2 * bin] = 0.0;
            packed[2 * bin + 1] = 0.0;
        };
        const auto high = fft_bin(params_.high_pass_freq_hz);
        const auto low = fft_bin(params_.low_pass_freq_hz);
        const auto lower = std::min(high, low);
        const auto upper = std::max(high, low);
        switch (params_.band) {
        case IirFilterBand::HighPass:
            for (std::size_t bin = 0; bin < high; ++bin) {
                zero_bin(bin);
            }
            break;
        case IirFilterBand::LowPass:
            for (std::size_t bin = low; bin < input.size() / 2; ++bin) {
                zero_bin(bin);
            }
            break;
        case IirFilterBand::BandPass:
            for (std::size_t bin = 0; bin < lower; ++bin) {
                zero_bin(bin);
            }
            for (std::size_t bin = upper; bin < input.size() / 2; ++bin) {
                zero_bin(bin);
            }
            break;
        case IirFilterBand::BandStop:
            for (std::size_t bin = lower; bin < upper; ++bin) {
                zero_bin(bin);
            }
            break;
        }
        output = JtFft::real_inverse(packed);
        return;
    }
    output.resize(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        output[i] = run_sample(input[i]);
    }
}

void FastIirFilter::reset() {
    std::fill(state_.begin(), state_.end(), 0.0);
    std::fill(fir_history_.begin(), fir_history_.end(), 0.0);
}

} // namespace pamguard::dsp
