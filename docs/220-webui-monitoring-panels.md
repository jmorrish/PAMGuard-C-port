# Web-UI Monitoring Panels

Date: 2026-07-23

## Purpose

Surfaces the six result arrays added in `docs/213`–`docs/219` (schema v23–v28) in the browser dashboard, which previously ignored them.

## What changed

- **Metric tiles**: `noiseBands`, `ltsa`, `ishmaelDetections`, `sgramCorrDetections`, `matchFiltDetections` counts, and the count of *classified* `matchedTemplateClassifications`, alongside the existing click/whistle tiles.
- **"Noise & Ishmael monitoring" panel**: cards for the latest noise band interval (per-band rms dB), latest LTSA periods (slice count, bin count, peak bin), the most recent detections from each of the three Ishmael detectors (sample span, peak, band), and recent matched-template verdicts (classified flag plus per-template threshold and match correlation).
- **"Monitoring & extra detector JSON" textarea** on the session form: an optional JSON object merged into the session config at the top level (form-owned keys always win), so `noiseBand`, `ltsa`, `ishmael`, `sgramCorr`, `matchFilt`, `matchedTemplate`, and `acquisition` blocks are configurable from the browser without six bespoke form editors.

## Validation

The script block parses (node syntax check). A live service check (session with noiseBand + ltsa + ishmael + sgramCorr + matchFilt over a 3 s stream with a 1 kHz tone burst) confirms every array the panels read populates at schema v28, with the energy sum and spectrogram correlation both detecting the burst.

One behaviour surfaced by that check and worth knowing: the noise band monitor emits at most **one** interval per PCM chunk (its `process` returns a single optional interval), so a chunk spanning several output intervals yields one. Streaming chunks are normally far shorter than the interval, where this cannot arise; recorded here rather than silently changed.

## Claim boundary

Display only — no engine or schema change. The panel shows the latest result body (live PCM response or feed poll), not history; the archive query panel remains the way to look back. LTSA is summarised (peak bin), not rendered as a long-term spectrogram image — that would be a worthwhile future slice.
