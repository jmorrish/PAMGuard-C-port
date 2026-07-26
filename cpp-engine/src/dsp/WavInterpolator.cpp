#include "pamguard/dsp/WavInterpolator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>
#include <utility>

namespace pamguard::dsp {

namespace {

using Complex = std::complex<double>;

struct Biquad {
    double a1 = 0.0;
    double a2 = 0.0;
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double v1 = 0.0;
    double v2 = 0.0;

    double process(double input) {
        // iirj DirectFormII.process1, including statement order.
        const double w =
            input - a1 * v1 - a2 * v2;
        const double output =
            b0 * w + b1 * v1 + b2 * v2;
        v2 = v1;
        v1 = w;
        return output;
    }
};

std::vector<Biquad> butterworth_four_low_pass(
    double sample_rate_hz,
    double cutoff_hz) {
    constexpr int order = 4;
    const double normalized_cutoff =
        cutoff_hz / sample_rate_hz;
    const double prewarp =
        std::tan(std::numbers::pi * normalized_cutoff);

    std::vector<Biquad> stages;
    stages.reserve(order / 2);
    for (int index = 0;
         index < order / 2;
         ++index) {
        // iirj Butterworth.AnalogLowPass.design.
        const double angle =
            std::numbers::pi / 2.0 +
            static_cast<double>(2 * index + 1) *
                std::numbers::pi /
                static_cast<double>(2 * order);
        const Complex analog_pole =
            std::polar(1.0, angle);

        // iirj LowPassTransform.transform.
        const Complex warped =
            analog_pole * prewarp;
        const Complex digital_pole =
            (Complex(1.0, 0.0) + warped) /
            (Complex(1.0, 0.0) - warped);

        // Biquad.setTwoPole for a conjugate pole pair and zeros at -1.
        Biquad stage;
        stage.a1 = -2.0 * digital_pole.real();
        stage.a2 =
            std::abs(digital_pole) *
            std::abs(digital_pole);
        stage.b0 = 1.0;
        stage.b1 = 2.0;
        stage.b2 = 1.0;
        stages.push_back(stage);
    }

    // Cascade.setLayout normalises at analog normalW=0, normalGain=1 and
    // applies the complete gain only to the first biquad.
    Complex numerator(1.0, 0.0);
    Complex denominator(1.0, 0.0);
    const Complex z1(1.0, 0.0);
    const Complex z2(1.0, 0.0);
    for (const auto& stage : stages) {
        Complex top(stage.b0, 0.0);
        top += stage.b1 * z1;
        top += stage.b2 * z2;
        Complex bottom(1.0, 0.0);
        bottom += stage.a1 * z1;
        bottom += stage.a2 * z2;
        numerator *= top;
        denominator *= bottom;
    }
    const double scale =
        1.0 / std::abs(numerator / denominator);
    stages.front().b0 *= scale;
    stages.front().b1 *= scale;
    stages.front().b2 *= scale;
    return stages;
}

std::vector<double> filter_four_pole_low_pass(
    std::span<const double> input,
    double sample_rate_hz,
    double cutoff_hz) {
    auto stages =
        butterworth_four_low_pass(
            sample_rate_hz,
            cutoff_hz);
    std::vector<double> output;
    output.reserve(input.size());
    for (double value : input) {
        for (auto& stage : stages) {
            value = stage.process(value);
        }
        output.push_back(value);
    }
    return output;
}

struct NaturalSpline {
    std::vector<double> knots;
    std::vector<double> y;
    std::vector<double> b;
    std::vector<double> c;
    std::vector<double> d;

    double value(double point) const {
        auto found =
            std::lower_bound(
                knots.begin(),
                knots.end(),
                point);
        std::size_t interval = 0;
        if (found == knots.end()) {
            interval = knots.size() - 2;
        }
        else if (*found == point) {
            interval = static_cast<std::size_t>(
                found - knots.begin());
            if (interval >= b.size()) {
                --interval;
            }
        }
        else {
            interval = static_cast<std::size_t>(
                found - knots.begin() - 1);
        }
        const double argument =
            point - knots[interval];
        // PolynomialFunction.evaluate's Horner order.
        double result = d[interval];
        result =
            argument * result + c[interval];
        result =
            argument * result + b[interval];
        result =
            argument * result + y[interval];
        return result;
    }
};

NaturalSpline natural_spline(
    std::span<const double> values) {
    if (values.size() < 3) {
        throw WavInterpolatorError(
            "WavInterpolator cubic spline requires at least 3 input samples");
    }
    const std::size_t intervals =
        values.size() - 1;
    NaturalSpline spline;
    spline.knots.resize(values.size());
    spline.y.assign(values.begin(), values.end());
    for (std::size_t index = 0;
         index < values.size();
         ++index) {
        spline.knots[index] =
            static_cast<double>(index) /
            static_cast<double>(values.size() - 1);
    }

    std::vector<double> h(intervals);
    for (std::size_t index = 0;
         index < intervals;
         ++index) {
        h[index] =
            spline.knots[index + 1] -
            spline.knots[index];
    }
    std::vector<double> mu(intervals, 0.0);
    std::vector<double> z(values.size(), 0.0);
    double g = 0.0;
    for (std::size_t index = 1;
         index < intervals;
         ++index) {
        g = 2.0 *
                (spline.knots[index + 1] -
                 spline.knots[index - 1]) -
            h[index - 1] * mu[index - 1];
        mu[index] = h[index] / g;
        z[index] =
            (3.0 *
                     (values[index + 1] * h[index - 1] -
                      values[index] *
                          (spline.knots[index + 1] -
                           spline.knots[index - 1]) +
                      values[index - 1] * h[index]) /
                     (h[index - 1] * h[index]) -
                 h[index - 1] * z[index - 1]) /
            g;
    }

    spline.b.resize(intervals);
    spline.c.resize(values.size());
    spline.d.resize(intervals);
    z[intervals] = 0.0;
    spline.c[intervals] = 0.0;
    for (std::size_t cursor = intervals;
         cursor > 0;
         --cursor) {
        const std::size_t index = cursor - 1;
        spline.c[index] =
            z[index] -
            mu[index] * spline.c[index + 1];
        spline.b[index] =
            (values[index + 1] - values[index]) /
                h[index] -
            h[index] *
                (spline.c[index + 1] +
                 2.0 * spline.c[index]) /
                3.0;
        spline.d[index] =
            (spline.c[index + 1] -
             spline.c[index]) /
            (3.0 * h[index]);
    }
    return spline;
}

} // namespace

std::vector<double> wav_interpolator_decimate(
    std::span<const double> input,
    double source_rate_hz,
    double target_rate_hz) {
    if (input.empty()) {
        throw WavInterpolatorError(
            "WavInterpolator input cannot be empty");
    }
    if (!std::isfinite(source_rate_hz) ||
        !std::isfinite(target_rate_hz) ||
        !(source_rate_hz > 0.0) ||
        !(target_rate_hz > 0.0)) {
        throw WavInterpolatorError(
            "WavInterpolator sample rates must be finite and positive");
    }
    if (target_rate_hz > source_rate_hz) {
        throw WavInterpolatorError(
            "WavInterpolator decimate target rate cannot exceed source rate");
    }
    if (target_rate_hz == source_rate_hz) {
        return {input.begin(), input.end()};
    }

    const double output_size_value =
        static_cast<double>(input.size()) *
        target_rate_hz / source_rate_hz;
    if (!(output_size_value >= 0.0) ||
        output_size_value >
            static_cast<double>(
                std::numeric_limits<std::size_t>::max())) {
        throw WavInterpolatorError(
            "WavInterpolator decimation output length is out of range");
    }
    const auto output_size =
        static_cast<std::size_t>(
            output_size_value);

    const auto filtered =
        filter_four_pole_low_pass(
            input,
            source_rate_hz,
            target_rate_hz / 2.0);
    const auto spline =
        natural_spline(filtered);

    std::vector<double> output(output_size);
    for (std::size_t index = 0;
         index < output_size;
         ++index) {
        output[index] = spline.value(
            static_cast<double>(index) /
            static_cast<double>(output_size));
    }
    return output;
}

} // namespace pamguard::dsp
