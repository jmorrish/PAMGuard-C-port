#include "pamguard/dsp/ClickRemoval.h"

#include <cmath>

namespace pamguard::dsp {

std::vector<double> remove_clicks(
    const std::vector<double>& source,
    double threshold,
    double power) {
    std::vector<double> result(source.size(), 0.0);
    if (source.empty()) {
        return result;
    }

    double mean = 0.0;
    for (const double sample : source) {
        mean += sample;
    }
    mean /= static_cast<double>(source.size());

    double variance_sum = 0.0;
    for (const double sample : source) {
        const double difference = sample - mean;
        variance_sum += difference * difference;
    }
    const double standard_deviation = std::sqrt(
        variance_sum / static_cast<double>(source.size() - 1));
    const double click_threshold = threshold * standard_deviation;

    // This is deliberately the same test as Java. In particular, NaN takes
    // the weighted branch, preserving the reference's edge-case behaviour.
    if (click_threshold <= 0.0) {
        return source;
    }
    for (std::size_t index = 0; index < source.size(); ++index) {
        const double normalized =
            (source[index] - mean) / click_threshold;
        const double weight =
            1.0 / (1.0 + std::pow(normalized, power));
        result[index] = source[index] * weight;
    }
    return result;
}

} // namespace pamguard::dsp
