# Ingest supervisor health summary

Updated: 2026-07-25

The status-file schema is version 3. It retains the aggregate fields:

- `health`;
- `workerCount`;
- `statusCounts`; and
- `healthCounts`.

Per-worker health and process fields are unchanged, while target identity is
now `targetMode`, `projectId`, `acquisitionUnitId`, and the nullable
`compatibilitySessionId`.

## Health semantics

- `running` workers are `healthy`.
- `waiting_restart` and `exited` workers are `degraded`.
- `not_started` workers are `pending`.
- `stopped` workers are `stopped`.

Top-level `health` is `healthy` when all workers are healthy, `degraded` when
any worker is degraded, `pending` when all are pending, `stopped` when all are
stopped, `mixed` for other non-degraded combinations, and `empty` when no
workers are configured.

`ops/ingest_supervisor_status_smoke.py` covers the aggregate calculation and
both the active-project and named legacy target metadata.
