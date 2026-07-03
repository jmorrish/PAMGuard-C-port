# Archive Event Sidecar

Date: 2026-07-01

This checkpoint adds sidecar persistence for detector-event archive queries.

## Files

When `PAMGUARD_RESULT_ARCHIVE_DIR` is configured, each session can now produce two archive files:

- `<sessionId>.ndjson`: raw compact engine result chunks.
- `<sessionId>.events.ndjson`: projected detector-event envelopes.

## Read behavior

`GET /sessions/{sessionId}/archive/detections` now prefers the sidecar file.

If the sidecar is missing, the service falls back to scanning `<sessionId>.ndjson` and projecting events in memory. This keeps existing archives readable.

Cursor pagination is defined over the filtered event stream. For example, `type=click&cursor=10&limit=100` starts at the eleventh matching click event for that filter set.

## Write behavior

On each PCM process call and flush call, the service writes the raw archive record first, then writes all projected detector events to the sidecar under the same archive mutex.

This keeps the two files append-consistent for the single-process service deployment model.

## Why this matters

The browser and future APIs can query detector timelines without repeatedly loading whole chunk records. This is a step toward scalable multi-user archive access.

## Remaining scale work

- Add cursor pagination for event queries.
- Add sample/time indexes for long-running streams.
- Move durable event storage to a database or object-store-backed index for multi-instance deployments.
