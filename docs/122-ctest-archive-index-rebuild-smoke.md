# CTest archive index rebuild smoke

Date: 2026-07-01

## What changed

`ops/rebuild_archive_event_index_smoke.py` now creates a temporary detector-event archive, rebuilds the `.events.index.ndjson` sidecar, and verifies:

- Two expected index entries are written.
- Byte offsets match the canonical event sidecar.
- Event type and `startSample` are preserved.
- Optional `endSample` is preserved.
- Optional `sourceId`, `ownerId`, and `tenantId` metadata are preserved.

`cpp-engine/CMakeLists.txt` registers this as `archive_event_index_rebuild_smoke` when a Python 3 interpreter is available.

## Why this matters

The indexed archive path is now protected by automated validation, not just manual smoke testing.
