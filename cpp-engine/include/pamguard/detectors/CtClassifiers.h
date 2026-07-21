#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "pamguard/detectors/CtChi2Classifier.h"

namespace pamguard::detectors {

/** PamArrayUtils.median(double[]): sorted middle, averaging the middle pair. */
[[nodiscard]] inline double ct_median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    auto n = values.size();
    if (n % 2 == 0) {
        n /= 2;
        return (values[n] + values[n - 1]) / 2.0;
    }
    n /= 2;
    return values[n];
}

/** PamArrayUtils.mean(double[]). */
[[nodiscard]] inline double ct_mean(const std::vector<double>& values) {
    double sum = 0.0;
    for (const auto value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

/** PamArrayUtils.std(double[]): population standard deviation. */
[[nodiscard]] inline double ct_std(const std::vector<double>& values) {
    const double mean = ct_mean(values);
    double total = 0.0;
    for (const auto value : values) {
        total += (mean - value) * (mean - value);
    }
    return std::sqrt(total / static_cast<double>(values.size()));
}

struct CtIdiClassifierConfig {
    /** IDIClassifierParams defaults; limits in seconds. */
    bool use_median_idi = true;
    double min_median_idi = 0.0;
    double max_median_idi = 2.0;
    bool use_mean_idi = false;
    double min_mean_idi = 0.0;
    double max_mean_idi = 2.0;
    bool use_std_idi = false;
    double min_std_idi = 0.0;
    double max_std_idi = 100.0;
    int species_flag = 1;
};

struct CtIdiSummary {
    double median_idi = 0.0;
    double mean_idi = 0.0;
    double std_idi = 0.0;
};

/**
 * Port of PAMGuard's IDIClassifier: each enabled criterion must place the
 * train's IDI statistic inside its inclusive limits. The statistics
 * themselves come from the IDI summary layer, which has bitwise Java
 * fixture parity (`docs/156`).
 */
class CtIdiClassifier {
public:
    CtIdiClassifier() = default;
    explicit CtIdiClassifier(CtIdiClassifierConfig config) : config_(config) {}

    [[nodiscard]] CtClassificationResult classify(const CtIdiSummary& idi) const {
        if (config_.use_median_idi &&
            (idi.median_idi < config_.min_median_idi || idi.median_idi > config_.max_median_idi)) {
            return {};
        }
        if (config_.use_mean_idi &&
            (idi.mean_idi < config_.min_mean_idi || idi.mean_idi > config_.max_mean_idi)) {
            return {};
        }
        if (config_.use_std_idi &&
            (idi.std_idi < config_.min_std_idi || idi.std_idi > config_.max_std_idi)) {
            return {};
        }
        return CtClassificationResult{config_.species_flag};
    }

private:
    CtIdiClassifierConfig config_;
};

struct CtBearingClassifierConfig {
    /** BearingClassifierParams defaults; angles in radians. */
    double bearing_lim_min = 85.0 * 3.141592653589793238462643383279502884 / 180.0;
    double bearing_lim_max = 95.0 * 3.141592653589793238462643383279502884 / 180.0;
    bool use_mean = false;
    double min_mean_bearing_derivative = -0.005 * 3.141592653589793238462643383279502884 / 180.0;
    double max_mean_bearing_derivative = 0.005 * 3.141592653589793238462643383279502884 / 180.0;
    bool use_median = true;
    double min_median_bearing_derivative = -0.005 * 3.141592653589793238462643383279502884 / 180.0;
    double max_median_bearing_derivative = 0.005 * 3.141592653589793238462643383279502884 / 180.0;
    bool use_std = true;
    double min_std_bearing_derivative = 0.0;
    double max_std_bearing_derivative = 1.5 * 3.141592653589793238462643383279502884 / 180.0;
    int species_flag = -1;
};

/** One click in a train as the bearing classifier sees it. */
struct CtBearingClick {
    std::int64_t time_ms = 0;
    /** Absent when the click has no localisation, as PAMGuard allows. */
    std::optional<double> bearing_radians;
};

struct CtBearingClassificationResult {
    int species_id = kCtNoSpecies;
    double mean_bearing_derivative = 0.0;
    double median_bearing_derivative = 0.0;
    double std_bearing_derivative = 0.0;
    bool valid = false;
};

/**
 * Port of PAMGuard's BearingClassifier. Bearing derivatives (radians per
 * second) are taken between successive clicks, then mean/median/population
 * standard deviation are tested against the configured limits, along with a
 * range test that passes when EITHER the minimum or the maximum bearing
 * falls inside the bearing limits.
 *
 * Two reference behaviours are preserved deliberately: clicks without a
 * localisation are skipped but leave a zero in the derivative array (rather
 * than shortening it), and the final bearing is filled in after the loop, so
 * the bearing array's last entry is always taken even though the loop stops
 * one short. Trains with fewer than three clicks, or with too many missing
 * localisations, return no species.
 */
class CtBearingClassifier {
public:
    CtBearingClassifier() = default;
    explicit CtBearingClassifier(CtBearingClassifierConfig config) : config_(config) {}

    [[nodiscard]] CtBearingClassificationResult classify(const std::vector<CtBearingClick>& clicks) const {
        CtBearingClassificationResult result;
        const auto count = clicks.size();
        if (count < 3) {
            return result;
        }

        std::vector<double> bearing_diff(count - 1, 0.0);
        std::vector<double> bearing(count, 0.0);
        std::size_t null_count = 0;
        for (std::size_t i = 0; i + 1 < count; ++i) {
            if (!clicks[i].bearing_radians.has_value()) {
                ++null_count;
                continue;
            }
            bearing[i] = *clicks[i].bearing_radians;
            const double next = clicks[i + 1].bearing_radians.value_or(0.0);
            const double time_diff = static_cast<double>(clicks[i + 1].time_ms - clicks[i].time_ms) / 1000.0;
            bearing_diff[i] = (*clicks[i].bearing_radians - next) / time_diff;
        }

        if (null_count + 4 > count) {
            return result;
        }

        bearing[bearing.size() - 1] = clicks[bearing.size() - 1].bearing_radians.value_or(0.0);

        const double min = *std::min_element(bearing.begin(), bearing.end());
        const double max = *std::max_element(bearing.begin(), bearing.end());
        result.mean_bearing_derivative = ct_mean(bearing_diff);
        result.median_bearing_derivative = ct_median(bearing_diff);
        result.std_bearing_derivative = ct_std(bearing_diff);
        result.valid = true;

        // Range test passes when either extreme falls inside the limits.
        bool passed = (min >= config_.bearing_lim_min && min <= config_.bearing_lim_max) ||
            (max >= config_.bearing_lim_min && max <= config_.bearing_lim_max);
        if (config_.use_mean && !(result.mean_bearing_derivative >= config_.min_mean_bearing_derivative &&
                                  result.mean_bearing_derivative <= config_.max_mean_bearing_derivative)) {
            passed = false;
        }
        if (config_.use_median && !(result.median_bearing_derivative >= config_.min_median_bearing_derivative &&
                                    result.median_bearing_derivative <= config_.max_median_bearing_derivative)) {
            passed = false;
        }
        if (config_.use_std && !(result.std_bearing_derivative >= config_.min_std_bearing_derivative &&
                                 result.std_bearing_derivative <= config_.max_std_bearing_derivative)) {
            passed = false;
        }

        if (passed) {
            result.species_id = config_.species_flag;
        }
        return result;
    }

private:
    CtBearingClassifierConfig config_;
};

} // namespace pamguard::detectors
