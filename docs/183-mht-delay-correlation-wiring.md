# MHT Time Delay And Correlation Wiring

Date: 2026-07-10

## Purpose

`docs/176` and `docs/179` ported the time-delay and correlation chi2 variables but left them unreachable from the served MHT stack, because both need per-pair data rather than per-click scalars. This slice threads that data through, so every ported MHT chi2 variable can now run end-to-end.

## What was threaded

- **Time delays.** `MhtChi2Unit` gained `pair_delays_seconds`. The session fills it from the click's localisation delays, in the click's channel-pair order, which is stable across clicks in a session — the ordering the variable requires.
- **Correlation.** `MhtChi2Unit` gained a shared-pointer `waveform` (first channel), populated only when the correlation variable is enabled so the shared pointer costs nothing otherwise. `StandardMhtChi2` keeps the previous **in-track** waveform and correlates on demand against the new click using the engine's fixture-pinned correlation estimator, mirroring how PAMGuard's `CorrelationManager` derives the value lazily for the specific pair rather than for adjacent detections.

A missing waveform scores as perfect correlation (contributing zero) rather than corrupting the track, and the correlation is clamped away from zero so `log(1/corr)` stays finite.

## Config

`click.train.mht` gains `enableTimeDelay` and `enableCorrelation`, both default false and both documented in OpenAPI with their prerequisites. The validator counts them toward the "at least one variable enabled" rule, and the config echo reports them. No result schema change.

## Validation

`session_mht_train_wiring` gains a case running a two-hydrophone localisation-enabled session with **both** variables on, asserting click localisations exist to feed them and that resulting train chi2 values stay finite. Full CTest suite passes `64/64`.

## Claim boundary

Enabling the time-delay variable on a two-hydrophone array contributes exactly nothing — its drop-the-worst-pair rule removes the only term (`docs/176`) — so it is only useful from three hydrophones up; the OpenAPI description says so. The correlation value comes from the engine's estimator rather than PAMGuard's `CorrelationManager`, and PAMGuard's optional FFT pre-filter is still unported. Neither variable's contribution inside the served stack is fixture-compared; the variables themselves are pinned individually.
