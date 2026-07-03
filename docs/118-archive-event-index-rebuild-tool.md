# Archive event index rebuild tool

Date: 2026-07-01

## Purpose

`ops/rebuild_archive_event_index.py` rebuilds detector-event index sidecars from canonical detector-event archive files.

It reads:

- `<session>.events.ndjson`

And writes:

- `<session>.events.index.ndjson`

## Example usage

```powershell
python .\ops\rebuild_archive_event_index.py --archive-dir .\runtime\archive --dry-run
python .\ops\rebuild_archive_event_index.py --archive-dir .\runtime\archive --force
```

For a single safe archive session filename prefix:

```powershell
python .\ops\rebuild_archive_event_index.py --archive-dir .\runtime\archive --session smoke-session --force
```

## Design notes

The tool computes byte offsets directly from the canonical `.events.ndjson` file and writes compact JSON index lines with `schemaVersion`, `offset`, `type`, `startSample`, optional `endSample`, optional `channelGroup`, and optional `sourceId` / `ownerId` / `tenantId` metadata.

The rebuild is atomic per index file: it writes a temporary file in the archive directory and then replaces the target index.

Existing indexes are skipped by default. Use `--force` to replace them.
