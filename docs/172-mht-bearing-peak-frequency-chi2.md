# MHT Bearing And Peak Frequency Chi2 Variables

Date: 2026-07-10

## Purpose

Continues the MHT chi2 variable set (`docs/166`, `docs/167`) with `BearingChi2Delta` and `PeakFrequencyChi2`, the two remaining variables whose inputs the engine already produces with fixture-pinned parity (bearings from the ported localisers, peak frequency from the click feature extractor).

## Reference semantics ported

`MhtBearingChi2Delta` (SimpleChi2VarDelta path):

- Per-pair difference is `BearingChi2Delta.getDifference`: the wraparound-aware minimum arc between two bearings, which is always non-negative and never exceeds pi. Crossing zero degrees therefore yields a small difference, not a large one.
- The chi2 scores the change between successive differences over `max(timeDiff * error, minError)^2` (defaults 4 and 2 degrees), so a constant bearing sweep scores zero.
- The jump penalty, when enabled, keys off the **current** absolute difference (`calcDeltaChi2` reads the `lastDiff` field set by the most recent `getDiffValue`), matching the amplitude variable's structure. Faithful quirk: because the difference is non-negative, the `NEGATIVE` jump direction negates it and can never fire; `POSITIVE` (the default) and `BOTH` behave identically.

`MhtPeakFrequencyChi2` (SimpleChi2Var base path): signed peak-frequency differences in Hz scored against the time-scaled static error (defaults 30 Hz, minimum cut 1).

## Fixture

`MhtBearingChi2FixtureExporter.java` drives the **real** `BearingChi2Delta` with data units carrying a minimal `AbstractLocalisation` subclass. Five cases: constant bearing (zero), linear sweep (`2.2e-30` — the delta-path zero-ramp property in floating point), irregular bearings, a wraparound case crossing zero degrees, and a jump-penalty case. `mht_bearing_chi2_parity` matches within `8.5e-15` relative, and also asserts the difference function is symmetric and bounded by pi.

## Claim boundary

`PeakFrequencyChi2` has **no dedicated fixture**: its combination maths is the `SimpleChi2Var` base path already pinned by the length variable (`docs/167`), and only its constants and diff source differ. PAMGuard derives peak frequency inside `getPeakFrequency` from the data unit's raw-data transforms and parent data block, which the port supplies from its own fixture-pinned feature extraction instead. Neither variable is enabled in the served MHT stack yet (`StandardMhtChi2` currently combines IDI, amplitude, and length); wiring them in requires per-click bearing/peak-frequency plumbing into the MHT units. `CorrelationChi2` and `TimeDelayChi2Delta` remain unported.
