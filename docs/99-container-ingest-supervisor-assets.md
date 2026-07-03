# Container Ingest Supervisor Assets

Date: 2026-07-01

This checkpoint updates the engine container packaging to include the multi-source ingest supervisor.

## Image additions

- `python3`
- `/app/ops/ingest_supervisor.py`
- `/app/ops/archive_retention.py`
- `/app/ingest-sources.example.json`

The default container command remains:

```text
/app/pamguard_engine_service 8080
```

Supervisor use is opt-in:

```powershell
docker run --rm pamguard-engine:local python3 /app/ops/ingest_supervisor.py --config /app/ingest-sources.example.json --dry-run
```
