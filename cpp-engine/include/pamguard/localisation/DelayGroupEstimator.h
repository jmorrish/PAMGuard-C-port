#pragma once

#include <cstddef>
#include <vector>

#include "pamguard/localisation/CorrelationDelayEstimator.h"
#include "pamguard/localisation/WorldVectors.h"

namespace pamguard::localisation {

struct ChannelPairDelay {
    std::size_t pair_index = 0;
    std::size_t channel_a = 0;
    std::size_t channel_b = 0;
    std::size_t audio_channel_a = 0;
    std::size_t audio_channel_b = 0;
    bool geometry_constrained = false;
    double max_delay_samples = 0.0;
    double hydrophone_distance_m = 0.0;
    bool pair_bearing_valid = false;
    double pair_bearing_radians = 0.0;
    double pair_bearing_error_radians = 0.0;
    /**
     * The pair's cone angle as unit vectors in the array's xyz frame. A pair
     * is a line sub-array, so there are always two, both cones, carrying the
     * left/right ambiguity the pair cannot resolve.
     */
    std::vector<WorldVector> pair_bearing_world_vectors;
    TimeDelayData delay;
};

class DelayGroupEstimator {
public:
    std::vector<ChannelPairDelay> estimate_delays(
        const std::vector<std::vector<double>>& waveforms,
        const std::vector<double>& max_delay_samples = {});

private:
    CorrelationDelayEstimator correlation_;

    [[nodiscard]] static std::size_t next_power_of_two(std::size_t value) noexcept;
};

} // namespace pamguard::localisation
