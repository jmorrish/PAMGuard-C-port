# Service Metrics Endpoint

Date: 2026-07-01

This checkpoint adds a lightweight operations endpoint for multi-session deployments.

## Implemented

- Added `GET /metrics`.
- The endpoint emits Prometheus text format.
- Metrics include:
  - active session count;
  - configured maximum sessions;
  - chunks received per session;
  - frames received per session;
  - bytes received per session;
  - session creation wall-clock time;
  - last PCM receive wall-clock time.
  - detector outputs by type per session.
  - processing call count and processing time per session.
- The endpoint is protected by `PAMGUARD_API_KEY` when API-key protection is enabled.

## Example

```text
pamguard_sessions 12
pamguard_max_sessions 64
pamguard_session_chunks_received{session="station-001"} 8842
pamguard_session_frames_received{session="station-001"} 36216832
pamguard_session_bytes_received{session="station-001"} 289734656
pamguard_session_detector_outputs{session="station-001",type="clicks"} 42
pamguard_session_detector_outputs{session="station-001",type="whistle_regions"} 7
pamguard_session_process_calls{session="station-001"} 8842
pamguard_session_process_ms{session="station-001"} 4120.5
pamguard_session_last_process_ms{session="station-001"} 0.47
```

## Remaining metrics work

- Add latency histograms.
- Add ingest-worker health metrics.
