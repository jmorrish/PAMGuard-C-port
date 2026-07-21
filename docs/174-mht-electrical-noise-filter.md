# MHT Electrical Noise Filter

Date: 2026-07-10

## Purpose

Ports PAMGuard's `SimpleElectricalNoiseFilter`, the MHT stack's guard against tracking electrical interference. Electrical noise produces detections that are *too* regular — near-perfect ICI, amplitude, and duration — which the chi2 machinery would otherwise score as an outstanding click train.

## Reference semantics ported

For a track with at least `nDataUnits` detections (default 30), each enabled chi2 variable's error-independent value is `rawChi2 * error^2 / bitcount`. If any falls below `minChi2` (default 1e-5), the track is flagged `JUNK_TRACK`. Two faithful details:

- The chi2 value itself is returned **unchanged** — the filter only sets the flag.
- The kernel confirms junk-flagged tracks out of the possibility mix immediately (the same branch that handles coast exhaustion), so the noise track is removed rather than left competing.

The C++ variables gained `raw_chi2()`/`error()` accessors (raw meaning the accumulated sum, not divided by bitcount, matching `MHTChi2Var.getChi2`), `MhtChi2` gained an `is_junk_track()` hook, and the kernel sets the track flag from it after each update.

## Fixture

The existing `standard-mht-chi2` fixture gained two appended cases (existing rows byte-identical): 34 perfectly uniform detections with the filter off and on. The contrast is stark and pins the exact trigger point:

- Filter **off**: the uniform track survives to the end, confirming a single 34-click train at chi2 0.
- Filter **on**: at detection **30** — exactly `nDataUnits` — the track is flagged and confirmed; the possibility count collapses from 32 to 2 in one step, and the confirmed 29-click track is followed by short fragments of the tail.

`standard_mht_chi2_stack_parity` reproduces both step-for-step.

## Config

`click.train.mht` gains `useElectricalNoiseFilter` (default false, as in PAMGuard), `electricalNoiseMinChi2`, and `electricalNoiseNDataUnits`, documented in OpenAPI and echoed in the session config. No result schema change.

## Claim boundary

Only `SimpleElectricalNoiseFilter` is ported; the `ElectricalNoiseFilter` interface has no other implementation in the reference. `CorrelationChi2`/`TimeDelayChi2Delta` and click train classification remain unported.
