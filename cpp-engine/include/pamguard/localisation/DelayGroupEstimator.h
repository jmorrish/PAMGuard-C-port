#pragma once

#include <cstddef>
#include <vector>

#include "pamguard/localisation/CorrelationDelayEstimator.h"
#include "pamguard/localisation/WorldVectors.h"

namespace pamguard::localisation {

enum class DelayFilterBand {
    HighPass,
    LowPass,
    BandPass,
    BandStop,
};

/**
 * Localiser.DelayMeasurementParams, excluding leading_edge_search_region:
 * that region is derived for each click from preSample +/- the largest
 * geometry delay, just as ClickDetector.measureDelays does.
 */
struct DelayMeasurementConfig {
    bool filter_bearings = false;
    DelayFilterBand filter_band = DelayFilterBand::HighPass;
    double filter_high_pass_hz = 0.0;
    double filter_low_pass_hz = 0.0;
    bool envelope_bearings = false;
    bool use_leading_edge = false;
    int up_sample = 1;
    bool use_restricted_bins = false;
    std::size_t restricted_bins = 80;
    /** Runtime-derived, inclusive sample region; negative means unspecified. */
    int leading_edge_search_start = -1;
    int leading_edge_search_end = -1;
};

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
    /** The same vectors rotated into the earth frame; empty when the array declares no orientation. */
    std::vector<WorldVector> pair_bearing_earth_world_vectors;
    TimeDelayData delay;
};

class DelayGroupEstimator {
public:
    std::vector<ChannelPairDelay> estimate_delays(
        const std::vector<std::vector<double>>& waveforms,
        const std::vector<double>& max_delay_samples = {},
        double sample_rate_hz = 0.0,
        const DelayMeasurementConfig& config = {});

private:
    CorrelationDelayEstimator correlation_;

    [[nodiscard]] static std::size_t next_power_of_two(std::size_t value) noexcept;
};

} // namespace pamguard::localisation
