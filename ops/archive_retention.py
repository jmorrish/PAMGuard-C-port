#!/usr/bin/env python3
"""Dry-run-first retention utility for PAMGuard result archives."""

from __future__ import annotations

import argparse
import time
from dataclasses import dataclass
from pathlib import Path


ARCHIVE_SUFFIXES = (".events.index.ndjson", ".events.ndjson", ".ndjson")


@dataclass
class Candidate:
    path: Path
    size_bytes: int
    mtime: float
    reason: str


@dataclass
class ArchiveGroup:
    key: str
    paths: list[Path]
    size_bytes: int
    mtime: float


def archive_key(path: Path) -> str | None:
    name = path.name
    for suffix in ARCHIVE_SUFFIXES:
        if name.endswith(suffix):
            return name[: -len(suffix)]
    return None


def archive_groups(archive_dir: Path) -> list[ArchiveGroup]:
    grouped: dict[str, list[Path]] = {}
    for path in archive_dir.iterdir():
        if not path.is_file():
            continue
        key = archive_key(path)
        if key is not None:
            grouped.setdefault(key, []).append(path)

    groups: list[ArchiveGroup] = []
    for key, paths in grouped.items():
        stats = [path.stat() for path in paths]
        groups.append(ArchiveGroup(
            key=key,
            paths=sorted(paths),
            size_bytes=sum(stat.st_size for stat in stats),
            mtime=max(stat.st_mtime for stat in stats),
        ))
    return sorted(groups, key=lambda item: item.mtime)


def retention_candidates(groups: list[ArchiveGroup], max_age_days: float | None, max_files: int | None) -> list[Candidate]:
    candidates: dict[Path, Candidate] = {}
    now = time.time()

    if max_age_days is not None:
        cutoff = now - max_age_days * 86400.0
        for group in groups:
            if group.mtime < cutoff:
                for path in group.paths:
                    stat = path.stat()
                    candidates[path] = Candidate(path=path, size_bytes=stat.st_size, mtime=stat.st_mtime, reason=f"group older than {max_age_days:g} days")

    if max_files is not None and max_files >= 0 and len(groups) > max_files:
        for group in groups[: len(groups) - max_files]:
            for path in group.paths:
                stat = path.stat()
                existing = candidates.get(path)
                reason = f"exceeds max archive groups {max_files}"
                if existing is not None:
                    reason = existing.reason + "; " + reason
                candidates[path] = Candidate(path=path, size_bytes=stat.st_size, mtime=stat.st_mtime, reason=reason)

    return sorted(candidates.values(), key=lambda item: item.mtime)


def main() -> int:
    parser = argparse.ArgumentParser(description="Prune PAMGuard archive files with dry-run defaults.")
    parser.add_argument("--archive-dir", required=True, help="Directory containing .ndjson archive files.")
    parser.add_argument("--max-age-days", type=float, help="Delete archive files older than this many days.")
    parser.add_argument("--max-files", dest="max_groups", type=int, help="Compatibility alias for --max-groups.")
    parser.add_argument("--max-groups", dest="max_groups", type=int, help="Keep only this many newest archive groups. A group contains .ndjson, .events.ndjson, and .events.index.ndjson sidecars for one session.")
    parser.add_argument("--apply", action="store_true", help="Actually delete candidates. Without this, only prints a dry-run plan.")
    args = parser.parse_args()

    archive_dir = Path(args.archive_dir).resolve()
    if not archive_dir.exists() or not archive_dir.is_dir():
        raise SystemExit(f"archive directory not found: {archive_dir}")
    if args.max_age_days is None and args.max_groups is None:
        raise SystemExit("provide --max-age-days and/or --max-groups")

    groups = archive_groups(archive_dir)
    candidates = retention_candidates(groups, args.max_age_days, args.max_groups)
    total_bytes = sum(candidate.size_bytes for candidate in candidates)

    mode = "APPLY" if args.apply else "DRY-RUN"
    print(f"{mode}: {len(candidates)} files selected, {total_bytes} bytes")
    for candidate in candidates:
        print(f"{candidate.path}\t{candidate.size_bytes}\t{candidate.reason}")

    if args.apply:
        for candidate in candidates:
            candidate.path.unlink()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
