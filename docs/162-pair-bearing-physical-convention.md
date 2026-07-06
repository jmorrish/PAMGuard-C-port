# Pair Bearing Physical Convention

Date: 2026-07-03

## Purpose

The parity ledger asked for the engine's delay-sign and bearing conventions to be pinned with a controlled end-to-end check, so that bearing outputs can be interpreted physically without reverse-engineering the correlation estimator. This slice adds `pair_bearing_physical_consistency`, which drives the full chain — synthetic waveform lead/lag, correlation delay estimation, PAMGuard pair bearing — with known geometry.

## Scenario

Two hydrophones 3 m apart at 1500 m/s and 48 kHz (endfire travel time exactly 96 samples). An identical short click is placed on both channels with a controlled shift, and the resulting delay is fed to the ported `PairBearingLocaliser` with positive spacing.

## Invariants checked (convention-free)

- Zero shift gives a broadside bearing of exactly `pi/2`.
- Opposite endfire shifts give supplementary angles (sum `pi`).
- The angle varies monotonically with the arrival-time difference.

## Engine convention pinned

With positive spacing (the engine's session wiring always supplies the pair's hydrophone distance, which is positive):

- Channel 1 (hydrophone B) receiving the click **later** — source on hydrophone A's side — gives angle `0`.
- Channel 1 receiving **earlier** — source on hydrophone B's side — gives angle `pi`.
- A half-endfire lag gives `pi/3`.

So angle `0` points toward the first channel of the pair. PAMGuard's own `PairBearingLocaliser.prepare()` may negate the spacing when the pair vector aligns with the array principal axis, which flips this convention; in the port that flip is a config decision (`spacing_m` sign), and the service wiring uses positive distances.

## Validation

- `pair_bearing_physical_consistency` passes; full CTest suite passes `52/52`.

## Claim boundary

This pins the engine's own end-to-end convention deterministically. Whether PAMGuard's delay-measurement sign for the same physical scene matches (given its own correlation and array-axis conventions) still needs a Java-side end-to-end fixture with a simulated source; until then, cross-implementation bearing comparisons should be made through delay values (already fixture-pinned) rather than assumed sign conventions.
