# MHT Session Parameters

Date: 2026-07-10

## Purpose

Closes two ledger items: the served MHT stack had hardcoded PAMGuard defaults, and the ported bearing/peak-frequency chi2 variables (`docs/172`) were not reachable from the served path. This slice makes the MHT kernel and chi2 parameters per-session configurable and wires both variables in.

## Config surface

`click.train.mht` (honoured only when `click.train.algorithm` is `"mht"`; omitted fields keep PAMGuard defaults):

- Variable enables: `enableIdi`, `enableAmplitude`, `enableLength` (all default true), `enableBearing`, `enablePeakFrequency` (default false).
- Chi2 parameters: `coastPenalty`, `newTrackPenalty`, `newTrackN`, `maxIci`, `lowIciExponent`, `longTrackExponent`.
- Kernel parameters: `nHold`, `nPruneback`, `nPrunebackStart`, `maxCoast`.

Validation rejects disabling every chi2 variable, non-positive `maxIci`, negative penalties, and non-positive `nHold`/`nPruneback`/`maxCoast`. The session config echo reports the resolved values under `click.train.mht`. The older diagnostic alias `click.trainMht` is retained.

## Variable wiring

MHT processing now runs **after** the localisation block so each MHT detection unit can carry the click's peak frequency (from the fixture-pinned click feature extractor, matched by click index) and bearing (from the click bearing localiser, converted from degrees to radians). Both stay off by default because they depend on those optional detector paths being enabled.

## Schema

No result schema change: `mhtClickTrains` keeps its v10 shape. Only the session config input surface and its echo grew, both additively.

## Validation

- `session_mht_train_wiring` gains a case enabling click features and the peak-frequency variable with tightened kernel parameters (`nHold` 10, `maxCoast` 2), asserting features exist per click and trains still form.
- Full CTest suite passes `61/61`.

## Claim boundary

Bearing/peak-frequency contributions are not yet fixture-compared *inside* the served stack (the variables themselves are pinned in `docs/172`, and the stack combination is pinned in `docs/169` over IDI/amplitude/length). `CorrelationChi2` and `TimeDelayChi2Delta` remain unported, so their enables do not exist; the electrical noise filter and click train classification are also still open.
