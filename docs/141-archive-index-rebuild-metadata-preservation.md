# Archive index rebuild metadata preservation

Date: 2026-07-01

## What changed

`ops/rebuild_archive_event_index.py` now preserves detector-event metadata when regenerating sidecar indexes:

- `sourceId`
- `ownerId`
- `tenantId`

## Why this matters

Live archive writes already include this metadata in new index entries. Rebuilt indexes must preserve the same fields so archive migrations, disaster recovery, and retention workflows do not silently downgrade multi-user archive metadata.

## Validation

`ops/rebuild_archive_event_index_smoke.py` now writes sample detector events with source/owner/tenant metadata and verifies those fields are present in the regenerated `.events.index.ndjson`.
