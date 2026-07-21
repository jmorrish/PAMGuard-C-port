#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "pamguard/localisation/ArrayShape.h"

namespace pamguard::localisation {

/**
 * Which bearing localiser PAMGuard's BearingLocaliserSelector picks for a
 * sub-array of a given shape.
 */
enum class BearingLocaliserChoice {
    /** No bearing is available: the sub-array is empty or a single point. */
    None,
    /** PairBearingLocaliser — a line array. */
    Pair,
    /**
     * MLGridBearingLocaliser2 — a plane or volume array. That localiser is
     * unported; see `bearing_localiser_name` for what the engine runs
     * instead.
     */
    Grid,
};

/**
 * Port of PAMGuard BearingLocaliserSelector.createBearingLocaliser's switch:
 * none/point get no localiser, a line gets the pair localiser, and a plane or
 * volume gets the ML grid localiser.
 *
 * PAMGuard has one extra branch this does not reproduce: a line array of more
 * than two hydrophones uses MLLineBearingLocaliser2 **when SMRUEnable is set**,
 * which gates SMRU-licensed extras that are not part of the open distribution.
 * The default build takes the pair branch, and so does this.
 */
[[nodiscard]] BearingLocaliserChoice select_bearing_localiser(ArrayShapeType shape);

/**
 * Convenience: the shape of a sub-array followed by the selector, which is how
 * `createBearingLocaliser` is called — on the hydrophone subset taking part in
 * one localisation, not on the whole array.
 */
[[nodiscard]] BearingLocaliserChoice select_bearing_localiser(
    const std::vector<std::array<double, 3>>& hydrophone_positions_m,
    const std::vector<int>& streamer_ids = {});

/** Stable identifier for API output: "none", "pair", or "grid". */
[[nodiscard]] std::string_view bearing_localiser_name(BearingLocaliserChoice choice);

/** Stable identifier for API output: "none", "point", "line", "plane", or "volume". */
[[nodiscard]] std::string_view array_shape_name(ArrayShapeType shape);

} // namespace pamguard::localisation
