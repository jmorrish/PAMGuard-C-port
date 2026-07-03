#include "pamguard/localisation/PairBearingLocaliser.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace pamguard::localisation {

PairBearingLocaliser::PairBearingLocaliser(PairBearingConfig config)
    : config_(std::move(config)) {
    if (config_.spacing_m == 0.0) {
        throw std::invalid_argument("pair bearing spacing_m must be non-zero");
    }
    if (config_.speed_of_sound_mps <= 0.0) {
        throw std::invalid_argument("pair bearing speed_of_sound_mps must be positive");
    }
}

const PairBearingConfig& PairBearingLocaliser::config() const noexcept {
    return config_;
}

std::optional<PairBearingResult> PairBearingLocaliser::localise(const std::vector<double>& delays_seconds) const {
    if (delays_seconds.empty()) {
        return std::nullopt;
    }
    const double delay = delays_seconds.size() == 3 ? delays_seconds[1] : delays_seconds[0];

    double ct = config_.speed_of_sound_mps * delay / config_.spacing_m;
    ct = std::max(-1.0, std::min(1.0, ct));

    const double angle = std::acos(ct);

    const double e1 = config_.speed_of_sound_mps * config_.timing_error_seconds;
    const double e2 = config_.speed_of_sound_mps * delay / config_.spacing_m * config_.spacing_error_m;
    const double e3 = delay * config_.speed_of_sound_error_mps;
    double error = (e1 * e1 + e2 * e2 + e3 * e3) / config_.spacing_m / std::sin(angle);
    error += config_.wobble_radians;
    error = std::sqrt(error);

    return PairBearingResult{angle, error};
}

} // namespace pamguard::localisation
