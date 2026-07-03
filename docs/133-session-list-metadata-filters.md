# Session list metadata filters

Date: 2026-07-01

## What changed

`GET /sessions` now supports optional exact-match filters:

- `sourceId`
- `ownerId`
- `tenantId`

The response includes:

- `count`: number of returned sessions after filtering.
- `totalSessions`: total active sessions in the engine.
- Echoed filter values.

## Scope

These filters are not authorization. They are convenience filters for an authenticated web tier or operations dashboard.

## Validation

The service smoke now verifies that owner/tenant/source filters return the expected session and that a missing owner returns zero sessions.
