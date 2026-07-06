#include <cmath>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <vector>

#include "pamguard/localisation/DelayGroupEstimator.h"
#include "pamguard/localisation/PairBearingLocaliser.h"

namespace {

constexpr double sample_rate_hz = 48000.0;
constexpr double speed_of_sound_mps = 1500.0;
constexpr double spacing_m = 3.0;

// One waveform pair: an identical short click on both channels, with
// channel 1 shifted by shift_samples relative to channel 0. Positive
// shift_samples means channel 1 receives the click LATER than channel 0
// (channel 0 leads, i.e. the source is nearer hydrophone A).
std::vector<std::vector<double>> waveforms_with_shift(int shift_samples) {
    constexpr std::size_t length = 512;
    constexpr int base_position = 250;
    std::vector<std::vector<double>> waveforms(2, std::vector<double>(length, 0.0));
    const double click_shape[] = {0.4, -0.9, 1.0, -0.8, 0.5, -0.2};
    for (int i = 0; i < 6; ++i) {
        waveforms[0][static_cast<std::size_t>(base_position + i)] = click_shape[i];
        waveforms[1][static_cast<std::size_t>(base_position + shift_samples + i)] = click_shape[i];
    }
    return waveforms;
}

double bearing_for_shift(int shift_samples) {
    pamguard::localisation::DelayGroupEstimator estimator;
    const double max_delay = std::ceil(spacing_m / speed_of_sound_mps * sample_rate_hz) + 1.0;
    const auto delays = estimator.estimate_delays(waveforms_with_shift(shift_samples), {max_delay});
    if (delays.size() != 1) {
        throw std::runtime_error("expected exactly one pair delay");
    }

    pamguard::localisation::PairBearingConfig config;
    config.spacing_m = spacing_m;
    config.speed_of_sound_mps = speed_of_sound_mps;
    pamguard::localisation::PairBearingLocaliser localiser(config);
    const auto result = localiser.localise({delays[0].delay.delay_samples / sample_rate_hz});
    if (!result.has_value()) {
        throw std::runtime_error("pair bearing produced no result");
    }
    return result->angle_radians;
}

bool close(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}

} // namespace

int main() {
    try {
        // Endfire shift: exactly the acoustic travel time between the
        // hydrophones (3 m at 1500 m/s = 2 ms = 96 samples at 48 kHz).
        constexpr int endfire_samples = 96;

        const double broadside = bearing_for_shift(0);
        const double channel1_late = bearing_for_shift(endfire_samples);
        const double channel1_early = bearing_for_shift(-endfire_samples);
        const double channel1_half_late = bearing_for_shift(endfire_samples / 2);

        // Convention-free physical invariants.
        if (!close(broadside, std::numbers::pi / 2.0)) {
            std::cerr << "Zero delay should give a broadside (pi/2) pair bearing, got " << broadside << "\n";
            return 1;
        }
        if (!close(channel1_late + channel1_early, std::numbers::pi)) {
            std::cerr << "Opposite endfire bearings should be supplementary, got "
                      << channel1_late << " and " << channel1_early << "\n";
            return 1;
        }
        const bool monotonic = (channel1_late < channel1_half_late && channel1_half_late < broadside) ||
            (channel1_late > channel1_half_late && channel1_half_late > broadside);
        if (!monotonic) {
            std::cerr << "Pair bearing should vary monotonically with the arrival-time difference\n";
            return 1;
        }

        // Engine convention, pinned: with positive spacing, angle 0 means the
        // source lies on the FIRST channel's (hydrophone A's) side — channel 1
        // receiving the click later gives angle 0; channel 1 early gives pi.
        // PAMGuard's own prepare() may flip the spacing sign against the array
        // principal axis, which flips this convention at config level.
        std::cout << "broadside=" << broadside << "\n";
        std::cout << "channel1_late=" << channel1_late << "\n";
        std::cout << "channel1_early=" << channel1_early << "\n";
        std::cout << "channel1_half_late=" << channel1_half_late << "\n";
        if (!close(channel1_late, 0.0) || !close(channel1_early, std::numbers::pi) ||
            !close(channel1_half_late, std::numbers::pi / 3.0)) {
            std::cerr << "Engine pair bearing convention changed: expected 0 / pi / pi/3 for late/early/half-late channel 1\n";
            return 1;
        }

        std::cout << "Pair bearing physical consistency passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
