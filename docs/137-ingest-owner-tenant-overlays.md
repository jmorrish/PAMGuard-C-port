# Ingest owner and tenant overlays

Date: 2026-07-01

## What changed

`ffmpeg_stream_ingest` now accepts:

- `--owner-id <id>`
- `--tenant-id <id>`

When `--session-config` is used, these values overlay `ownerId` and `tenantId` in the posted session JSON.

`ops/ingest_supervisor.py` passes these flags when `ownerId` or `tenantId` are present in the source manifest or defaults.

## Why this matters

Deployments can reuse station/array session templates while stamping ownership from the source manifest.

This avoids duplicating full detector configuration files just to change user/project metadata.

## Validation

The FFmpeg ingest bridge target compiled after adding the flags.

The supervisor dry-run path was checked with an enabled source manifest and emitted both `--owner-id` and `--tenant-id`.
