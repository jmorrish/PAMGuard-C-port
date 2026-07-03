# Ingest supervisor status metadata

Date: 2026-07-01

## What changed

`ops/ingest_supervisor.py` status files now include a top-level status summary:

- `schemaVersion`
- `health`
- `workerCount`
- `statusCounts`
- `healthCounts`

`ops/ingest_supervisor.py` status files now include non-secret source/session metadata for each worker:

- `sourceId`
- `sessionId`
- `ownerId`
- `tenantId`
- `engine`
- `source`
- `sampleRateHz`
- `channelCount`
- `chunkFrames`
- `uptimeMs`

Process status fields are still present:

- `status`
- `health`
- `pid`
- `restarts`
- `lastStartUnixMs`
- `lastObservedUnixMs`
- `lastExitUnixMs`
- `lastExitCode`
- `nextStartUnixMs`

## Security note

The status file intentionally does not include the full worker command, because the command may contain API keys.

## Why this matters

For a deployment with many Icecast/BUTT/direct Ethernet streams, operators need to correlate a worker process with the engine session it feeds and identify restart loops quickly.
