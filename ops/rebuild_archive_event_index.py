#!/usr/bin/env python3
"""Rebuild detector-event archive indexes from canonical event sidecars."""

from __future__ import annotations

import argparse
import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any


EVENT_SUFFIX = ".events.ndjson"
INDEX_SUFFIX = ".events.index.ndjson"


@dataclass
class RebuildResult:
    event_path: Path
    index_path: Path
    event_count: int
    skipped: bool
    reason: str


def is_non_negative_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def index_entry_for_event(event: Any, offset: int) -> dict[str, Any] | None:
    if not isinstance(event, dict):
        return None
    event_type = event.get("type")
    start_sample = event.get("startSample")
    if not isinstance(event_type, str) or not is_non_negative_int(start_sample):
        return None

    entry: dict[str, Any] = {
        "schemaVersion": 1,
        "offset": offset,
        "type": event_type,
        "startSample": start_sample,
    }

    end_sample = event.get("endSample")
    if is_non_negative_int(end_sample):
        entry["endSample"] = end_sample

    channel_group = event.get("channelGroup")
    if isinstance(channel_group, str):
        entry["channelGroup"] = channel_group

    source_id = event.get("sourceId")
    if isinstance(source_id, str):
        entry["sourceId"] = source_id

    owner_id = event.get("ownerId")
    if isinstance(owner_id, str):
        entry["ownerId"] = owner_id

    tenant_id = event.get("tenantId")
    if isinstance(tenant_id, str):
        entry["tenantId"] = tenant_id

    return entry


def event_files(archive_dir: Path, session: str | None) -> list[Path]:
    if session:
        path = archive_dir / f"{session}{EVENT_SUFFIX}"
        return [path] if path.exists() else []
    return sorted(path for path in archive_dir.glob(f"*{EVENT_SUFFIX}") if path.is_file())


def rebuild_one(event_path: Path, force: bool, dry_run: bool) -> RebuildResult:
    index_path = event_path.with_name(event_path.name[: -len(EVENT_SUFFIX)] + INDEX_SUFFIX)
    if index_path.exists() and not force:
        return RebuildResult(
            event_path=event_path,
            index_path=index_path,
            event_count=0,
            skipped=True,
            reason="index exists; use --force to rebuild",
        )

    event_count = 0
    temp_path = index_path.with_name(index_path.name + ".tmp")

    if dry_run:
        with event_path.open("rb") as source:
            while True:
                offset = source.tell()
                raw_line = source.readline()
                if not raw_line:
                    break
                if not raw_line.strip():
                    continue
                event = json.loads(raw_line.decode("utf-8"))
                if index_entry_for_event(event, offset) is not None:
                    event_count += 1
        return RebuildResult(
            event_path=event_path,
            index_path=index_path,
            event_count=event_count,
            skipped=False,
            reason="dry-run",
        )

    try:
        with event_path.open("rb") as source, temp_path.open("w", encoding="utf-8", newline="\n") as output:
            while True:
                offset = source.tell()
                raw_line = source.readline()
                if not raw_line:
                    break
                if not raw_line.strip():
                    continue
                event = json.loads(raw_line.decode("utf-8"))
                entry = index_entry_for_event(event, offset)
                if entry is None:
                    continue
                output.write(json.dumps(entry, separators=(",", ":")))
                output.write("\n")
                event_count += 1
        os.replace(temp_path, index_path)
    finally:
        if temp_path.exists():
            temp_path.unlink()

    return RebuildResult(
        event_path=event_path,
        index_path=index_path,
        event_count=event_count,
        skipped=False,
        reason="rebuilt",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Rebuild PAMGuard detector-event archive index sidecars.")
    parser.add_argument("--archive-dir", required=True, help="Directory containing .events.ndjson files.")
    parser.add_argument("--session", help="Safe archive session filename prefix to rebuild, without .events.ndjson.")
    parser.add_argument("--force", action="store_true", help="Replace existing indexes.")
    parser.add_argument("--dry-run", action="store_true", help="Read event files and print what would be rebuilt without writing.")
    args = parser.parse_args()

    archive_dir = Path(args.archive_dir).resolve()
    if not archive_dir.exists() or not archive_dir.is_dir():
        raise SystemExit(f"archive directory not found: {archive_dir}")

    files = event_files(archive_dir, args.session)
    if not files:
        print("No detector event sidecars found.")
        return 0

    mode = "DRY-RUN" if args.dry_run else "APPLY"
    print(f"{mode}: {len(files)} detector event sidecar(s)")
    for event_path in files:
        result = rebuild_one(event_path, force=args.force, dry_run=args.dry_run)
        status = "SKIP" if result.skipped else result.reason.upper()
        print(f"{status}: {result.event_path} -> {result.index_path} events={result.event_count} {result.reason}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
