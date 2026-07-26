# FFmpeg Stream Ingest Bridge

Date: 2026-06-30

Compatibility boundary (2026-07-25): this is the historical fixed-session
bridge checkpoint. Production ingest now uses the schema-v2 active-project
supervisor documented in `docs/91-ingest-supervisor.md`; the bridge remains
only behind explicitly named legacy compatibility.

This checkpoint adds a bridge from Icecast/BUTT/direct media URLs into the C++ engine service.

## Implemented

- Added `ffmpeg_stream_ingest`.
- The tool starts FFmpeg, decodes a source URL or file, converts it to interleaved little-endian `float32` PCM, and posts chunks to:

```text
POST /sessions/{sessionId}/pcm-f32le?startSample=N
```

- Supported source types are whatever the installed FFmpeg can decode, including:
  - Icecast HTTP streams;
  - BUTT/Shoutcast-style MP3 streams;
  - WAV files/URLs;
  - MP3/AAC/other FFmpeg-supported media files.
- FFmpeg handles:
  - decode;
  - resampling;
  - channel-count conversion;
  - stream reconnect flags for HTTP/HTTPS sources.
- The engine service remains responsible for:
  - per-session detector state;
  - click/whistle/spectrogram processing;
  - localisation;
  - feature extraction/classification.

## Example

```powershell
.\build\ffmpeg_stream_ingest.exe `
  --source "http://example.org/live.mp3" `
  --engine "http://127.0.0.1:8080" `
  --session "station-001" `
  --sample-rate 48000 `
  --channels 2 `
  --chunk-frames 4096 `
  --session-config ".\station-001-session.json" `
  --allow-existing-session `
  --audio-filter "pan=2c|c0=c0|c1=c1" `
  --realtime `
  --restart `
  --restart-delay-ms 5000 `
  --ffmpeg "C:\path\to\ffmpeg.exe"
```

## Validation

The bridge was built with MSVC/Ninja.

CTest status after this checkpoint:

```text
19/19 tests passed
```

End-to-end ingest smoke test:

```text
posted chunk 1 startSample=0 continuity=first delta=0 nextExpected=2048
posted chunk 2 startSample=2048 continuity=contiguous delta=0 nextExpected=4096
```

The smoke test intentionally used `--max-chunks 2`, so FFmpeg reported a broken-pipe warning when the bridge stopped early. That is expected for the smoke test and not expected for normal continuous streaming.

## Historical compatibility scale model

The old model was one bridge per source/session. It remains available for
regression/archive compatibility.

The current `platform/ingest-sources.example.json` does not launch this bridge:
it targets stable active-project Acquisition unit IDs. The deliberately named
`platform/ingest-sources.legacy-session-compat.example.json` demonstrates the
retained bridge mode.

## Operational restart mode

The bridge now supports an outer FFmpeg process restart loop:

```powershell
.\build\ffmpeg_stream_ingest.exe `
  --source "http://example.org/live.mp3" `
  --engine "http://127.0.0.1:8080" `
  --session "station-001" `
  --sample-rate 48000 `
  --channels 2 `
  --restart `
  --max-restarts 0 `
  --restart-delay-ms 5000
```

`startSample` is preserved across restarts so the engine session remains monotonic even if the decoder process exits and reconnects.

## Self-contained session creation

An ingest worker can create its target engine session before decoding starts:

```powershell
.\build\ffmpeg_stream_ingest.exe `
  --source "http://example.org/live.mp3" `
  --engine "http://127.0.0.1:8080" `
  --session "station-001" `
  --session-config ".\platform\station-session.example.json" `
  --allow-existing-session `
  --sample-rate 48000 `
  --channels 2 `
  --realtime
```

`--allow-existing-session` is useful when a supervisor restarts the ingest worker but the service still has the session.

Use `--realtime` when feeding a file/URL at its native media rate is more important than offline catch-up speed.

## Channel mapping and localisation

For multi-channel localisation streams, the bridge now supports:

```text
--audio-filter "<ffmpeg -af expression>"
```

Example:

```powershell
.\build\ffmpeg_stream_ingest.exe `
  --source "http://icecast.example/hydrophones.wav" `
  --session "array-001" `
  --channels 4 `
  --audio-filter "pan=4c|c0=c0|c1=c1|c2=c2|c3=c3"
```

Use this when the source channel order needs explicit mapping before the engine interprets interleaved samples against the configured hydrophone geometry.

## Remaining ingest work

- Add per-source health reporting and metrics.
- Add authentication/tenant ownership checks before a source can feed a session.
- Add queue/backpressure policy for slow consumers.
- Add HTTPS client support for engine URLs if the service is deployed behind TLS directly rather than behind a reverse proxy.
