# MHT Length And Amplitude Chi2 Variables

Date: 2026-07-10

## Purpose

Continues MHT click train porting from the IDI chi2 foundation (`docs/166`). This slice ports the two next chi2 variables: `LengthChi2` (the `SimpleChi2Var` base path over click durations) and `AmplitudeChi2` (the `SimpleChi2VarDelta` path over absolute amplitude differences, with the amplitude jump penalty).

## Reference semantics ported

`cpp-engine/src/detectors/MhtSimpleChi2Vars.cpp`:

- `MhtLengthChi2`: per-pair contributions `(duration0 - duration1)^2 / max(minError, idi * error)^2` (signed millisecond difference; defaults error 0.2, minError 0.002), batch-averaged over `unitCount - 1`; the streaming update accumulates from the second usable unit, unlike the IDI variable which records one IDI first.
- `MhtAmplitudeChi2`: the delta state machine scores the change between successive absolute amplitude differences over `max(timeDiff * error, minError)^2` (defaults error 30 dB, minError 1) — a perfectly linear amplitude ramp scores exactly zero — and adds `JUNK_TRACK_PENALTY` when the current absolute difference exceeds `maxAmpJump` (10 dB, enabled by default). The batch `calcChi2` keeps the base per-pair form, exactly as the Java class inherits it (a PAMGuard asymmetry worth knowing).
- Both share the `IDIManager.calcTime` replica (`mht_calc_time_seconds`).

## Fixture

`MhtSimpleChi2VarsFixtureExporter.java` drives the real `LengthChi2` and `AmplitudeChi2` classes with data units overriding time, `getAmplitudeDB`, and `getDurationInMilliseconds`. Eight cases: steady/wild length batch and update paths, steady/ramp/jump amplitude updates (ramp pins the zero-score delta property; jump pins the penalty — observed `6666673.83` = accumulated penalty over bitcount), and a steady amplitude batch pinning the inherited base-path asymmetry.

`mht_simple_chi2_vars_parity` compares within `1e-9` relative.

## Claim boundary

Bearing, correlation, and time-delay chi2 variables, the MHT kernel/TrackBitSet machinery, and `StandardMHTChi2` combination remain unported.
