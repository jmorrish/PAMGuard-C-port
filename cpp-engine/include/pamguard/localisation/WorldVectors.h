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

/**
 * PAMGuard's getPlanarVector for a two-angle set: the unit vector an azimuth
 * and elevation point at, **in the frame those angles were measured in**.
 *
 * No array-axis rotation is applied, which is what makes this the right
 * treatment for `LSQBearingLocaliser` output. That localiser fits raw
 * inter-hydrophone vectors — unlike `MLGridBearingLocaliser2`, which rotates
 * every pair vector into the array-axis frame first — so its angles are
 * already in the hydrophone frame and this reconstructs the fitted vector
 * exactly. Rotating them again, as `getWorldVectors` would, double-transforms.
 *
 * For a **volume** sub-array the two agree anyway, because a volume array's
 * principal axes are the Cartesian axes and the rotation is the identity. They
 * diverge for a plane.
 */
[[nodiscard]] std::array<double, 3> planar_unit_vector(double azimuth_radians, double elevation_radians);

/**
 * Port of PAMGuard AbstractLocalisation.getRealWorldVectors: the array-frame
 * vectors rotated into an **earth frame** by the array's orientation.
 *
 * The reference takes that orientation from `GpsData.getQuaternion()`, which
 * is `new PamQuaternion(toRadians(heading), toRadians(pitch), toRadians(roll))`
 * — the same construction streamer orientation uses (`docs/193`), so the same
 * clockwise-heading and pitch-roll-heading conventions apply.
 *
 * Faithful detail: where the reference finds no origin position it returns the
 * **unrotated** vectors, so an undeclared orientation leaves them in the array
 * frame rather than failing. Pass `orientation_declared = false` for that.
 *
 * A line sub-array's vectors are forced to cones here, as the reference does
 * after rotating.
 */
[[nodiscard]] std::vector<WorldVector> real_world_vectors(ArrayShapeType shape,
                                                          const std::vector<WorldVector>& array_frame_vectors,
                                                          bool orientation_declared,
                                                          double heading_radians,
                                                          double pitch_radians,
                                                          double roll_radians);

} // namespace pamguard::localisation
