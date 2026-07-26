# Container deployment

Updated: 2026-07-25

`Dockerfile.engine` builds the project-authoritative C++ service and packages
the tools needed to run supervised external audio ingest.

## Image contents

The runtime image includes:

- `pamguard_engine_service`;
- FFmpeg and Python 3;
- `ops/ingest_supervisor.py`;
- the schema-v2 active-project manifest at
  `/app/ingest-sources.example.json`;
- `openapi.yaml` and the browser UI; and
- explicitly retained compatibility assets:
  `ffmpeg_stream_ingest`, the session templates, and
  `ingest-sources.legacy-session-compat.example.json`.

The compatibility assets are not the default production workflow.

## Build and run the engine

```powershell
docker build -f Dockerfile.engine -t pamguard-engine:local .
docker compose -f docker-compose.engine.yml up --build
```

The compose example serves:

```text
http://localhost:8080
```

It persists saved projects under `/data/projects`. Set
`PAMGUARD_ACTIVE_PROJECT_ID` to the UUID of a saved project when the service
must reopen that project automatically:

```powershell
$env:PAMGUARD_ACTIVE_PROJECT_ID = "replace-with-saved-project-uuid"
docker compose -f docker-compose.engine.yml up --build
```

## Production ingest worker

Create a schema-v2 manifest from `platform/ingest-sources.example.json`.
Replace its `projectId` and each `acquisitionUnitId` with stable IDs returned by
`GET /v1/projects/active/acquisitions`; also make its `engine` URL reachable
from the worker container. Do not target an Acquisition whose built-in capture
is already running. For the command below, set `statusFile` and
`cursorDirectory` under `/var/run/pamguard-ingest`.

Validate a mounted manifest without starting FFmpeg:

```powershell
docker run --rm `
  -v "${PWD}\ingest-sources.json:/etc/pamguard/ingest.json:ro" `
  -v "pamguard-ingest-state:/var/run/pamguard-ingest" `
  pamguard-engine:local `
  python3 /app/ops/ingest_supervisor.py `
  --config /etc/pamguard/ingest.json `
  --validate
```

Remove `--validate` to run. Persist both `statusFile` and `cursorDirectory`, and
pass the configured API-key environment variable when authentication is
enabled. Run exactly one worker for each Acquisition unit. The active project
must be open and its runtime started before ingest begins.

The legacy session bridge can still be exercised with the deliberately named
`/app/ingest-sources.legacy-session-compat.example.json`; it must not be used
as the production/default example.

## Relevant deployment knobs

```text
PAMGUARD_HTTP_THREADS
PAMGUARD_MAX_PCM_BODY_BYTES
PAMGUARD_MAX_ARCHIVE_QUERY_RECORDS
PAMGUARD_CORS_ORIGIN
PAMGUARD_PROJECT_DIR
PAMGUARD_ACTIVE_PROJECT_ID
PAMGUARD_WEB_UI_FILE
PAMGUARD_OPENAPI_FILE
PAMGUARD_API_KEY
PAMGUARD_API_KEY_FILE
PAMGUARD_INGEST_STATUS_FILE
PAMGUARD_AUDIT_LOG_FILE
```

TLS still belongs at the ingress or reverse proxy.
