# GitHub Actions Python ops coverage

Date: 2026-07-01

## What changed

The Windows C++ engine workflow now syntax-checks all Python ops scripts:

- `ops/ingest_supervisor.py`
- `ops/ingest_supervisor_status_smoke.py`
- `ops/archive_retention.py`
- `ops/rebuild_archive_event_index.py`
- `ops/rebuild_archive_event_index_smoke.py`

The CTest phase also picks up these Python-backed smokes when Python 3 is available during CMake configure:

- `archive_event_index_rebuild_smoke`
- `archive_retention_grouped_smoke`
- `ingest_supervisor_status_smoke`
