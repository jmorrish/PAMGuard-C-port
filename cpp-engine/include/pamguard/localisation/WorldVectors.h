#pragma once

#include <array>
#include <vector>

#include "pamguard/localisation/ArrayShape.h"

namespace pamguard::localisation {

struct WorldVector {
    std::array<double, 3> direction{};
    /**
     * PamVector's cone flag: the vector describes a **surface** of possible
     * directions rather than one direction. A line sub-array's bearing is a
     * cone about the array axis, so both of its vectors are marked.
     */
    bool cone = false;
};

/**
 * Port of PAMGuard AbstractLocalisation.getPlanarVector + getWorldVectors:
 * turns a localiser's angles into unit direction vectors expressed in the
 * **hydrophone array's own xyz frame**.
 *
 * This is not an earth frame. PAMGuard's own comment is explicit that getting
 * to a real-world direction needs a further rotation by the course, pitch, and
 * roll of the vessel or array; `getRealWorldVectors` does that with GPS data,
 * which the engine has no source for.
 *
 * How many vectors come back, and what they mean, follows the sub-array shape:
 *
 * - Volume: one vector. The geometry gives an unambiguous direction.
 * - Plane:  two vectors, one either side of the array plane — the mirror
 *           ambiguity a plane cannot resolve.
 * - Line:   two cone vectors, the second with its y component negated — the
 *           left/right ambiguity a line cannot resolve.
 * - None/Point: empty.
 *
 * `angles_radians` is the localiser's own angle set: two angles (theta then
 * phi) for a plane or volume, one for a line. `array_axes` is the sub-array's
 * principal axes from `array_directions`; an empty set falls back to
 * PAMGuard's fixed linear array geometry.
 */
[[nodiscard]] std::vector<WorldVector> world_vectors(ArrayShapeType shape,
                                                     const std::vector<std::array<double, 3>>& array_axes,
                                                     const std::vector<double>& angles_radians);

} // namespace pamguard::localisation
