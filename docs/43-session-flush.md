# Session Flush Endpoint

Date: 2026-07-01

This checkpoint adds a flush path for finite files and streams.

## Implemented

- Added `AnalysisSession::flush()`.
- Added `SessionManager::flush_session()`.
- Added:

```text
POST /sessions/{sessionId}/flush
```

- The flush implementation emits pending whistle/moan connected regions and active click train summaries.
- Flush results are serialized with the same JSON path as normal chunk results.
- Result archives store flush outputs when archiving is enabled.
- `ffmpeg_stream_ingest` calls flush after normal EOF when not running in restart mode.
- The browser console exposes a `Flush session` button.
- Added detector-level CTest coverage:
  - `connected_region_flush_parity`

## Why this matters

Connected-region whistle/moan tracking keeps a region open while it is still growing. At the end of a WAV/MP3 file there may be no following empty slice to close that region, so a final flush is needed to emit the last contour.

## Remaining flush work

- Flush partial spectrogram frames if/when the spectrogram engine adds partial-frame emission.
- Extend train completion semantics as the full PAMGuard click train lifecycle is ported.
