# Grouped archive retention

Date: 2026-07-01

## What changed

`ops/archive_retention.py` now treats archive files as session groups.

A group can include:

- `<session>.ndjson`
- `<session>.events.ndjson`
- `<session>.events.index.ndjson`

Retention candidate selection now deletes all sidecars in a selected group together.

`--max-groups` is the preferred count-based retention option. `--max-files` remains as a compatibility alias.

## Why this matters

The indexed archive path adds another sidecar. File-by-file retention can leave orphaned event indexes or delete canonical files separately from their derived sidecars.

Grouped retention keeps archive cleanup safer for production operations.

## Validation

`ops/archive_retention_smoke.py` creates two temporary archive groups and verifies that `--max-files 1` selects all sidecars from the older group.

CTest registers this as `archive_retention_grouped_smoke` when Python 3 is available.
