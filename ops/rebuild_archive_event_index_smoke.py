#!/usr/bin/env python3
"""Smoke test for detector-event archive index rebuilds."""

from __future__ import annotations

import json
import tempfile
from pathlib import Path

from rebuild_archive_event_index import EVENT_SUFFIX, INDEX_SUFFIX, rebuild_one


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="pamguard-index-rebuild-") as temp_dir:
        archive_dir = Path(temp_dir)
        event_path = archive_dir / f"smoke-session{EVENT_SUFFIX}"
        first_line = b'{"type":"click","sessionId":"smoke-session","sourceId":"smoke-source","ownerId":"owner-a","tenantId":"tenant-a","startSample":0,"channelGroup":"channel:0"}\n'
        second_line = b'{"type":"whistle-peak","sessionId":"smoke-session","sourceId":"smoke-source","ownerId":"owner-a","tenantId":"tenant-a","startSample":256,"endSample":300}\n'
        event_path.write_bytes(first_line + second_line)

        result = rebuild_one(event_path, force=True, dry_run=False)
        if result.skipped or result.event_count != 2:
            raise RuntimeError(f"unexpected rebuild result: {result}")

        index_path = archive_dir / f"smoke-session{INDEX_SUFFIX}"
        entries = [json.loads(line) for line in index_path.read_text(encoding="utf-8").splitlines() if line.strip()]
        if len(entries) != 2:
            raise RuntimeError(f"expected two index entries, got {len(entries)}")
        if entries[0]["offset"] != 0 or entries[0]["type"] != "click" or entries[0]["startSample"] != 0:
            raise RuntimeError(f"first index entry mismatch: {entries[0]}")
        if entries[0].get("sourceId") != "smoke-source" or entries[0].get("ownerId") != "owner-a" or entries[0].get("tenantId") != "tenant-a":
            raise RuntimeError(f"first index metadata mismatch: {entries[0]}")
        if entries[1]["offset"] != len(first_line) or entries[1]["type"] != "whistle-peak" or entries[1]["startSample"] != 256:
            raise RuntimeError(f"second index entry mismatch: {entries[1]}")
        if entries[1].get("endSample") != 300:
            raise RuntimeError(f"endSample was not preserved: {entries[1]}")
        if entries[1].get("sourceId") != "smoke-source" or entries[1].get("ownerId") != "owner-a" or entries[1].get("tenantId") != "tenant-a":
            raise RuntimeError(f"second index metadata mismatch: {entries[1]}")

    print("Archive event index rebuild smoke passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
