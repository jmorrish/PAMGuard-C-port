# Click Detector Display

Date: 2026-07-23

## Purpose

Adds PAMGuard's most-used display family to the web UI: the **Click detector** tab with bearing-time, amplitude-time, and inter-click-interval scatters, plus a waveform and **Wigner-Ville** panel for a selected click. The engine served all of this data already (`docs/154`+ click chain, localisation bearings, waveforms on request); none of it was displayed.

## What was built

- **Data plumbing**: `ffmpeg_stream_ingest` gains `--click-waveforms` (appends `includeClickWaveforms=true` to its PCM POSTs) and captures spawn with it, so the live stream carries click waveforms to the browser. Waveforms only travel when clicks occur; archives are unaffected (they build their own body).
- **Click store** (client-side): clicks accumulate from every live result (unthrottled — clicks are sparse) and from one-shot sends, deduplicated by start sample, reset automatically when a session restarts from zero; capped at 2000. Bearings join by `clickIndex` from `clickLocalisations` (first pair bearing, else LSQ); ICI is the start-sample difference of consecutive clicks.
- **Scatters** (selectable time window 30 s–10 min): bearing-time (0–180°, with an honest "needs ≥2 channels with localisation" state), signal-excess-time (auto-ranged; labelled signal excess, which is what the engine's click carries — not calibrated amplitude), ICI-time (log scale 1 ms–3 s). Echo-flagged clicks draw grey. Clicking a point selects it (nearest within 14 px).
- **Selected click panel**: waveform trace plus the **Wigner-Ville distribution** — analytic signal via FFT, lag kernel `z(n+τ)z*(n−τ)`, FFT over lag — of a 128-sample window centred on the click's peak, drawn 0..fs/2 with square-root contrast.

## Validation

- **WVD correctness test** (node, extracting the page's own functions): a pure tone at fs/4 must sit exactly at the axis midpoint and a linear chirp's ridge must rise monotonically. The first implementation FAILED this test — it kept N/2 lag-FFT bins, but the WVD lag product oscillates at **2f**, so the axis covered only 0..fs/4 (the tone pinned to the top, chirps aliased downward). Fixed to keep all N bins (bin k = k·fs/2N); after the fix the tone ridge is dead flat at bin 64/128 and the chirp ridge rises 49→77 with zero drops.
- **End-to-end**: an impulse train served as a local HTTP stream through the capture path produced clicks **with 150-sample waveforms** in the result feed (2 in the 0.8 s feed window at 0.4 s spacing — exactly right for the feed depth; the UI's continuous stream sees every click).
- Script parses; all 134 referenced ids exist; no duplicate ids; suite `82/82` (one environmental timeout from leftover test ffmpeg processes, clean on re-run).

## Claim boundary

Signal excess dB is displayed, not calibrated amplitude — the engine's click results carry trigger SNR; wiring `rawAmplitude2dB` through click results would be an engine change (recorded as possible future work). Bearing uses the first available pair bearing (or LSQ) per click, not a display-side choice of localiser. The Wigner panel shows the raw WVD (with analytic signal suppressing half the cross-terms); PAMGuard offers smoothed variants (SPWVD) — not implemented. The store is per-page-load, client-side only; the archive remains the durable record. No automated browser test — validated by construction, extracted-function tests, and the data-flow E2E.
