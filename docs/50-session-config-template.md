# Session Config Template

Date: 2026-07-01

This checkpoint adds a concrete low-level engine session template for stream workers.

## Added

```text
platform/station-session.example.json
```

The template includes:

- FFT length, hop, window, and channels;
- two-channel linear array geometry;
- click detector, localisation, feature extraction, classifier presets, and train tracking;
- whistle peak detector and connected-region/rejoin settings.

## Usage

```powershell
.\cpp-engine\build\ffmpeg_stream_ingest.exe `
  --source "http://station.example/live.mp3" `
  --engine "http://127.0.0.1:8080" `
  --session "station-001" `
  --session-config ".\platform\station-session.example.json" `
  --allow-existing-session `
  --sample-rate 48000 `
  --channels 2 `
  --restart
```
