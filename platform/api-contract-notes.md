# API Contract Notes

## Primary project resources

- `Project`: the saved/working controlled-unit configuration
- `ControlledUnit`: a stable PAMGuard module instance
- `DataBlock`: a typed runtime output derived from a controlled unit
- `Acquisition`: a Sound Acquisition controlled unit with a stable `unitId`

The normal operator and supervised-ingest surface is project-authoritative.
External audio is posted to:

```text
POST /v1/projects/active/acquisitions/{acquisitionUnitId}/pcm-f32le
```

with `expectedWorkingRevision` and `startSample`. Discover stable Acquisition
IDs and the current working revision through:

```text
GET /v1/projects/active/acquisitions
```

## Compatibility resources

- `Source`: file, stream URL, direct device, or protocol connector
- `ArrayConfiguration`: hydrophone/channel metadata
- `DetectorConfiguration`: PAMGuard-compatible settings
- `AnalysisSession`: active or completed processing unit
- `DetectionSet`: results for one session/time range
- `SpectrogramTileSet`: browser-ready spectrogram data

## Compatibility session lifecycle

```text
created -> starting -> running -> reconnecting -> draining -> complete
created -> starting -> failed
running -> stopped
```

## Result Event Types

- `spectrogram_frame`
- `click_detection`
- `click_track_update`
- `whistle_contour`
- `background_measurement`
- `stream_health`
- `session_status`

## API Direction

Keep the public API independent from internal detector classes. The API should expose stable scientific concepts and versioned config/result schemas.

## Current C++ engine service surface

The primary service exposes versioned project/controlled-unit endpoints,
including stable Acquisition discovery, host binding, capture, and PCM ingest.

The older routes remain explicitly for regression, archive, job, and project
import compatibility:

- `POST /sessions`
- `DELETE /sessions/{sessionId}`
- `POST /sessions/{sessionId}/pcm-f32le`

Both PCM endpoints accept interleaved little-endian `float32` frames.
Project-native ingest verifies the stable project/unit identity, running
runtime, audio shape, and exact working revision before audio is accepted.

Current result fields include:

- `spectrogramFrames`
- `spectrogram`, opt-in frame payloads
- `clicks`
- `clickFeatures`
- `clickClassifications`
- `clickLocalisations`
- `whistlePeaks`
- `whistleRegions`

Production Icecast/BUTT/direct-stream ingest uses
`ops/ingest_supervisor.py` with the schema-v2 active-project manifest. It
launches FFmpeg directly without a shell and posts to the stable Acquisition
endpoint. `ffmpeg_stream_ingest` is retained for the explicitly named
legacy-session compatibility mode.
