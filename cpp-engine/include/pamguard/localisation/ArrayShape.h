#pragma once

#include <array>
#include <vector>

namespace pamguard::localisation {

enum class ArrayShapeType {
    None = 0,
    Point = 1,
    Line = 2,
    Plane = 3,
    Volume = 4,
};

/**
 * Port of PAMGuard ArrayManager.getArrayShape over explicit hydrophone
 * positions. Faithful to the reference, including the last-duplicate-wins
 * unique filter, the 1/1000-radian parallel tolerance, and getMaxVolume
 * taking the maximum of signed triple products.
 *
 * `streamer_ids`, when supplied, must have one entry per position and
 * reproduces PAMGuard's streamer-scoped uniqueness: two hydrophones at the
 * same position count as duplicates only when they share a streamer, so
 * co-located phones on different streamers both survive. An empty vector
 * means a single streamer.
 */
[[nodiscard]] ArrayShapeType array_shape(const std::vector<std::array<double, 3>>& hydrophone_positions_m,
                                         const std::vector<int>& streamer_ids = {});

/**
 * Port of PAMGuard ArrayManager.getArrayDirections: principal array axis
 * vectors (empty for none/point; one vector for a line array aligned to the
 * nearest positive Cartesian axis; two for a plane; the Cartesian axes for a
 * volumetric array). `streamer_ids` behaves as for `array_shape`.
 */
[[nodiscard]] std::vector<std::array<double, 3>> array_directions(
    const std::vector<std::array<double, 3>>& hydrophone_positions_m,
    const std::vector<int>& streamer_ids = {});

} // namespace pamguard::localisation
