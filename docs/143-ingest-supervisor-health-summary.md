# Ingest supervisor health summary

Date: 2026-07-01

## Implemented

- Added status-file `schemaVersion` `2`.
- Added top-level supervisor health fields:
  - `health`
  - `workerCount`
  - `statusCounts`
  - `healthCounts`
- Added per-worker fields:
  - `health`
  - `lastObservedUnixMs`

## Health semantics

- `running` workers are `healthy`.
- `waiting_restart` and `exited` workers are `degraded`.
- `not_started` workers are `pending`.
- `stopped` workers are `stopped`.

The top-level `health` is:

- `healthy` when all workers are healthy.
- `degraded` when any worker is degraded.
- `pending` when all workers are pending.
- `stopped` when all workers are stopped.
- `mixed` for other non-degraded mixed states.
- `empty` when no workers are configured.

## Validation

- Added `ops/ingest_supervisor_status_smoke.py`.
- Registered `ingest_supervisor_status_smoke` in CTest when Python is available.
