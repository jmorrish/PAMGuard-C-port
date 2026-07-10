# Standard MHT Chi2 Stack

Date: 2026-07-10

## Purpose

Completes the core MHT click train detection stack: `StandardMHTChi2` (the per-track combiner of chi2 variables with coast counting and track penalties) is ported over the previously ported kernel and IDI/amplitude/length variables, and the **entire stack** is pinned end-to-end against the real Java `MHTKernel` + `StandardMHTChi2` + `StandardMHTChi2Provider`.

## Reference semantics ported

`cpp-engine/src/detectors/StandardMhtChi2.cpp`:

- Raw chi2: sum of enabled variables' streaming updates (IDI, amplitude, length; the bearing/correlation/time-delay/peak-frequency slots are not yet ported and stay disabled).
- Coast counting from the track IDI structure: `floor(timeDiff / |medianIDI|)` for two-plus detection tracks; the single-detection expression over `maxICI` otherwise.
- Sub-two-detection tracks score `maxChi` (2e17) with zero coasts; a fresh instance holds `Double.MAX_VALUE`, which matters for stable-sort tie order in the confirm-all pass (the fresh all-coasts duplicate is silently dropped rather than confirmed, exactly as in Java).
- Penalties: `coastPenalty * nCoasts`; the new-track penalty when the pre-pruneback prefix holds at most `newTrackN` detections; the junk penalty when `medianIDI > maxICI` or the maximum IDI exceeds `(maxCoast+1) * medianIDI`; otherwise the low-ICI nudge (`(medianIDI/maxICI)^lowICIExponent`) and long-track division (`(trackTime/totalTime)^longTrackExponent`) for tracks longer than `newTrackN`.
- The provider replicates `IDIManager`'s master cumulative time series, per-track IDI structure (median on a copy — the Java `PamArrayUtils.median` sorts its input in place, but all downstream uses are order-invariant), last-time and total-time queries.
- Defaults match `StandardMHTChi2Params`/`MHTChi2Params`: coastPenalty 10, newTrackPenalty 50, newTrackN 3, maxICI 0.4, lowICIExponent 0.1, longTrackExponent 0.1. The electrical noise filter is off (unported).

## Fixture

`StandardMhtChi2FixtureExporter.java` drives the real full stack (reflection swaps the provider's private `IDIManager` for the fixed-sample-rate subclass; data units carry time/amplitude/duration). Two scenarios:

- `steady-train-then-gap`: pins gap-forced confirmation with real chi2 values — the four-click train at 2452.48, junk-penalty tracks at 2e7, and maxChi single-detection tracks.
- `two-trains-amplitude-split`: two interleaved trains with distinct amplitudes and lengths are separated **perfectly** (`101010101010` at chi2 0.817 and `010101010101` at 44.76) — the core MHT capability, now reproduced by the port step-for-step.

`standard_mht_chi2_stack_parity` replays every step count and confirmed track (bitset exact, chi2 within 1e-9 relative).

## Claim boundary

Bearing/correlation/time-delay/peak-frequency chi2 variables, the electrical noise filter, `clearKernelGarbage` memory reclamation, and wiring the MHT stack into the session/service as an alternative train former (replacing or complementing the max-ICI tracker) remain. Click-train classification (`CTClassifier` chain) is untouched.
