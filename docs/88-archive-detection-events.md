# Archive Detection Event Endpoint

Date: 2026-07-01

This checkpoint adds a detector-event projection over the optional NDJSON result archive.

## Endpoint

```text
GET /sessions/{sessionId}/archive/detections
```

Query parameters:

- `type`: optional exact event type filter.
- `limit`: number of most recent events to return; `0` requests all events unless capped by `PAMGUARD_MAX_ARCHIVE_QUERY_RECORDS`.
- `cursor`: optional zero-based cursor over the filtered event stream.
- `startSampleFrom`: inclusive event start-sample lower bound.
- `startSampleTo`: inclusive event start-sample upper bound.
- `overlapStartSample`: inclusive lower bound for event intervals; an event matches when its `endSample`, or `startSample` for instant events, is greater than or equal to this value.
- `overlapEndSample`: inclusive upper bound for event intervals; an event matches when its `startSample` is less than or equal to this value.

When `cursor` is omitted, results are the most recent matching event tail. When `cursor` is present, results are returned forward from that cursor and the response may include `nextCursor` for the next page.

The existing raw endpoint remains available:

```text
GET /sessions/{sessionId}/archive
```

## Event envelope

Each returned item has a stable envelope:

```json
{
  "type": "click",
  "sessionId": "demo",
  "startSample": 123456,
  "endSample": 123480,
  "recordStartSample": 123392,
  "relatedTrainIds": [7],
  "channelGroup": "triggerBitmap:3",
  "payload": {}
}
```

The `payload` is the original compact detector payload from the archived engine record. This preserves detector-specific fields while keeping the archive API shape stable for web clients.

`relatedTrainIds` is emitted when a click-related event can be associated with click train payloads in the same archived engine record.

`whistle-contour` events use the contour payload `durationSamples` field to populate `endSample`, so archived contour detections can be queried/exported as time ranges rather than start-only point events.

Use `overlapStartSample` / `overlapEndSample` when looking for ranged events such as click tracks or whistle contours that intersect a sample window.

## Initial event types

- `click`
- `click-feature`
- `click-classification`
- `click-localisation`
- `click-bearing`
- `click-track`
- `click-track-localisation`
- `click-track-bearing`
- `whistle-peak`
- `whistle-contour`

## Persistence note

New archive writes also create a detector-event sidecar:

```text
<sessionId>.events.ndjson
```

The detection endpoint prefers this sidecar and falls back to projecting older raw chunk archives when the sidecar is not present.

The sidecar keeps detection queries away from full chunk payloads and avoids re-projecting detector arrays on every request. It is still a sequential NDJSON scan, so production multi-user deployments should add a cursor/index or database-backed event table before retaining long-running high-rate streams.

## CSV export

The same filters can be exported as CSV:

```text
GET /sessions/{sessionId}/archive/detections.csv?type=click&limit=100
```

CSV columns:

```text
type,sessionId,startSample,endSample,recordStartSample,channelGroup,relatedTrainIds,payload
```
