# Ingest Session Bootstrap

Date: 2026-07-01

This checkpoint lets an FFmpeg ingest worker create its target analysis session before streaming audio.

## Implemented

- Added `ffmpeg_stream_ingest --session-config <json-file>`.
- Added `--allow-existing-session` for supervised restarts.
- Session creation uses the same configured engine URL and API key as chunk posting.
- Added a ready-to-edit example:
  - `platform/station-session.example.json`

## Example

```powershell
.\build\ffmpeg_stream_ingest.exe `
  --source "http://station.example/live.mp3" `
  --engine "http://127.0.0.1:8080" `
  --session "station-001" `
  --session-config ".\platform\station-session.example.json" `
  --allow-existing-session `
  --sample-rate 48000 `
  --channels 2 `
  --realtime `
  --restart
```

This makes each ingest worker self-contained: it can provision the deterministic C++ session, decode the stream, post PCM chunks, and flush on finite EOF.
