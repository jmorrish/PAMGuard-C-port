# Pair Bearing Localiser Port

Date: 2026-07-03

## Purpose

The parity ledger called for pinning down PAMGuard bearing localiser semantics for common array geometries. The C++ far-field foundation solves a least-squares unit-vector problem, but PAMGuard's reference for two-element closely spaced arrays is `Localiser.algorithms.timeDelayLocalisers.bearingLoc.PairBearingLocaliser`, which computes a cone angle from the pair axis with a specific error-propagation formula. That semantics did not exist in the C++ engine at all. This slice ports it and pins it with a Java-generated fixture.

## Reference semantics ported

`PairBearingLocaliser.localise()`:

```text
ct    = clamp(speedOfSound * delay / spacing, -1, 1)
angle = acos(ct)
e1    = speedOfSound * timingError
e2    = speedOfSound * delay / spacing * spacingError
e3    = delay * speedOfSoundError
error = sqrt((e1^2 + e2^2 + e3^2) / spacing / sin(angle) + wobbleRadians)
```

Faithfully preserved quirks:

- At clamped endfire (`|ct| >= 1`), `sin(angle) = 0` makes the error `Infinity`.
- With negative spacing (PAMGuard flips the spacing sign when the pair vector aligns with the principal array axis), the error term is divided by a negative spacing before the wobble is added, giving a smaller error than the positive-spacing twin.
- A three-delay input uses only the middle delay (PAMGuard's own reduction, commented in the source as a bodge for Vancouver data).
- Empty delays produce no result.

## Implementation

- New `cpp-engine/include/pamguard/localisation/PairBearingLocaliser.h` and `src/localisation/PairBearingLocaliser.cpp`, expression order matching the Java source for floating-point fidelity. Constructor validation (non-zero spacing, positive sound speed) follows the engine's convention; negative spacing is allowed by design.
- `PairBearingFixtureExporter.java` transcribes `localise()` verbatim — the real class cannot be driven headless because `prepare()` pulls the `ArrayManager`/`PamController` singletons — and exports a seven-case catalogue: broadside, positive/negative mid-cone, near-endfire, endfire clamp, negative spacing, and the three-delay reduction.
- `pair_bearing_fixture_check.cpp` mirrors the case catalogue by name, enforces case count/order, checks zero-spacing rejection and empty-delay behaviour, and compares angle/error within `1e-12` (with exact matching for infinities).

## Validation

- `pair_bearing_basic_parity` passes across all seven cases.
- Full CTest suite passes `49/49` on Windows Release.

## Claim boundary

This ports the pair (two-element) bearing localiser maths. `prepare()`-side behaviour — array-shape detection, spacing/wobble derivation from the hydrophone array, and the principal-axis spacing sign flip — is supplied as configuration here, with the sign-flip semantics documented and exercised via the negative-spacing case. PAMGuard's other bearing localisers (`LSQBearingLocaliser`, `MLGridBearingLocaliser`, simplex) remain unported; the service does not yet expose pair-bearing outputs.
