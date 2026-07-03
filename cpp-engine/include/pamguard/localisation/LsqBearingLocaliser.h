#pragma once

#include <array>
#include <optional>
#include <vector>

namespace pamguard::localisation {

struct LsqPairGeometry {
    /** Baseline vector between the pair's hydrophones, metres (j minus i in PAMGuard's i<j pair order). */
    std::array<double, 3> baseline_m{};
    /** Separation error vector for the pair, metres. */
    std::array<double, 3> error_m{};
};

struct LsqBearingConfig {
    double speed_of_sound_mps = 1500.0;
    double speed_of_sound_error_mps = 0.0;
    double timing_error_seconds = 0.0;
    std::vector<LsqPairGeometry> pairs;
};

struct LsqBearingResult {
    double azimuth_radians = 0.0;
    double elevation_radians = 0.0;
    double azimuth_error_radians = 0.0;
    double elevation_error_radians = 0.0;
};

/**
 * Port of PAMGuard's LSQBearingLocaliser: weighted least-squares bearing from
 * pairwise time delays using a replica of Jama's Householder QR (including
 * Jama's scaled hypot), PAMGuard's fit weights (pair length over the error
 * component along the pair, squared), the negated-delay right hand side, the
 * heading-style angle convention (pi/2 minus atan2(x, y), asin(z)), and the
 * curvature-based error estimate whose reported values come from the last
 * (20 degree) iteration. The log-likelihood reproduces PAMGuard's per-pair
 * chi overwrite rather than accumulation.
 *
 * Rank-deficient geometry (any 3-hydrophone pair set, or collinear/coplanar
 * sets) yields no result, where PAMGuard's Jama solve throws.
 */
class LsqBearingLocaliser {
public:
    explicit LsqBearingLocaliser(LsqBearingConfig config);

    [[nodiscard]] const LsqBearingConfig& config() const noexcept;
    [[nodiscard]] std::optional<LsqBearingResult> localise(const std::vector<double>& delays_seconds) const;

private:
    LsqBearingConfig config_;
    std::vector<double> fit_weights_;
    std::vector<std::array<double, 3>> hydrophone_vectors_;
    std::vector<std::array<double, 3>> hydrophone_error_vectors_;
    std::vector<std::array<double, 3>> qr_;
    std::array<double, 3> r_diag_{};

    [[nodiscard]] std::optional<std::array<double, 3>> qr_solve(std::vector<double> rhs) const;
    [[nodiscard]] double log_likelihood(const std::vector<double>& delays_seconds, double angle0, double angle1) const;
};

} // namespace pamguard::localisation
