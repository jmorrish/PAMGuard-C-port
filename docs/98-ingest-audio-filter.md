# FFmpeg Ingest Audio Filter

Date: 2026-07-01

This checkpoint adds optional FFmpeg audio-filter support to `ffmpeg_stream_ingest`.

## New flag

```text
--audio-filter <ffmpeg-filter>
```

The bridge inserts this as:

```text
-af <ffmpeg-filter>
```

before `-ac`, `-ar`, and `-f f32le`.

## Localisation use case

For multi-channel arrays, channel order must match the hydrophone geometry sent in the engine session config.

Example:

```powershell
.\build\ffmpeg_stream_ingest.exe `
  --source "http://icecast.example/hydrophones.wav" `
  --engine "http://127.0.0.1:8080" `
  --session "array-001" `
  --channels 4 `
  --audio-filter "pan=4c|c0=c0|c1=c1|c2=c2|c3=c3"
```

The multi-source supervisor accepts the same value as `audioFilter` per source.
