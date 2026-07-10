# MHT Train Service Output

Date: 2026-07-10

## Purpose

`docs/166`-`docs/169` ported the full MHT click train stack with end-to-end fixture parity. This slice wires it into the session and service as a selectable alternative to the max-ICI click train tracker, delivering PAMGuard-style multi-hypothesis train formation through the API.

## Behaviour

- `click.train.algorithm` session config selects the train former: `"ici"` (default, unchanged behaviour) or `"mht"`. Invalid values are rejected at session creation; the config echo reports `trainAlgorithm`.
- In MHT mode the session runs one ported MHT kernel (with `StandardMhtChi2` and the IDI/amplitude/length variables at PAMGuard defaults) per click channel-bitmap group. Each detected click feeds the kernel as time (from the start sample), an uncalibrated peak level in dB (the amplitude chi2 scores differences only, so calibration offsets cancel), and the click duration in milliseconds.
- Confirmed tracks with at least `click.train.minClicks` clicks are served as `mhtClickTrains` entries: `trainId`, `channelBitmap`, `chi2` (StandardMHTChi2 semantics, lower is better), `clickCount`, first/last start samples, and the member click start samples/times. `flush` confirms all remaining hypotheses.
- In MHT mode `clickTrains` stays empty (and vice versa); archived detector events gain the `mht-click-track` type.

## Schema

Bumps the engine result `schemaVersion` to `10` (purely additive).

## Validation

- `session_mht_train_wiring`: eight synthetic clicks 100 ms apart produce a confirmed MHT train containing at least six of them after flush, with the ICI tracker silent; ICI mode still produces `clickTrains` with no MHT output.
- Service smoke asserts schema version 10 on health, live results, and archived records.
- Full CTest suite passes `60/60`.

## Claim boundary

MHT parameters (kernel and chi2) are PAMGuard defaults, not yet configurable per session. Kernel memory grows with session length (`clearKernelGarbage`/`MHTGarbageBot` unported) — long live streams should prefer the ICI tracker until reclamation lands. Bearing/correlation/time-delay/peak-frequency variables, the electrical noise filter, and click train classification remain unported.
