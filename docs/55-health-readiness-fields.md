# Health Readiness Fields

Date: 2026-07-01

This checkpoint expands `GET /health` with deployment-readiness fields.

## Implemented

`GET /health` now reports:

```json
{
  "sessionConfigPersistenceEnabled": true,
  "resultArchiveEnabled": true
}
```

Existing health output also reports session count, capacity, result schema version, API-key requirement, CORS origin, PCM body limit, archive-query record limit, HTTP thread count, web UI serving, and OpenAPI serving.

## Readiness endpoint

`GET /ready` reports whether the service can accept another session.

It returns HTTP `200` when capacity is available and HTTP `503` when `PAMGUARD_MAX_SESSIONS` is configured and the current session count has reached that limit.

## Why this matters

For container and supervised deployments, health checks should show whether persistence and archiving are actually configured instead of requiring operators to inspect startup logs.
