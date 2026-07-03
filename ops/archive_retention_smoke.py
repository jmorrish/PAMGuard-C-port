#!/usr/bin/env python3
"""Smoke test for grouped archive retention candidate selection."""

from __future__ import annotations

import tempfile
import time
from pathlib import Path

from archive_retention import archive_groups, retention_candidates


def write(path: Path, text: str, mtime: float) -> None:
    path.write_text(text, encoding="utf-8")
    import os

    os.utime(path, (mtime, mtime))


def main() -> int:
    now = time.time()
    old = now - 10_000
    new = now - 100

    with tempfile.TemporaryDirectory(prefix="pamguard-retention-") as temp_dir:
        archive_dir = Path(temp_dir)
        write(archive_dir / "old.ndjson", "old-result\n", old)
        write(archive_dir / "old.events.ndjson", "old-event\n", old)
        write(archive_dir / "old.events.index.ndjson", "old-index\n", old)
        write(archive_dir / "new.ndjson", "new-result\n", new)
        write(archive_dir / "new.events.ndjson", "new-event\n", new)
        write(archive_dir / "new.events.index.ndjson", "new-index\n", new)

        groups = archive_groups(archive_dir)
        if [group.key for group in groups] != ["old", "new"]:
            raise RuntimeError(f"archive groups were not ordered by group mtime: {groups}")

        candidates = retention_candidates(groups, max_age_days=None, max_files=1)
        candidate_names = sorted(candidate.path.name for candidate in candidates)
        expected = ["old.events.index.ndjson", "old.events.ndjson", "old.ndjson"]
        if candidate_names != expected:
            raise RuntimeError(f"group retention did not select all old sidecars: {candidate_names}")

    print("Archive retention grouped smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
