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

## PAMGuard sign equivalence

The existing correlation fixture already pins the Java sign for the same construction: `CorrelationDelayFixtureExporter` drives PAMGuard's real `Correlations.getDelay(channel0, channel1, ...)` with channel 1 arriving five samples later, and the fixture records `+5.003` — positive delay means the second channel is late, and the C++ estimator matches that fixture exactly. Chaining the two results: PAMGuard-positive delays and engine-positive delays mean the same physical lag, and both produce pair bearing `0` toward the first channel under positive spacing.

## Claim boundary

The engine end-to-end convention and the Correlations-level sign equivalence are both pinned. The remaining open convention question is PAMGuard's array-axis handling — `PairBearingLocaliser.prepare()` negates the spacing when the pair vector aligns with the array principal axis (`ArrayManager.getArrayDirections`), which reverses the reported angle's reference direction for some array configurations. Reproducing that flip needs array-shape semantics from `ArrayManager`, tracked as part of array model parity.
