#pragma once

#include <optional>
#include <vector>

namespace pamguard::localisation {

struct PairBearingConfig {
    double spacing_m = 0.0;
    double spacing_error_m = 0.0;
    double speed_of_sound_mps = 1500.0;
    double speed_of_sound_error_mps = 0.0;
    double timing_error_seconds = 0.0;
    double wobble_radians = 0.0;
};

struct PairBearingResult {
    double angle_radians = 0.0;
    double error_radians = 0.0;
};

/**
 * Port of PAMGuard's PairBearingLocaliser.localise() maths for two-element
 * closely spaced arrays: angle from the pair axis as acos of the clamped
 * normalised delay, plus PAMGuard's error propagation formula (reproduced
 * faithfully, including its behaviour at endfire and for negative spacing).
 * Spacing may be negative, matching PAMGuard's axis-direction sign flip.
 */
class PairBearingLocaliser {
public:
    explicit PairBearingLocaliser(PairBearingConfig config);

    [[nodiscard]] const PairBearingConfig& config() const noexcept;

    /**
     * Mirrors PairBearingLocaliser.localise(double[], long): empty input
     * yields no result and a three-delay input uses only the middle delay
     * (PAMGuard's own reduction for that case).
     */
    [[nodiscard]] std::optional<PairBearingResult> localise(const std::vector<double>& delays_seconds) const;

private:
    PairBearingConfig config_;
};

} // namespace pamguard::localisation
