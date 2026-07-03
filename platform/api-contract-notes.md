# API Contract Notes

## Core Resources

- `Source`: file, stream URL, direct device, or protocol connector
- `ArrayConfiguration`: hydrophone/channel metadata
- `DetectorConfiguration`: PAMGuard-compatible settings
- `AnalysisSession`: active or completed processing unit
- `DetectionSet`: results for one session/time range
- `SpectrogramTileSet`: browser-ready spectrogram data

## Session Lifecycle

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

## Current C++ Engine Service Surface

The current C++ service exposes a lower-level engine API for development and integration testing:

- `GET /health`
- `POST /sessions`
- `DELETE /sessions/{sessionId}`
- `POST /sessions/{sessionId}/pcm-f32le`

The PCM endpoint accepts interleaved little-endian `float32` audio frames. Query parameters:

- `startSample`: absolute first sample in the submitted chunk.
- `timeMs`: optional wall-clock timestamp in milliseconds.
- `includeSpectrogram`: when true, include renderable spectrogram frame data.
- `includeSpectrogramComplex`: when true, include complex FFT bins as `{real, imag}` pairs.
- `spectrogramMaxBins`: cap returned bins per frame.
- `spectrogramBinStride`: return every Nth bin.

Current result fields include:

- `spectrogramFrames`
- `spectrogram`, opt-in frame payloads
- `clicks`
- `clickFeatures`
- `clickClassifications`
- `clickLocalisations`
- `whistlePeaks`
- `whistleRegions`

Live Icecast/BUTT/direct-stream ingest is currently provided by `ffmpeg_stream_ingest`, which decodes media with FFmpeg and posts PCM chunks to the session endpoint.
