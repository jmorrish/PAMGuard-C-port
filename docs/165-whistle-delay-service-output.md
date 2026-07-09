# Whistle Delay Service Output

Date: 2026-07-06

## Purpose

`docs/164` ported the whistle contour delay core with fixture parity. This slice wires it into the session and service so multi-channel whistle sessions produce cross-channel contour delays per completed region — the first whistle localisation output to reach the API.

## Behaviour

When at least two channels run whistle region detection and the array has hydrophone geometry, the session:

- Retains a bounded per-channel history of complex spectrogram frames (512 slices; PAMGuard likewise accepts partial coverage when older FFT data has been discarded).
- For each completed region, accumulates the contour's `peak_info` bins across slices into the ported `WhistleDelayEstimator` for every (region channel, other whistle channel) pair in ascending audio-channel order, using the geometry-derived max delay (same formula as the click path).
- Emits `whistleDelays` entries: `channel`, `regionNumber`, `startSample`, and `delays[]` in the same shape as click localisation delays — samples/seconds/path difference, delay score, geometry constraint metadata, and PAMGuard pair bearing fields (including the schema-v8 array-axis spacing flip, via the shared attach path).

Delays require geometry: without hydrophone positions for both channels the pair is skipped (PAMGuard's whistle delays likewise depend on array-manager max delays).

## Schema

Bumps the engine result `schemaVersion` to `9` (purely additive).

## Validation

- New `session_whistle_delay_wiring` CTest: a two-channel session with a three-bin tone burst delayed 3 samples on channel 1 recovers the delay on each channel's best-scoring region (`+3.002` samples, score `0.9996`) with geometry and pair bearing attached; a geometry-free session detects regions but produces no delays. Leakage-skirt fragment regions landing on the narrowband ambiguity lobe are tolerated — that ambiguity is pinned by the whistle-delay fixture.
- Service smoke asserts schema version 9 on health, live results, and archived records.
- Full CTest suite passes `55/55`.

## Claim boundary

Delay output only: bearing conversion of whistle delays through the bearing localiser chain into `WhistleBearingInfo`-equivalent outputs, PAMGuard's exact backwards FFT-block search behaviour under data discard, and detection-grouper semantics remain unported. The engine computes delays for every channel's regions (PAMGuard detects on one group channel); pairs are deduplicated by ascending channel order within each region.
