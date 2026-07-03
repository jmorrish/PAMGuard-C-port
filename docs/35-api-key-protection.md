# API Key Protection

Date: 2026-07-01

This checkpoint adds an optional first security boundary for enterprise deployments.

## Implemented

- `pamguard_engine_service` reads `PAMGUARD_API_KEY` or, if that is unset, `PAMGUARD_API_KEY_FILE`.
- When unset, local development remains unauthenticated.
- When set, all session and PCM endpoints require either:
  - `X-API-Key: <key>`;
  - `Authorization: Bearer <key>`.
- `GET /health` remains public and reports `authRequired`.
- CORS preflight allows `Authorization` and `X-API-Key`.
- `ffmpeg_stream_ingest` can send an API key via:
  - `--api-key <key>`;
  - `--api-key-env <name>`;
  - or the same `PAMGUARD_API_KEY` environment variable.
- The browser console includes an optional API-key field and sends `X-API-Key` when filled in.
- The OpenAPI contract declares `ApiKeyAuth` and `BearerAuth` on protected low-level service endpoints.

## Example

```powershell
$env:PAMGUARD_API_KEY = "change-me"
.\build\pamguard_engine_service.exe 8080

.\build\ffmpeg_stream_ingest.exe `
  --source "http://station.example/live.mp3" `
  --engine "http://127.0.0.1:8080" `
  --session "station-001" `
  --sample-rate 48000 `
  --channels 2 `
  --api-key-env "PAMGUARD_API_KEY"
```

Mounted-secret style:

```powershell
Set-Content -NoNewline -Path .\api-key.txt -Value "change-me"
$env:PAMGUARD_API_KEY_FILE = (Resolve-Path .\api-key.txt)
.\build\pamguard_engine_service.exe 8080
```

For ingest workers, prefer `--api-key-env PAMGUARD_API_KEY` over literal `--api-key` so process arguments carry only the environment variable name.

## Production note

This is a deployment guard, not a full identity system. A production multi-user platform still needs tenant ownership, user auth, source ownership checks, TLS, and audit logging.
