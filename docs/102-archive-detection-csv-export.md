# Archive Detection CSV Export

Date: 2026-07-01

This checkpoint adds CSV export for archived detector events.

## Endpoint

```text
GET /sessions/{sessionId}/archive/detections.csv
```

It accepts the same filters as the JSON event endpoint:

- `type`
- `limit`
- `cursor`
- `startSampleFrom`
- `startSampleTo`

## Columns

```text
type,sessionId,startSample,endSample,recordStartSample,channelGroup,relatedTrainIds,payload
```

`payload` is the detector-specific compact JSON payload escaped as a CSV cell.

## Validation

The service smoke test now verifies that CSV export returns expected click rows.
