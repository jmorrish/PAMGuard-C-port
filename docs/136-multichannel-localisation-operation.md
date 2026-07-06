# Multi-channel localisation operation

Date: 2026-07-01

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

- Keep all channels from one physical array in the same session so sample frames remain aligned.
- Use one ingest worker per source/session.
- Use explicit array geometry for localisation-capable sessions.
- Keep session routing sticky to the engine instance that owns the session.
- Monitor `sampleContinuity`, `sampleDiscontinuities`, `idleMs`, and `lastSampleDelta`.

## Current claim boundary

It is safe to claim that the current web/API path carries multi-channel click delay and bearing foundation outputs.

It is not safe yet to claim full PAMGuard click localisation equivalence.

Remaining work includes:

- LSQ bearing localiser selection for four-plus hydrophone sessions (pair bearing is exposed; the ported LSQ localiser is engine-only so far).
- More array geometry models.
- Whistle/moan localisation.
- Real multi-channel Icecast/BUTT soak tests.
- Controlled localisation fixtures with known source positions.
