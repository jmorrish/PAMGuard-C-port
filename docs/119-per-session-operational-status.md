# Per-session operational status

Date: 2026-07-01

## What changed

`GET /sessions/{sessionId}` now includes a compact `status` object alongside the existing detailed `runtime` counters.

The status object is designed for dashboards, autoscaling supervisors, and web-tier health views.

Key fields include:

- `activityState`: `awaiting-audio` or `audio-received`.
- `hasReceivedAudio`.
- `ageMs`.
- `idleMs`.
- `sampleTimelineOk`.
- `sampleDiscontinuities`.
- `lastSampleContinuity`.
- `nextExpectedStartSample`.
- `totalDetectorOutputs`.
- `meanProcessMs`.
- `lastProcessMs`.

## Prometheus additions

The `/metrics` endpoint now exposes:

- `pamguard_session_age_ms`
- `pamguard_session_idle_ms`
- `pamguard_session_has_received_audio`
- `pamguard_session_mean_process_ms`

## Design note

The engine deliberately does not decide what counts as stale. It reports age and idle time; the web tier, ingest supervisor, or Kubernetes probes can apply deployment-specific thresholds.
