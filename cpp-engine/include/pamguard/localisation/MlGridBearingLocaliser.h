#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

#include "pamguard/localisation/ArrayShape.h"

namespace pamguard::localisation {

struct MlGridHydrophone {
    /** Absolute position, streamer offsets already resolved. */
    std::array<double, 3> position_m{};
    /** PAMGuard Hydrophone.getCoordinateErrors: per-axis position error. */
    std::array<double, 3> position_error_m{};
    int streamer_id = 0;
};

struct MlGridBearingConfig {
    /** Hydrophones taking part, in the order their delay pairs are formed. */
    std::vector<MlGridHydrophone> hydrophones;
    double speed_of_sound_mps = 1500.0;
    double speed_of_sound_error_mps = 0.0;
    double timing_error_seconds = 0.0;
    /**
     * PAMGuard's default grid is 3 degrees in both angles. Written as
     * `degrees * (pi / 180)` to match Math.toRadians, which multiplies by a
     * precomputed constant. Dividing by 180 afterwards instead lands one ulp
     * away, which is enough to move the floor() that sets the theta bin count
     * from 120 bins to 121.
     */
    double theta_step_radians = 3.0 * (3.141592653589793238462643383279502884 / 180.0);
    double phi_step_radians = 3.0 * (3.141592653589793238462643383279502884 / 180.0);
    /**
     * Select `MLLineBearingLocaliser2`'s theta convention, which is the whole
     * of that subclass: it overrides `thetaBinToAngle` to return
     * `pi/2 - super.thetaBinToAngle(bin)`.
     *
     * Because Java dispatches virtually and `prepare` calls that method while
     * building the delay table, the flag changes the **table** as well as the
     * reported angle — the two are not independent, and treating this as a
     * post-hoc output transform would give different numbers.
     *
     * PAMGuard selects that subclass only for a line sub-array of more than
     * two hydrophones **and** only when `SMRUEnable.isEnable()`, which gates
     * licensed extras absent from the open distribution. The default build
     * takes the pair localiser instead, so the engine's selector never asks
     * for this; it exists so an SMRU-licensed deployment can be reproduced.
     */
    bool line_theta_convention = false;
};

struct MlGridBearingResult {
    double theta_radians = 0.0;
    double theta_error_radians = 0.0;
    double phi_radians = 0.0;
    double phi_error_radians = 0.0;
    /** False for a line sub-array, where PAMGuard returns theta alone. */
    bool has_phi = false;
};

/**
 * Port of PAMGuard MLGridBearingLocaliser2 — the localiser
 * BearingLocaliserSelector picks for plane and volume sub-arrays.
 *
 * The algorithm builds a lookup table of expected delays and delay errors over
 * a theta/phi grid, scores an observed delay set against it as a Gaussian log
 * likelihood, and takes the interpolated peak. Grid extents and peak-search
 * wrapping follow the array shape, exactly as PAMGuard's `prepare` does:
 *
 * - Line:   theta over [0, pi], a single phi bin at 0, no wrapping.
 * - Plane:  theta over [-pi, pi], phi over [0, pi/2], theta wraps with step 2.
 * - Volume: theta over [-pi, pi], phi over [-pi/2, pi/2], theta wraps.
 *
 * The theta grid's first and last bins are the same angle, which is why
 * PAMGuard's wrap step is 2 rather than 1.
 */
class MlGridBearingLocaliser {
public:
    explicit MlGridBearingLocaliser(MlGridBearingConfig config);

    /**
     * `delays_seconds` must hold one delay per hydrophone pair in PAMGuard's
     * order (0-1, 0-2, ..., 1-2, ...). Returns nothing when the pair count
     * does not match, when the array-axis matrix is singular (PAMGuard's
     * `prepare` returns early and leaves an empty table), or when any delay
     * error is zero — that would divide by zero in the likelihood.
     */
    [[nodiscard]] std::optional<MlGridBearingResult> localise(const std::vector<double>& delays_seconds) const;

    [[nodiscard]] ArrayShapeType array_type() const noexcept { return array_type_; }
    [[nodiscard]] std::size_t theta_bin_count() const noexcept { return theta_bins_; }
    [[nodiscard]] std::size_t phi_bin_count() const noexcept { return phi_bins_; }
    [[nodiscard]] bool prepared() const noexcept { return prepared_; }

    /** Exposed for tests: the log-likelihood surface for a delay set. */
    [[nodiscard]] std::vector<std::vector<double>> likelihood_surface(const std::vector<double>& delays_seconds) const;

private:
    [[nodiscard]] double theta_bin_to_angle(double bin) const;
    [[nodiscard]] double phi_bin_to_angle(double bin) const;
    [[nodiscard]] double likelihood_at(const std::vector<double>& delays_seconds, std::size_t theta_bin,
                                       std::size_t phi_bin) const;
    [[nodiscard]] std::array<double, 2> errors_at(const std::vector<double>& delays_seconds, long theta_bin,
                                                  long phi_bin) const;

    MlGridBearingConfig config_;
    ArrayShapeType array_type_ = ArrayShapeType::None;
    bool prepared_ = false;
    bool wrap_theta_ = false;
    int wrap_theta_step_ = 1;
    std::array<double, 2> theta_range_{};
    std::array<double, 2> phi_range_{};
    std::size_t theta_bins_ = 0;
    std::size_t phi_bins_ = 0;
    std::size_t pair_count_ = 0;
    /** [theta][phi][pair] */
    std::vector<double> delay_grid_;
    std::vector<double> delay_error_grid_;
};

} // namespace pamguard::localisation
