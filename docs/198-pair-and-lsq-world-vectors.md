# Pair and LSQ World Vectors

Date: 2026-07-21

## Purpose

`docs/197` attached world vectors to grid bearings only, and left extending them to pair and LSQ bearings as a follow-up. Doing that turned out to need a decision rather than a repetition: the two localisers report their angles in **different frames**, and applying the same transform to both would be wrong.

## The finding

`MLGridBearingLocaliser2.prepare` rotates every pair vector into the array-axis frame before building its delay table, so its theta and phi are array-frame angles and `getWorldVectors`' rotation is exactly what turns them back into hydrophone coordinates.

`LSQBearingLocaliser.prepare` does **not**. It builds its matrices straight from `getAbsHydrophoneVector` differences with no rotation at all, and derives its angles from the fitted vector:

```java
angs[0][0] = Math.PI/2. - Math.atan2(v.getElement(0), v.getElement(1));
angs[0][1] = Math.asin(v.getElement(2));
```

Feed those angles into `getPlanarVector` and the algebra collapses to the identity:

- `pi/2 - angles[0]` is `atan2(vx, vy)`, so `sin` and `cos` of it give `vx` and `vy` over `sqrt(vx² + vy²)`.
- `sin(asin(vz))` is `vz`, and `cos(asin(vz))` is `sqrt(1 - vz²)`, which for a unit vector is exactly that same `sqrt(vx² + vy²)`.

So `getPlanarVector` reconstructs the LSQ fit vector precisely — and it is already in the hydrophone frame. Rotating it again by the inverse array-axis matrix, as `getWorldVectors` does, would **double-transform** it.

### Why PAMGuard is not visibly wrong here

`AbstractLocalisation.getWorldVectors` is the base-class path for every localisation, LSQ ones included. That looks like an inconsistency until you check which array shapes LSQ actually runs on: it needs four or more non-coplanar hydrophones, so in practice a **volume** sub-array — and `getArrayDirections` returns the plain Cartesian axes for a volume array. The coordinate matrix is the identity, its inverse is the identity, and the rotation does nothing.

The two treatments agree on the shape LSQ is used on. They would diverge on a plane, where the axes are two in-plane vectors.

## Implementation

Two different calls, named for what they do:

- **Pair bearings** use `world_vectors` with the pair's own principal axis. `PairBearingLocaliser` measures its cone angle against `arrayAxis[0]` (and the ported spacing sign flip from `docs/163` aligns it), so a pair is a line sub-array and takes the line branch — two vectors, both cones, carrying the left/right ambiguity explicitly.
- **LSQ bearings** use a new `planar_unit_vector`, which is `getPlanarVector`'s two-angle case with **no** rotation applied. One vector.

Keeping them as separate functions rather than one with a flag is deliberate: the difference is a frame, and a boolean argument at a call site would not say which frame the caller is in.

## API output

Schema v19 adds:

- `pairBearingWorldVectors` on each click localisation and whistle delay entry that carries a pair bearing — always two entries, both with `cone: true`.
- `worldVectors` on the `lsqBearing` object — always one entry, `cone: false`.

A consumer can now read a direction vector from whichever localiser produced output, in one consistent frame, without reconstructing anyone's reference axis.

## Validation

`world_vector_parity` gains two properties over five fitted vectors (including the degenerate along-axis cases):

1. **Round trip.** Deriving azimuth and elevation the way `LSQBearingLocaliser` does, then feeding them to `planar_unit_vector`, returns the original unit vector to `1e-12`. This is the property that justifies skipping the rotation.
2. **Volume identity.** Running the *same* angles through the full `world_vectors` path with a volume sub-array's axes gives the same answer to `1e-12` — demonstrating the claim above rather than asserting it, so if `array_directions` ever stopped returning Cartesian axes for a volume array this test would catch it.

The HTTP smoke asserts the two-hydrophone session's pair bearing carries exactly two world vectors and that both are flagged as cones.

Full CTest suite passes `70/70`.

## Claim boundary

Everything in `docs/197`'s claim boundary applies: these are array-frame vectors, not earth-frame, and reaching a compass direction needs the GPS and attitude feeds the engine has no source for.

The LSQ vector set holds **one** entry, not two. A plane sub-array's mirror ambiguity is real and the grid localiser expresses it, but LSQ returns a single fitted direction and inventing its mirror here would be the engine's construction, not the reference's.

`planar_unit_vector` is only correct for angles already in the hydrophone frame. It is not a general-purpose angle-to-vector helper, and the header says so.
