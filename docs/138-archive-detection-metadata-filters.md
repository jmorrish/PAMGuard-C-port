# Archive detection metadata filters

Date: 2026-07-01

## What changed

Archived detector-event queries now support exact metadata filters:

- `sourceId`
- `ownerId`
- `tenantId`

These filters are available on:

- `GET /sessions/{sessionId}/archive/detections`
- `GET /sessions/{sessionId}/archive/detections.csv`

The JSON response echoes the applied metadata filters.

The CSV export now includes metadata columns:

```text
type,sessionId,sourceId,ownerId,tenantId,startSample,endSample,recordStartSample,channelGroup,relatedTrainIds,payload
```

## Index behavior

New detector-event index entries include `sourceId`, `ownerId`, and `tenantId` when present.

For correctness with existing archives, metadata-filtered queries currently fall back to scanning the canonical event sidecar rather than relying on older index files that may not contain metadata.

## Browser behavior

The browser archive query applies the current `Owner ID` and `Tenant ID` fields when they are filled in.

`sourceId` filtering is exposed through the API/OpenAPI for integration callers.

## Validation

The service smoke verifies:

- Matching source/owner/tenant metadata returns the expected click events.
- A missing owner returns zero detector events.
- Metadata-filtered queries fall back from the sidecar index for correctness.
- CSV export includes source/owner/tenant columns and values.
