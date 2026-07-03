# Archive detection summary endpoint

Date: 2026-07-01

## What changed

The service now exposes:

```text
GET /sessions/{sessionId}/archive/detections/summary
```

It supports the same operational filters used by detector-event archive queries:

- `type`
- `sourceId`
- `ownerId`
- `tenantId`
- `startSampleFrom`
- `startSampleTo`

The response includes:

- `totalCount`
- `types`
- `minStartSample`
- `maxStartSample`
- `source`
- `indexedAvailable`
- echoed filters

## Design note

The summary scans the canonical event sidecar when available, with raw result archive fallback if the event sidecar is absent.

This avoids returning or allocating the full event list when a dashboard only needs counts and sample bounds.

## Validation

The service smoke verifies:

- Owned click summaries match the owned click event count.
- Type counts are populated.
- Missing-owner filters return zero summary count.
- Event sidecar and index availability are reported.
