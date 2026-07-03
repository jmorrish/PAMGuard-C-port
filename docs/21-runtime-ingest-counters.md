# Runtime Ingest Counters

Date: 2026-06-30

This checkpoint adds lightweight per-session runtime counters to support multi-user/source operations.

## Implemented

Session status now includes:

```json
"runtime": {
  "createdUnixMs": 0,
  "lastReceiveUnixMs": 0,
  "chunksReceived": 0,
  "framesReceived": 0,
  "bytesReceived": 0,
  "lastStartSample": 0,
  "lastTimeMs": 0
}
```

Counters are updated whenever PCM is submitted to:

```text
POST /sessions/{sessionId}/pcm-f32le
```

## Validation

CTest status after this checkpoint:

```text
21/21 tests passed
```

HTTP smoke test:

```json
{"chunks":1,"frames":128,"bytes":512,"lastStart":320}
```

## Remaining operations work

- Add ingest liveness timestamps.
- Add detector processing duration metrics.
- Add queue/backpressure metrics.
- Add per-tenant/user attribution.
- Add Prometheus/OpenTelemetry export.
