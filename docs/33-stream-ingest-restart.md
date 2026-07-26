# Stream Ingest Restart Mode

Date: 2026-07-01

Compatibility boundary (2026-07-25): the options below still describe the
retained fixed-session bridge, not the production ingest identity model.
Production uses the active-project supervisor in `docs/91`.

This checkpoint hardens the FFmpeg ingest bridge for long-running Icecast/BUTT/direct internet streams.

## Implemented

- Added `--restart` to restart the FFmpeg process after EOF or errors.
- Added `--restart-delay-ms` for backoff between process restarts.
- Added `--max-restarts` for bounded restart testing or operations.
- Preserved `startSample` across restarts so the engine session timeline remains monotonic.
- Added `--start-sample` for manual supervised recovery.
- Added `--resume-from-engine` to query `runtime.expectedStartSample` from an existing engine session before posting PCM.
- Kept `--max-chunks` as a smoke-test escape hatch that exits cleanly without restarting.

## Deployment model

Run one bridge process per source/session:

```powershell
.\build\ffmpeg_stream_ingest.exe `
  --source "http://station.example/live.mp3" `
  --engine "http://127.0.0.1:8080" `
  --session "station-001" `
  --sample-rate 48000 `
  --channels 2 `
  --chunk-frames 4096 `
  --restart `
  --restart-delay-ms 5000
```

This remains available only for fixed-session compatibility. The production
shape is one supervised direct-FFmpeg writer per stable Sound Acquisition
instance.

## Remaining ingest work

- Add per-source health endpoint and metrics.
- Add tenant/auth checks before a bridge can feed a session.
- Add queue/backpressure policy for slow service consumers.
