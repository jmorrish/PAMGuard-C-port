# World Vectors

Date: 2026-07-21

## Purpose

`docs/196` served the grid localiser's theta and phi under those names because they live in the sub-array's principal axis frame, and said converting them to a compass bearing needs an orientation reference the engine does not have. That is true of the *earth* frame — but PAMGuard has a step in between that the engine can reach: expressing the direction in the **hydrophone array's own xyz frame**, which is a frame the session already defines.

This ports that step, so a consumer gets an unambiguous unit vector instead of an angle whose reference axis they have to reconstruct.

## Reference semantics ported

`AbstractLocalisation.getWorldVectors` takes the localiser's angles, turns them into a unit vector via `getPlanarVector`, and rotates that vector out of the array-axis frame using the inverse of a coordinate matrix built from the array's principal axes.

Its own comment is careful about the name: *"Real world in this instance means relative to the xyz coordinate frame of the hydrophone array. To get real real world vectors relative to the planet, the vectors will need to be further rotated by the course of the vessel/array and the pitch, roll of the hydrophone array."* That further rotation is `getRealWorldVectors`, which needs GPS, and is not ported.

How many vectors come back is the interesting part, because it is where PAMGuard encodes what each array shape cannot resolve:

| Shape | Vectors | Meaning |
| --- | --- | --- |
| Volume | 1 | Unambiguous direction. |
| Plane | 2 | The mirror pair, one either side of the array plane. |
| Line | 2 | The left/right pair, both flagged as **cones** about the array axis. |

The plane's pair comes from building the coordinate matrix twice with the third axis flipped. The line's pair comes from negating the pointer's y component in the array frame and rotating it through the *same* matrix — so the second vector is not simply the first with its y sign flipped, which the `line-diagonal` fixture case demonstrates.

### Details worth stating

- **Only the line branch marks its vectors as cones.** A plane or volume vector is never flagged, even when the localiser supplied a single angle and the planar vector itself was a cone: the reference constructs fresh vectors on those branches and leaves the flag at its default. The port matches rather than propagating the flag.
- **With no array axes at all**, the reference falls back to a fixed frame `{{0,1,0},{1,0,0},{0,0,1}}` — y as the principal axis, the usual orientation for a forward-towed array. Two fixture cases exercise it.
- **A single axis** gets a frame completed from it: the second row is perpendicular in the horizontal plane, unless the array is perfectly vertical, in which case it falls back to the x axis.
- **`Matrix.times` here is matrix-times-column**, unlike `PamVector.rotate(Matrix)` in `MLGridBearingLocaliser2`, which makes the vector a single row and multiplies from the left. The two appear a few lines apart in related code and transpose each other; the shared helper names them separately (`matrix_times_column`, `row_times_matrix`) so the distinction cannot be lost.

## Implementation

`localisation::world_vectors` takes the shape, the array axes, and the localiser's angles. `AnalysisSession` calls it for every grid bearing and stores the result alongside theta and phi.

Jama's LU inverse moved out of `MlGridBearingLocaliser.cpp` into a shared `JamaMatrix.h`/`.cpp`, since both ports need it; the ML grid parity fixture still passes unchanged, which is the check that the move was clean.

## API output

Schema v18 adds `worldVectors` to the `gridBearing` object: an array of `{x, y, z, cone}` entries in the array's xyz frame. One entry for a volume sub-array, two for a plane or line.

The `cone` flag is worth reading rather than ignoring. For a line sub-array it says the vector is a *surface* of possible directions, not one direction — plotting it as an arrow would misrepresent what the array measured.

## Validation

`world_vector_parity` (new) replays `tests/fixtures/localisation/world-vector.csv` from `WorldVectorFixtureExporter`, which transcribes the four `AbstractLocalisation` methods — that class hangs off a `PamDataUnit` and reaches GPS — while the vector maths is the real `PamVector` and the matrix inverse and multiply are the real `Jama.Matrix`. Array axes come by reflection from `ArrayShapeFixtureExporter`, as `docs/195` established.

Twelve cases: plane arrays at zero angles, a quarter turn, elevated, and with negative elevation; volume arrays at zero and oblique angles; line arrays along y, broadside, perfectly vertical, and diagonal; and both a line and a plane with the axes dropped, exercising the fixed-geometry fallback. Every component matches **exactly** — `max_abs_error` is 0.

Three guards are pinned: a point sub-array, an unknown shape, and an empty angle set all produce no vectors.

`session_lsq_bearing_wiring` additionally asserts a volume sub-array's served vector set holds exactly one non-cone entry and that it is a unit vector.

Full CTest suite passes `70/70`.

## Claim boundary

**Array frame, not earth frame.** These vectors are relative to the hydrophone coordinate system the session declares. Getting to a compass direction needs `getRealWorldVectors`' rotation by vessel course, pitch, and roll, which needs GPS and attitude feeds the engine has no source for. The distinction is PAMGuard's own and is preserved deliberately: the field is named for what the reference calls it, and this document is where the qualification lives.

The vectors are attached to `gridBearing` only in this slice. `docs/198-pair-and-lsq-world-vectors.md` extends them to pair and LSQ bearings, and explains why LSQ needs a *different* treatment rather than the same one — its angles are already in the hydrophone frame, so rotating them again would double-transform.

Streamer orientation (`docs/193`) is baked into the hydrophone positions before the axes are derived, so the frame follows the oriented array. It is still a static frame; a towed array that changes attitude through a tow is not modelled.
