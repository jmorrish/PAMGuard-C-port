# Architecture

## System Overview

```text
Audio Sources
Icecast / BUTT / WAV / MP3 / direct Ethernet
        |
Ingest Layer
decode, timestamp, buffer, reconnect, archive
        |
Normalized PCM Stream
sample-accurate frames + metadata
        |
Analysis Session
source + detector config + detector state
        |
C++ PAMGuard-Math Engine
FFT / spectrogram / click / whistle-moan / localisation
        |
Results Layer
detections, contours, spectrogram tiles, exports
        |
Web App + API
configuration, monitoring, review, download
```

## Analysis Session

The primary engine isolation unit is:

```text
AnalysisSession = audio source + detector configuration + array metadata + detector state
```

Use one session per source/config combination. Multiple users may subscribe to the same session if they share source and config.

## Worker Model

Offline files use ephemeral jobs:

```text
file + config -> queued job -> worker session -> results -> session exits
```

Live streams use long-running sessions:

```text
stream + config -> assigned worker -> continuous session -> real-time results
```

## Shared Pipeline

For one source running multiple modules, shared upstream processing should be reused:

```text
PCM stream
-> shared framing and channel buffers
-> shared FFT/spectrogram where settings match
-> click detector path
-> whistle/moan detector path
-> localisation/tracking path
```

## Scaling Rule

Do not scale by user count alone. Scale by active analysis sessions and processing load.

Examples:

- 50 users watching one stream with one config: one engine session plus many subscribers.
- One user running 10 sources: 10 sessions.
- One stream analysed with 3 detector configs: 3 sessions, with possible shared decoded PCM.

## Storage

Store enough to make detections auditable:

- Source audio reference or archived chunks
- Decoded PCM chunk references where needed
- Engine version
- Config version
- Array metadata version
- Detection outputs
- Intermediate parity data for test fixtures

