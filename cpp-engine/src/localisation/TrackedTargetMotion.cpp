#include "pamguard/localisation/TrackedTargetMotion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace pamguard::localisation {
namespace {

constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double fixed_bearing_error_radians = 3.0 * pi / 180.0;

double constrain_to_pi(double angle) {
    if (!std::isfinite(angle)) {
        return angle;
    }
    while (angle > pi) {
        angle -= 2.0 * pi;
    }
    while (angle <= -pi) {
        angle += 2.0 * pi;
    }
    return angle;
}

std::int32_t java_round_float(const float value) {
    if (std::isnan(value)) {
        return 0;
    }
    if (value <= static_cast<float>(
                         std::numeric_limits<std::int32_t>::min())) {
        return std::numeric_limits<std::int32_t>::min();
    }
    if (value >= static_cast<float>(
                         std::numeric_limits<std::int32_t>::max())) {
        return std::numeric_limits<std::int32_t>::max();
    }
    return static_cast<std::int32_t>(
            std::floor(static_cast<double>(value + 0.5F)));
}

bool finite_observation(const TrackedTargetMotionObservation& observation) {
    return std::all_of(
                   observation.origin_metres.begin(),
                   observation.origin_metres.end(),
                   [](const double value) { return std::isfinite(value); })
            && std::isfinite(observation.heading_radians)
            && std::isfinite(observation.bearing_radians);
}

struct LinearFit {
    double a = std::numeric_limits<double>::quiet_NaN();
    double b = std::numeric_limits<double>::quiet_NaN();
    double siga = std::numeric_limits<double>::quiet_NaN();
    double sigb = std::numeric_limits<double>::quiet_NaN();
    double chi2 = 0.0;
};

/**
 * Exact weighted branch of Stats.LinFit used by Java LeastSquares.
 */
LinearFit linear_fit(
        const std::vector<double>& x,
        const std::vector<double>& y,
        const std::vector<double>& sig) {
    const auto count = x.size();
    double ss = 0.0;
    double sx = 0.0;
    double sy = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double weight = 1.0 / (sig[i] * sig[i]);
        ss += weight;
        sx += x[i] * weight;
        sy += y[i] * weight;
    }

    const double sxoss = sx / ss;
    double st2 = 0.0;
    double gradient_numerator = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double t = (x[i] - sxoss) / sig[i];
        st2 += t * t;
        gradient_numerator += t * y[i] / sig[i];
    }

    LinearFit result;
    result.b = gradient_numerator / st2;
    result.a = (sy - sx * result.b) / ss;
    result.siga = std::sqrt((1.0 + sx * sx / (ss * st2)) / ss);
    result.sigb = std::sqrt(1.0 / st2);
    if (count > 2) {
        for (std::size_t i = 0; i < count; ++i) {
            const double residual =
                    (y[i] - result.a - result.b * x[i]) / sig[i];
            result.chi2 += residual * residual;
        }
    }
    return result;
}

} // namespace

std::vector<std::size_t>
TrackedTargetMotionLeastSquares::select_observation_indices(
        const std::size_t observation_count,
        const std::int32_t max_localisation_points) {
    if (max_localisation_points < 0
        || observation_count
                > static_cast<std::size_t>(
                        std::numeric_limits<std::int32_t>::max())) {
        return {};
    }

    std::size_t kept = observation_count;
    if (max_localisation_points == 0
        || static_cast<std::size_t>(max_localisation_points)
                < observation_count) {
        kept = static_cast<std::size_t>(max_localisation_points);
    }
    if (kept == 0) {
        return {};
    }

    const float keep_ratio =
            static_cast<float>(observation_count - 1)
            / static_cast<float>(kept - 1);
    std::vector<std::size_t> selected;
    selected.reserve(kept);
    for (std::size_t i = 0; i < kept; ++i) {
        const auto index =
                java_round_float(static_cast<float>(i) * keep_ratio);
        selected.push_back(static_cast<std::size_t>(index));
    }
    return selected;
}

TrackedTargetMotionResult
TrackedTargetMotionLeastSquares::localise_side(
        const std::vector<TrackedTargetMotionObservation>& observations,
        const std::int32_t max_localisation_points) {
    TrackedTargetMotionResult result;
    if (max_localisation_points < 0
        || observations.size()
                > static_cast<std::size_t>(
                        std::numeric_limits<std::int32_t>::max())) {
        result.status = TrackedTargetMotionStatus::invalid_point_limit;
        return result;
    }

    result.selected_observation_indices = select_observation_indices(
            observations.size(),
            max_localisation_points);
    if (result.selected_observation_indices.empty()) {
        result.status = TrackedTargetMotionStatus::no_observations;
        return result;
    }

    for (const auto index : result.selected_observation_indices) {
        if (index >= observations.size()
            || !finite_observation(observations[index])) {
            result.status = TrackedTargetMotionStatus::non_finite_input;
            return result;
        }
    }

    double best_angle = 9999999.0;
    std::size_t best_selected_index =
            std::numeric_limits<std::size_t>::max();
    for (std::size_t i = 0;
         i < result.selected_observation_indices.size();
         ++i) {
        const auto& observation =
                observations[result.selected_observation_indices[i]];
        const double angle = std::abs(constrain_to_pi(
                observation.bearing_radians
                - observation.heading_radians));
        if (std::abs(angle - pi / 2.0)
            < std::abs(best_angle - pi / 2.0)) {
            best_selected_index = i;
            best_angle = angle;
        }
    }
    if (best_selected_index
        == std::numeric_limits<std::size_t>::max()) {
        result.status = TrackedTargetMotionStatus::no_observations;
        return result;
    }

    result.reference_observation_index =
            result.selected_observation_indices[best_selected_index];
    const auto& reference =
            observations[result.reference_observation_index];
    const double reference_heading = reference.heading_radians;
    const double reference_angle = pi / 2.0 - reference_heading;
    const double a = std::cos(reference_angle);
    const double b = -std::sin(-reference_angle);
    const double c = -b;
    const double d = a;

    const auto count = result.selected_observation_indices.size();
    std::vector<double> fit_x(count);
    std::vector<double> fit_y(count);
    std::vector<double> sig(count);
    double last_detection_angle =
            std::numeric_limits<double>::quiet_NaN();

    for (std::size_t i = 0; i < count; ++i) {
        const auto& observation =
                observations[result.selected_observation_indices[i]];
        const double xt =
                observation.origin_metres[0]
                - reference.origin_metres[0];
        const double yt =
                observation.origin_metres[1]
                - reference.origin_metres[1];
        double x = a * xt + b * yt;
        const double y = c * xt + d * yt;

        last_detection_angle = constrain_to_pi(
                -(observation.bearing_radians
                  - (pi / 2.0 - reference_angle)));
        const double inverse_tangent =
                1.0 / std::tan(last_detection_angle);
        x -= y * inverse_tangent;
        fit_x[i] = x;
        fit_y[i] = inverse_tangent;
        sig[i] = fixed_bearing_error_radians
                / std::pow(std::sin(last_detection_angle), 2.0);
    }

    const LinearFit fit = linear_fit(fit_x, fit_y, sig);
    const double y0 = -1.0 / fit.b;
    const double x0 = y0 * fit.a;
    if (std::isnan(x0) || std::isnan(y0)) {
        result.status = TrackedTargetMotionStatus::degenerate_fit;
        return result;
    }
    // This intentionally uses the final observation's angle, matching the
    // mutable local variable in pinned PAMGuard LeastSquares.
    if (last_detection_angle * y0 < 0.0) {
        result.status =
                TrackedTargetMotionStatus::non_convergent_bearings;
        return result;
    }

    const double range = std::sqrt(y0 * y0 + x0 * x0);
    const double local_bearing = std::atan2(y0, x0);
    double true_bearing_degrees =
            pi / 2.0 - (local_bearing + reference_angle);
    true_bearing_degrees *= 180.0 / pi;
    const double true_bearing_radians =
            true_bearing_degrees * pi / 180.0;
    const double direction_x = std::sin(true_bearing_radians);
    const double direction_y = std::cos(true_bearing_radians);
    const double direction_z = std::sin(0.0);

    result.position_metres = {
            reference.origin_metres[0] + direction_x * range,
            reference.origin_metres[1] + direction_y * range,
            reference.origin_metres[2] + direction_z * range};
    result.raw_chi2 = fit.chi2;
    result.reduced_chi2 = fit.chi2;
    if (count > 2) {
        result.reduced_chi2 /= static_cast<double>(count - 2);
    }
    result.aic = fit.chi2 + 4.0;
    result.perpendicular_error_metres =
            std::abs(fit.sigb / std::pow(fit.b, 2.0));
    result.parallel_error_metres = std::sqrt(
            std::pow(y0 * fit.siga, 2.0)
            + std::pow(
                    fit.a * result.perpendicular_error_metres,
                    2.0));
    result.error_angle_radians = reference_heading + pi / 2.0;
    result.status = TrackedTargetMotionStatus::success;
    return result;
}

std::vector<TrackedTargetMotionResult>
TrackedTargetMotionLeastSquares::localise_ambiguities(
        const std::vector<
                std::vector<TrackedTargetMotionObservation>>&
                ambiguity_sides,
        const std::int32_t max_localisation_points) {
    std::vector<TrackedTargetMotionResult> results;
    results.reserve(ambiguity_sides.size());
    for (const auto& side : ambiguity_sides) {
        results.push_back(localise_side(side, max_localisation_points));
    }
    return results;
}

TrackedTargetMotionFilterAssessment
TrackedTargetMotionLeastSquares::assess_filters(
        const bool fit_succeeded,
        const TrackedTargetMotionFilterInput& filter_input,
        const TrackedTargetMotionLimits& limits) {
    TrackedTargetMotionFilterAssessment result;
    const double runaway_limit =
            2.0 * pi * java_earth_radius_metres;
    // DetectionGroupLocaliser2 rejects only `>`, so NaN passes this first
    // guard exactly as it does in Java.
    result.passes_runaway_guard =
            !(filter_input.perpendicular_distance_metres > runaway_limit);
    result.passes_configured_limits =
            filter_input.perpendicular_distance_metres
                            <= limits.max_range_metres
            && filter_input.height_metres >= limits.min_height_metres
            && filter_input.height_metres <= limits.max_height_metres;
    result.accepted = fit_succeeded
            && result.passes_runaway_guard
            && result.passes_configured_limits;
    return result;
}

} // namespace pamguard::localisation
