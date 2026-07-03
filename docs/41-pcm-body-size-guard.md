# PCM Body Size Guard

Date: 2026-07-01

This checkpoint adds an optional HTTP request size guard for PCM ingest.

## Implemented

- Added `PAMGUARD_MAX_PCM_BODY_BYTES`.
- Default `0` keeps existing unlimited behaviour.
- When set, `POST /sessions/{id}/pcm-f32le` rejects oversized chunks with HTTP `413`.
- `GET /health` reports `maxPcmBodyBytes`.

## Example

```powershell
$env:PAMGUARD_MAX_PCM_BODY_BYTES = 8388608
.\build\pamguard_engine_service.exe 8080
```

At 48 kHz, 2-channel, f32le, `8388608` bytes is about 21.8 seconds of audio. Production deployments should normally use smaller chunks for latency and backpressure.
