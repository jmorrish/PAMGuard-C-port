# Archive detection interval filters

Date: 2026-07-01

## Implemented

Archived detector-event queries now support interval-overlap filters:

- `overlapStartSample`
- `overlapEndSample`

The filters are available on:

- `GET /sessions/{sessionId}/archive/detections`
- `GET /sessions/{sessionId}/archive/detections/summary`
- `GET /sessions/{sessionId}/archive/detections.csv`

## Semantics

Each event is treated as:

- `[startSample, endSample]` when `endSample` is present.
- `[startSample, startSample]` for instant events.

An event matches the overlap window when:

```text
eventEnd >= overlapStartSample
eventStart <= overlapEndSample
```

Existing `startSampleFrom` / `startSampleTo` filters still apply to the event start sample. When both start filters and overlap filters are supplied, both must match.

## Validation

The HTTP service smoke now queries archived click-track events by a sample inside the track interval.
