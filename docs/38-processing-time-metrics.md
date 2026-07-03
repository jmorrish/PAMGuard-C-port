# Processing Time Metrics

Date: 2026-07-01

This checkpoint adds basic per-session processing-time telemetry.

## Implemented

- Measured wall-clock duration around each `SessionManager::process_audio` call.
- Runtime session JSON now reports:
  - `processCalls`;
  - `totalProcessMs`;
  - `lastProcessMs`.
- `/metrics` now reports:

```text
pamguard_session_process_calls{session="station-001"} 8842
pamguard_session_process_ms{session="station-001"} 4120.5
pamguard_session_last_process_ms{session="station-001"} 0.47
```

## Why this matters

For many concurrent streams, a session can be receiving audio but falling behind CPU-wise. These metrics give us the first operational signal for capacity planning and autoscaling.
