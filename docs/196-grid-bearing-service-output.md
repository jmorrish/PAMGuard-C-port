# Grid Bearing Service Output

Date: 2026-07-21

## Purpose

Serves the ML grid localiser ported in `docs/195`. Until now, a plane or volume sub-array reported `bearingLocaliser: "grid"` — naming the localiser PAMGuard selects — while the engine actually ran LSQ. The localiser PAMGuard selects now runs, and its result is served.

## What changed

Click localisations and whistle delay entries gain a `gridBearing` object at schema v17, present when the sub-array's shape selects the grid localiser and the solve succeeds. Session config gains `array.hydrophones[].xErrorM`, `yErrorM`, and `zErrorM` — PAMGuard's `Hydrophone.getCoordinateErrors`, which the grid table uses to weight each bin. All three default to zero and are rejected if negative or non-finite.

Zero position errors do not disable the localiser: the delay uncertainty also carries a speed-of-sound term and a timing term, so an array with only `timingErrorSeconds` set still gets a grid bearing. It is only when **all three** error sources are zero that the localiser declines, which `docs/195` covers.

## Angles, and why they are not azimuth and elevation

`gridBearing` reports `thetaRadians`/`thetaDegrees` and `phiRadians`/`phiDegrees`, not azimuth and elevation. These are the reference's own angles, measured in the sub-array's **principal axis frame** — `MLGridBearingLocaliser2` rotates every pair vector into the frame the array's own axes define before building its table, and the returned theta is `pi/2` minus the grid angle in that frame.

Converting those to a compass azimuth needs the array's orientation relative to the world, which for a real deployment means heading from a sensor feed the engine does not have. Reporting them under PAMGuard's names is the honest option; presenting them as azimuth and elevation would imply a world frame that has not been established. The neighbouring `lsqBearing` object does report azimuth and elevation, because `LSQBearingLocaliser` solves directly in the hydrophone coordinate frame.

`hasPhi` is false for a line sub-array, matching the reference's single-angle return, though a line sub-array does not select this localiser in the first place.

## Both objects, deliberately

`lsqBearing` is unchanged and still emitted when its own preconditions hold. Nothing disappears from existing responses, and a consumer that has been reading `lsqBearing` keeps working.

For parity with PAMGuard, `gridBearing` is the one to read: it is the localiser PAMGuard selects, and `bearingLocaliser` says so. `lsqBearing` remains available as a second opinion in a different frame, and because removing a field consumers may depend on needs a stronger reason than tidiness.

The region-level `bearing` object on whistle delays is also unchanged — it still reports the LSQ azimuth when available. Repointing it at the grid result would silently change values under an existing field name, which is exactly the kind of change a schema version cannot warn a consumer about.

## Validation

`session_lsq_bearing_wiring` gains three cases: a volume sub-array with declared position errors produces a grid bearing over all six pairs with finite angles and `hasPhi` set; the same array with **no** position errors still produces one, pinning that the wiring does not silently require them; and a two-channel line sub-array produces none, since it selects the pair localiser.

The HTTP smoke asserts the two-hydrophone smoke session emits no `gridBearing`, alongside its existing `arrayShape`/`bearingLocaliser` assertions.

Full CTest suite passes `69/69`.

## Claim boundary

Everything in `docs/195`'s claim boundary still applies — streamer-level separation errors are not modelled, and the reference's dead crawl/simplex/bisection paths are unported.

Position errors are static per session, like the geometry they describe. A real towed array's position uncertainty grows with the tow and would come from the same sensor feed the engine lacks.

The two localisers are not cross-checked against each other. They solve in different frames and, for a plane array, over different hemispheres, so a disagreement between `gridBearing` and `lsqBearing` is expected rather than a fault signal, and neither is validated against the other.
