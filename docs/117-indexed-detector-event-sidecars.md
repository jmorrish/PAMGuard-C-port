# Indexed detector event sidecars

Date: 2026-07-01

## What changed

The archive now writes a lightweight detector-event index next to each event sidecar:

- Canonical event sidecar: `<session>.events.ndjson`
- Index sidecar: `<session>.events.index.ndjson`

The event sidecar remains the source of truth. The index stores enough metadata to find matching event lines quickly:

- Byte offset into the canonical event sidecar.
- Detector event type.
- `startSample`.
- Optional `endSample`.
- Optional `channelGroup`.
- Optional `sourceId`.
- Optional `ownerId`.
- Optional `tenantId`.

## Query behavior

`GET /sessions/{sessionId}/archive/detections` now prefers the index when both sidecar files exist.

The response includes:

```json
{
  "indexed": true
}
```

If the index is absent, the service falls back to scanning the canonical event sidecar, and `indexed` is `false`.

Cursor semantics are unchanged. The cursor remains a zero-based position in the filtered event stream, not a byte offset.

## Why this matters

This gives the enterprise port a practical indexed archive path without introducing a database dependency before the PAMGuard maths are pinned down.

It improves the shape of archive queries for multi-user deployments while preserving a simple recovery story: the canonical `.events.ndjson` file can still be scanned if the index is missing.

## Remaining production work

This is not a replacement for full production storage. A later phase should add:

- Durable database-backed indexes for large archives.
- Index rebuild tooling from canonical sidecars.
- Migrations and schema version management.
- Retention-aware compaction.
- Query planning beyond exact type and sample-range filters.
