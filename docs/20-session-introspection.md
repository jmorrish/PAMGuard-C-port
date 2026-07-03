# Session Introspection Endpoints

Date: 2026-06-30

This checkpoint adds operator/browser visibility into active engine sessions.

## Implemented

- Added:

```text
GET /sessions
GET /sessions/{sessionId}
```

- `GET /sessions` returns:
  - compact effective config for each active session;
  - total session count.
- `GET /sessions/{sessionId}` returns:
  - session/source IDs;
  - sample rate and channel count;
  - FFT settings;
  - enabled detector flags;
  - array summary;
  - `exists: true`.

## Validation

CTest status after this checkpoint:

```text
21/21 tests passed
```

HTTP smoke test:

```json
{"count":1,"listed":1,"exists":true,"sampleRateHz":48000}
```

## Remaining platform work

- Add authentication and tenant scoping.
- Add per-session byte/frame counters.
- Add per-session ingest health and reconnect state.
- Add persistent session/config storage.
