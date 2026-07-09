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
 * positions (single streamer). Faithful to the reference, including the
 * last-duplicate-wins unique filter, the 1/1000-radian parallel tolerance,
 * and getMaxVolume taking the maximum of signed triple products.
 */
[[nodiscard]] ArrayShapeType array_shape(const std::vector<std::array<double, 3>>& hydrophone_positions_m);

/**
 * Port of PAMGuard ArrayManager.getArrayDirections: principal array axis
 * vectors (empty for none/point; one vector for a line array aligned to the
 * nearest positive Cartesian axis; two for a plane; the Cartesian axes for a
 * volumetric array).
 */
[[nodiscard]] std::vector<std::array<double, 3>> array_directions(
    const std::vector<std::array<double, 3>>& hydrophone_positions_m);

} // namespace pamguard::localisation
