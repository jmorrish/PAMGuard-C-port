# Result Archive Query Endpoint

Date: 2026-07-01

This checkpoint adds a simple API reader for the optional NDJSON result archive.

## Implemented

When `PAMGUARD_RESULT_ARCHIVE_DIR` is set, archived records can be read with:

```text
GET /sessions/{sessionId}/archive?limit=100
```

Optional sample range filters:

```text
GET /sessions/{sessionId}/archive?startSampleFrom=48000&startSampleTo=96000&limit=100
```

Behaviour:

- reads `<sessionId>.ndjson`;
- returns the most recent `limit` records with a bounded in-memory tail buffer for positive limits;
- when `startSampleFrom` and/or `startSampleTo` are provided, only records with `startSample` inside the inclusive range are considered;
- returns an empty array if no archive exists;
- `limit=0` returns all records unless `PAMGUARD_MAX_ARCHIVE_QUERY_RECORDS` is set.

Production guard:

```text
PAMGUARD_MAX_ARCHIVE_QUERY_RECORDS
```

When set to a positive number, archive requests with `limit=0` or `limit` above that value are rejected with HTTP `400`.

Response shape:

```json
{
  "sessionId": "demo",
  "count": 1,
  "limit": 100,
  "startSampleFrom": null,
  "startSampleTo": null,
  "records": []
}
```

Detector-event projection:

```text
GET /sessions/{sessionId}/archive/detections?type=click&startSampleFrom=48000&limit=100
```

This endpoint scans the same NDJSON archive but returns timestamped detector-event envelopes instead of full engine records. The `limit` applies to projected events, not chunks, and `startSampleFrom` / `startSampleTo` filter by event `startSample`.

Forward pagination can be requested with a cursor over the filtered event stream:

```text
GET /sessions/{sessionId}/archive/detections?type=click&cursor=0&limit=100
```

When `cursor` is omitted, the endpoint returns the most recent event tail. When `cursor` is present, the endpoint returns matching events forward from that cursor and may return `nextCursor`.

Initial event types:

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

## Validation

CTest status after this checkpoint:

```text
23/23 tests passed
```

Archive query smoke test after two chunk posts:

```json
{"count":1,"startSample":128,"inputFrames":128}
```

## Remaining result API work

- Add indexed storage instead of full-file scans.
- Add pagination cursors.
