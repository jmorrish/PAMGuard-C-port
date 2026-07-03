#include <cmath>
#include <iostream>
#include <stdexcept>

#include "pamguard/localisation/FarFieldBearingLocaliser.h"

namespace {

bool close(double a, double b, double tolerance = 1e-6) {
    return std::abs(a - b) < tolerance;
}

pamguard::localisation::ChannelPairDelay delay(std::size_t pair_index, std::size_t channel_a, std::size_t channel_b, double delay_samples) {
    pamguard::localisation::ChannelPairDelay result;
    result.pair_index = pair_index;
    result.channel_a = channel_a;
    result.channel_b = channel_b;
    result.audio_channel_a = channel_a;
    result.audio_channel_b = channel_b;
    result.delay.delay_samples = delay_samples;
    result.delay.delay_score = 1.0;
    return result;
}

} // namespace

int main() {
    pamguard::localisation::FarFieldBearingConfig config;
    config.sample_rate_hz = 48000.0;
    config.speed_of_sound_mps = 1500.0;
    config.hydrophones = {
        {0, 0.0, 0.0, 0.0},
        {1, 1.0, 0.0, 0.0},
        {2, 0.0, 1.0, 0.0},
    };

    const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
    const double delay_samples = (inv_sqrt2 / config.speed_of_sound_mps) * config.sample_rate_hz;
    std::vector<pamguard::localisation::ChannelPairDelay> delays = {
        delay(0, 0, 1, delay_samples),
        delay(1, 0, 2, delay_samples),
        delay(2, 1, 2, 0.0),
    };

    pamguard::localisation::FarFieldBearingLocaliser localiser(config);
    const auto result = localiser.estimate(delays, {0, 1, 2}, 0, 1234);
    if (!result.valid || result.used_pairs != 3 || !close(result.unit_x, inv_sqrt2, 1e-5) ||
        !close(result.unit_y, inv_sqrt2, 1e-5) || !close(result.unit_z, 0.0, 1e-5) ||
        !close(result.azimuth_degrees, 45.0, 1e-4) || !close(result.elevation_degrees, 0.0, 1e-4)) {
        std::cerr << "Far-field bearing check failed\n";
        return 1;
    }

    const auto missing_hydrophone = localiser.estimate(delays, {0, 1, 99}, 1, 4321);
    if (!missing_hydrophone.valid || missing_hydrophone.used_pairs != 1 ||
        !close(missing_hydrophone.unit_x, 1.0, 1e-5)) {
        std::cerr << "Partial-geometry bearing should retain the usable channel pair\n";
        return 1;
    }

    const auto all_missing_hydrophones = localiser.estimate(delays, {99, 98, 97}, 1, 4321);
    if (all_missing_hydrophones.valid || all_missing_hydrophones.used_pairs != 0) {
        std::cerr << "All-missing hydrophone bearing should be invalid with zero usable pairs\n";
        return 1;
    }

    const auto one_pair = localiser.estimate({delays[0]}, {0, 1}, 2, 5678);
    if (!one_pair.valid || one_pair.used_pairs != 1 || !close(one_pair.unit_x, 1.0, 1e-5) ||
        !close(one_pair.unit_y, 0.0, 1e-5) || !close(one_pair.azimuth_degrees, 0.0, 1e-4)) {
        std::cerr << "One-pair bearing foundation check failed\n";
        return 1;
    }

    bool rejected_bad_config = false;
    try {
        auto bad_config = config;
        bad_config.sample_rate_hz = 0.0;
        pamguard::localisation::FarFieldBearingLocaliser bad_localiser(bad_config);
        (void)bad_localiser;
    }
    catch (const std::invalid_argument&) {
        rejected_bad_config = true;
    }
    if (!rejected_bad_config) {
        std::cerr << "Far-field bearing localiser should reject bad sample rate\n";
        return 1;
    }

    std::cout << "Far-field bearing check passed\n";
    return 0;
}
