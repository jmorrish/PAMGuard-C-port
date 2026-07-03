# Ingest Real-Time Pacing

Date: 2026-07-01

This checkpoint adds optional FFmpeg read pacing for ingest workers.

## Implemented

- Added `ffmpeg_stream_ingest --realtime`.
- The option inserts FFmpeg `-re` before the input.
- Default behaviour remains offline-fast decoding/posting.

## Usage

```powershell
.\cpp-engine\build\ffmpeg_stream_ingest.exe `
  --source ".\recording.wav" `
  --engine "http://127.0.0.1:8080" `
  --session "file-demo" `
  --session-config ".\platform\station-session.example.json" `
  --sample-rate 48000 `
  --channels 2 `
  --realtime
```

## When to use it

Use `--realtime` for demos, soak tests, and live-like playback. Leave it off for offline batch analysis when the service can process faster than real time.
