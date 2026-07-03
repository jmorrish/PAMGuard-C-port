# Ingest startSample resume controls

`ffmpeg_stream_ingest` now supports explicit start-sample recovery for supervised deployments.

Options:

- `--start-sample <n>` sets the initial `startSample` for the first posted chunk.
- `--resume-from-engine` queries `GET /sessions/{sessionId}` and uses `runtime.expectedStartSample` before ingest starts.

Typical live-stream restart pattern:

```powershell
.\build\ffmpeg_stream_ingest.exe `
  --source "http://icecast.example/live.mp3" `
  --session "station-001" `
  --session-config ".\platform\station-session.example.json" `
  --allow-existing-session `
  --resume-from-engine `
  --restart
```

This avoids accidentally posting a new worker's first chunk at `startSample=0` into an already-running session. If the source itself restarts from the beginning of a file, do not use `--resume-from-engine`; create a new session or set `--start-sample` deliberately.
