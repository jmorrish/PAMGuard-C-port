# Ingest Status Service Endpoint

Date: 2026-07-01

## Purpose

Large live deployments need a single web/API place to check whether many stream ingest workers are running, pending, degraded, or stopped.

This checkpoint adds an optional authenticated engine-service endpoint:

```text
GET /ingest/status
```

## Configuration

Set the service environment variable to the status file written by `ops/ingest_supervisor.py`:

```powershell
$env:PAMGUARD_INGEST_STATUS_FILE = "C:\pamguard\data\ingest-status.json"
```

The supervisor keeps ownership of the status-file schema. The service only reads and projects it.

`GET /health` reports `ingestStatusEnabled` so deployment tooling can discover whether this projection is configured.

## Response

Successful response:

```json
{
  "configured": true,
  "exists": true,
  "status": {
    "schemaVersion": 2,
    "health": "healthy",
    "workerCount": 50,
    "workers": []
  }
}
```

If the environment variable is unset or the file does not exist, the endpoint returns `404` with `configured` and `exists` flags.

If the file exists but cannot be parsed as JSON, the endpoint returns `500`.

## Security

The endpoint uses the same API-key/Bearer auth as session and archive APIs. It does not expose launch commands or API keys; the supervisor status document deliberately omits those fields.

## Prometheus metrics

When `PAMGUARD_INGEST_STATUS_FILE` is configured, `/metrics` also projects the same status file into operational gauges:

- `pamguard_ingest_status_configured`
- `pamguard_ingest_status_file_exists`
- `pamguard_ingest_status_parse_error`
- `pamguard_ingest_workers`
- `pamguard_ingest_health_count{health="..."}`
- `pamguard_ingest_status_count{status="..."}`
- `pamguard_ingest_worker_health{source="...",session="...",status="...",health="..."}`
- `pamguard_ingest_worker_restarts{...}`
- `pamguard_ingest_worker_uptime_ms{...}`
- `pamguard_ingest_worker_last_observed_unix_ms{...}`

These metrics are intended for fleet dashboards and alerting across many live streams.

## Validation

`cpp-engine/scripts/service-smoke.ps1` now writes a temporary supervisor status file, starts the service with `PAMGUARD_INGEST_STATUS_FILE`, asserts that `/ingest/status` returns the expected healthy status document, and checks that `/metrics` includes ingest supervisor gauges.
