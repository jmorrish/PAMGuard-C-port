# Session List Smoke Coverage

Date: 2026-07-01

The service already exposes:

```text
GET /sessions
```

This checkpoint adds smoke-test coverage to ensure the active-session list includes a newly created session.

## Why this matters

The production web layer and ingest supervisors need an authenticated way to discover active engine sessions without relying on out-of-band IDs.

`GET /sessions` returns the same session status shape as `GET /sessions/{sessionId}`, including runtime counters when available.
