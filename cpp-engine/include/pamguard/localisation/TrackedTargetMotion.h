#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace pamguard::localisation {

/**
 * One already-resolved bearing in PAMGuard's local Cartesian frame.
 *
 * x is east, y is north, z is height, and both angles are radians clockwise
 * from north. The upstream adapter is responsible for supplying one vector per
 * ambiguity side, exactly as TMGroupLocInfo::getWorldVectors(side) does.
 */
struct TrackedTargetMotionObservation {
    std::array<double, 3> origin_metres{};
    double heading_radians = 0.0;
    double bearing_radians = 0.0;
};

enum class TrackedTargetMotionStatus {
    success,
    invalid_point_limit,
    non_finite_input,
    no_observations,
    degenerate_fit,
    non_convergent_bearings,
};

/**
 * Direct output of PAMGuard's bearing-group Least Squares algorithm plus the
 * DetectionGroupLocaliser2 chi-square/AIC presentation values.
 *
 * A successful result can contain infinities/NaNs: pinned PAMGuard returns
 * true for a perfectly parallel bearing set and relies on its downstream
 * runaway/configured filters to reject it. This port deliberately preserves
 * that behaviour.
 */
struct TrackedTargetMotionResult {
    TrackedTargetMotionStatus status =
            TrackedTargetMotionStatus::no_observations;
    std::array<double, 3> position_metres{
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()};
    double raw_chi2 = std::numeric_limits<double>::quiet_NaN();
    double reduced_chi2 = std::numeric_limits<double>::quiet_NaN();
    double aic = std::numeric_limits<double>::quiet_NaN();
    double perpendicular_error_metres =
            std::numeric_limits<double>::quiet_NaN();
    double parallel_error_metres =
            std::numeric_limits<double>::quiet_NaN();
    double error_angle_radians =
            std::numeric_limits<double>::quiet_NaN();
    std::size_t reference_observation_index =
            std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> selected_observation_indices;

    [[nodiscard]] bool succeeded() const noexcept {
        return status == TrackedTargetMotionStatus::success;
    }
};

struct TrackedTargetMotionLimits {
    double max_range_metres = 20'000.0;
    double min_height_metres = -5'000.0;
    double max_height_metres = 5.0;
};

/**
 * The GPS/track adapter supplies these values after converting the Cartesian
 * fit back to a georeferenced location. LeastSquares itself cannot calculate
 * the closest GPS-path ("beam") distance.
 */
struct TrackedTargetMotionFilterInput {
    double perpendicular_distance_metres =
            std::numeric_limits<double>::quiet_NaN();
    double height_metres = std::numeric_limits<double>::quiet_NaN();
};

struct TrackedTargetMotionFilterAssessment {
    bool passes_runaway_guard = false;
    bool passes_configured_limits = false;
    bool accepted = false;
};

class TrackedTargetMotionLeastSquares final {
public:
    static constexpr std::int32_t java_unlimited_points = 2'147'483'647;
    static constexpr double java_earth_radius_metres = 6'371'000.0;

    /**
     * Reproduce TMGroupLocInfo.copySubDetections, including Java float
     * arithmetic and endpoint-preserving Math.round selection.
     */
    [[nodiscard]] static std::vector<std::size_t>
    select_observation_indices(
            std::size_t observation_count,
            std::int32_t max_localisation_points =
                    java_unlimited_points);

    /**
     * Port of Localiser.algorithms.genericLocaliser.leastSquares.LeastSquares
     * for one ambiguity side.
     */
    [[nodiscard]] static TrackedTargetMotionResult localise_side(
            const std::vector<TrackedTargetMotionObservation>& observations,
            std::int32_t max_localisation_points =
                    java_unlimited_points);

    /**
     * Run each ambiguity side independently, matching
     * DetectionGroupLocaliser2.localiseMin.
     */
    [[nodiscard]] static std::vector<TrackedTargetMotionResult>
    localise_ambiguities(
            const std::vector<
                    std::vector<TrackedTargetMotionObservation>>&
                    ambiguity_sides,
            std::int32_t max_localisation_points =
                    java_unlimited_points);

    /**
     * Apply DetectionGroupLocaliser2's 2*pi*Earth-radius runaway check followed
     * by GeneralGroupLocaliser.resultsFilterOK.
     */
    [[nodiscard]] static TrackedTargetMotionFilterAssessment assess_filters(
            bool fit_succeeded,
            const TrackedTargetMotionFilterInput& filter_input,
            const TrackedTargetMotionLimits& limits = {});
};

} // namespace pamguard::localisation
