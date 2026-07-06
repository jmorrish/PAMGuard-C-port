# LSQ Bearing Service Output

Date: 2026-07-03

## Purpose

`docs/158` ported PAMGuard's `LSQBearingLocaliser` with fixture parity; this slice wires it into the click localisation pipeline so sessions with enough geometry receive PAMGuard LSQ bearing output per click.

## Behaviour

When a click covers four or more channels, all delay pairs are geometry-constrained with positive spacing, the sample rate is known, and `array.spacingErrorM > 0`, the session runs the ported LSQ localiser over the click's pairwise delays and attaches an `lsqBearing` object to the click localisation:

- `azimuthRadians` / `azimuthDegrees` — PAMGuard heading-style azimuth (`pi/2 - atan2(x, y)`).
- `elevationRadians` / `elevationDegrees` — `asin(z)`.
- `azimuthErrorRadians` / `elevationErrorRadians` — PAMGuard curvature error estimates, omitted when non-finite.
- `usedPairs` — number of delay pairs in the solve.

The object is omitted entirely when preconditions fail or the geometry is rank deficient (collinear/coplanar hydrophone sets — where PAMGuard's Jama solve throws).

## Why `spacingErrorM > 0` is required

PAMGuard's LSQ fit weights are `(pair length / separation error component)^2`; a zero separation error makes the weights infinite. PAMGuard array managers always supply nonzero separation errors, so the port requires the config to do the same rather than inventing a fallback weighting. Each pair's separation error vector is taken as `spacingErrorM` along the pair baseline direction.

## Schema

Bumps the engine result `schemaVersion` to `6` (purely additive after `5`).

## Validation

- New `session_lsq_bearing_wiring` CTest: a four-channel tetrahedron session produces a valid `lsqBearing` over six pairs alongside per-pair bearings; `spacingErrorM = 0` suppresses it; two-channel sessions stay pair-bearing-only.
- `service-smoke.ps1` asserts schema version 6 everywhere and asserts the two-channel smoke session has no `lsqBearing`.
- Full CTest suite passes `51/51` including all service smokes.

## Claim boundary

Bearing localiser *selection* is a simple channel-count rule here (pair bearing per pair always; LSQ at four-plus channels), not PAMGuard's `BearingLocaliserSelector` with array-shape detection. Whether engine correlation delay signs match PAMGuard's delay measurement conventions for the same physical scene remains to be pinned with a controlled end-to-end fixture. Train-level aggregation of pair/LSQ bearings is a follow-up.
