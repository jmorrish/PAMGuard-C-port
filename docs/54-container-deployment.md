# Container Deployment

Date: 2026-07-01

This checkpoint adds a repeatable Linux container build for the C++ engine service and FFmpeg ingest bridge.

## Added

- `Dockerfile.engine`
- `docker-compose.engine.yml`
- `.dockerignore`

## Image contents

The runtime image includes:

- `pamguard_engine_service`;
- `ffmpeg_stream_ingest`;
- FFmpeg;
- Python 3 for the optional ingest supervisor;
- `station-session.example.json`;
- `array-session.example.json`;
- `ingest-sources.example.json`;
- `openapi.yaml`;
- `ops/ingest_supervisor.py`;
- `ops/archive_retention.py`;
- static browser UI at `/app/web-ui/index.html`.

## Build

```powershell
docker build -f Dockerfile.engine -t pamguard-engine:local .
```

## Run

```powershell
docker compose -f docker-compose.engine.yml up --build
```

The compose file maps:

```text
http://localhost:8080
```

## Deployment knobs

```text
PAMGUARD_HTTP_THREADS
PAMGUARD_MAX_SESSIONS
PAMGUARD_MAX_PCM_BODY_BYTES
PAMGUARD_MAX_ARCHIVE_QUERY_RECORDS
PAMGUARD_CORS_ORIGIN
PAMGUARD_SESSION_CONFIG_DIR
PAMGUARD_RESULT_ARCHIVE_DIR
PAMGUARD_WEB_UI_FILE
PAMGUARD_OPENAPI_FILE
PAMGUARD_API_KEY
PAMGUARD_API_KEY_FILE
PAMGUARD_REQUIRE_SESSION_METADATA
PAMGUARD_INGEST_STATUS_FILE
PAMGUARD_AUDIT_LOG_FILE
```

## Ingest worker inside the image

```powershell
docker run --rm pamguard-engine:local /app/ffmpeg_stream_ingest `
  --source "http://station.example/live.mp3" `
  --engine "http://host.docker.internal:8080" `
  --session "station-001" `
  --session-config "/app/station-session.example.json" `
  --allow-existing-session `
  --sample-rate 48000 `
  --channels 2 `
  --restart
```

## Ingest supervisor inside the image

```powershell
docker run --rm pamguard-engine:local python3 /app/ops/ingest_supervisor.py `
  --config /app/ingest-sources.example.json `
  --dry-run
```

## Remaining deployment work

- Split service and ingest worker into separate deployment roles if desired.
- Add production TLS/reverse-proxy manifests.
- Expand the starter Kubernetes manifest into production overlays and autoscaling policy once load targets are measured.
