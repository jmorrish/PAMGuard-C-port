# Container ingest supervisor assets

Updated: 2026-07-25

The engine image packages:

- Python 3 and FFmpeg;
- `/app/ops/ingest_supervisor.py`;
- `/app/ingest-sources.example.json`, the schema-v2 active-project example;
- `/app/ingest-sources.legacy-session-compat.example.json`, the explicitly
  named schema-v1 compatibility example; and
- `/app/ops/archive_retention.py`.

The default image command remains:

```text
/app/pamguard_engine_service 8080
```

Supervisor use is opt-in. Static validation is:

```powershell
docker run --rm pamguard-engine:local `
  python3 /app/ops/ingest_supervisor.py `
  --config /app/ingest-sources.example.json `
  --validate
```

For real ingest, mount a deployment-specific schema-v2 manifest and durable
cursor/status storage. Its `projectId` and `acquisitionUnitId` values must
identify the active, running project and Sound Acquisition instances.
