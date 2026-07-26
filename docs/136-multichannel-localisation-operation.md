# Multi-channel localisation operation

Date: 2026-07-01

Deployment update (2026-07-25): production ingest now targets one stable Sound
Acquisition controlled-unit instance in the active project. Session routes
below describe retained compatibility validation, not the production ingest
identity model.

## Input model

Each engine session represents one source/input.

For multi-channel streams or files, audio is sent to the service as interleaved `f32le` frames:

```text
frame0_ch0, frame0_ch1, frame0_ch2, ...
frame1_ch0, frame1_ch1, frame1_ch2, ...
```

The session config defines:

- `sampleRateHz`
- `channelCount`
- FFT channels
- Click detector channel and trigger bitmaps
- Array hydrophones and speed of sound

`GET /sessions/{sessionId}` reports `array.clickLocalisationReadiness` so operators can see whether the active click channels have complete hydrophone geometry.

The FFmpeg ingest bridge is responsible for decoding WAV/MP3/Icecast/BUTT-style inputs into that interleaved PCM shape.

`--audio-filter` can be used for channel mapping, for example:

```text
pan=4c|c0=c0|c1=c1|c2=c2|c3=c3
```

## Click localisation path

When click localisation is enabled:

- The click detector captures waveform snippets for the configured channels.
- Pairwise time delays are estimated across channel pairs.
- Delay search windows are constrained from hydrophone spacing and speed of sound when array geometry is available.
- Far-field bearing foundation estimates bearing vectors from pair delays and hydrophone geometry.
- Geometry-constrained delay pairs additionally carry PAMGuard `PairBearingLocaliser` angle/error outputs (`pairBearingRadians`, `pairBearingDegrees`, `pairBearingErrorRadians`) from result schema version 5 (`docs/159-pair-bearing-service-output.md`).
- Sessions with four or more fully-geometry hydrophones and positive `spacingErrorM` additionally receive a per-click PAMGuard `lsqBearing` object from result schema version 6 (`docs/160-lsq-bearing-service-output.md`).
- Click train summaries aggregate per-click delay/bearing outputs into train-level summaries.

## API/archive outputs

The HTTP service smoke now verifies that multi-channel sessions return:

- Live `clickLocalisations` with delay pairs.
- Live `clickBearings` with used-pair metadata.
- Archived `click-localisation` detector events.
- Archived `click-bearing` detector events.

Detector events also carry:

- `sourceId`
- `ownerId`
- `tenantId`
- `relatedTrainIds` when click train output links a click to train IDs.

Session status also reports:

- `mode`: `disabled`, `geometry-constrained`, `partial-geometry`, `delay-only-unconstrained`, or `invalid-click-channel-count`.
- `geometryComplete`: true only when all click channels have hydrophone positions.
- `bearingEnabled`: true when at least two active click channels have hydrophone positions.
- `missingClickHydrophoneChannels`: click channels missing array geometry.

## Operational expectations

For live deployments:

- Keep all channels from one physical array in the same Acquisition stream so
  sample frames remain aligned.
- Use one ingest writer per stable Acquisition unit/timeline.
- Use explicit project Array Manager geometry for localisation-capable units.
- Keep ingest routing sticky to the engine instance that owns the active
  project runtime.
- Monitor `sampleContinuity`, `sampleDiscontinuities`, `idleMs`, and `lastSampleDelta`.

## Current claim boundary

The current web/API path carries multichannel click delays, pair/LSQ/ML-grid
bearings, ambiguity-preserving world/earth vectors, and PAMGuard-style
sub-array-shape localiser selection. It also provides engine-derived
click-train pair-bearing aggregation, whistle contour/region localisation, and
a project-authoritative tracked-click HTTP case that uses five posed
observations to recover a known target with navigation-beam/range/height
filter evidence.

That is still not a claim of complete PAMGuard localisation equivalence.
Remaining boundaries are:

- PAMGuard reference parity for the higher-level train-localiser behaviour;
- live GPS/georeferencing and finer-than-chunk pose interpolation;
- time-varying array deformation;
- whistle-group target localisation; and
- sustained real multichannel Icecast/BUTT ingest soaking.
