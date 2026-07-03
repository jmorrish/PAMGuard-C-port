# FFmpeg Source ID Overlay

Date: 2026-07-01

## Purpose

Enterprise deployments often distinguish a stable source ID from a session ID. For example, a hydrophone station may keep `sourceId=harbour-array` while creating dated or tenant-scoped session IDs.

## Bridge option

`ffmpeg_stream_ingest` now supports:

```text
--source-id <id>
```

When `--session-config` is used, the bridge overlays `sourceId` before posting `POST /sessions`.

## Supervisor behavior

`ops/ingest_supervisor.py` passes the manifest `id` as `--source-id`, so the engine preserves source identity even when `session` / `sessionId` differs.

## Validation

`ops/ingest_supervisor_command_smoke.py` asserts that manifest source IDs expand into `--source-id`. Registered FFmpeg help coverage also asserts that the bridge exposes the option.
