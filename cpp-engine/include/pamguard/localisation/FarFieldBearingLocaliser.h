#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pamguard/localisation/DelayGroupEstimator.h"

namespace pamguard::localisation {

struct HydrophonePosition {
    std::size_t channel = 0;
    double x_m = 0.0;
    double y_m = 0.0;
    double z_m = 0.0;
};

struct FarFieldBearingConfig {
    double sample_rate_hz = 0.0;
    double speed_of_sound_mps = 1500.0;
    std::vector<HydrophonePosition> hydrophones;
};

struct FarFieldBearingResult {
    std::size_t click_index = 0;
    std::int64_t click_start_sample = 0;
    bool valid = false;
    double unit_x = 0.0;
    double unit_y = 0.0;
    double unit_z = 0.0;
    double azimuth_degrees = 0.0;
    double elevation_degrees = 0.0;
    double residual_rms_seconds = 0.0;
    std::size_t used_pairs = 0;
};

class FarFieldBearingLocaliser {
public:
    explicit FarFieldBearingLocaliser(FarFieldBearingConfig config);

    [[nodiscard]] const FarFieldBearingConfig& config() const noexcept;
    [[nodiscard]] FarFieldBearingResult estimate(
        const std::vector<ChannelPairDelay>& delays,
        const std::vector<std::size_t>& click_channels,
        std::size_t click_index,
        std::int64_t click_start_sample) const;

private:
    FarFieldBearingConfig config_;
};

} // namespace pamguard::localisation
