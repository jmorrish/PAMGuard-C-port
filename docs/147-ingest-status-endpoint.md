# Ingest status service endpoint

Updated: 2026-07-25

`GET /ingest/status` projects the JSON status file written by
`ops/ingest_supervisor.py`.

Configure the service with:

```powershell
$env:PAMGUARD_INGEST_STATUS_FILE = "C:\pamguard\data\ingest-status.json"
```

The supervisor owns the schema; the service only reads and returns it.
`GET /health` reports `ingestStatusEnabled`.

## Response

The current production supervisor writes schema version 3:

```json
{
  "configured": true,
  "exists": true,
  "status": {
    "schemaVersion": 3,
    "health": "healthy",
    "workerCount": 1,
    "workers": [
      {
        "sourceId": "station-001",
        "targetMode": "active-project",
        "projectId": "11111111-1111-4111-8111-111111111111",
        "acquisitionUnitId": "22222222-2222-4222-8222-222222222222",
        "compatibilitySessionId": null
      }
    ]
  }
}
```

An unset or missing file returns `404` with `configured` and `exists` flags.
Malformed JSON returns `500`.

The endpoint uses the same API-key/Bearer authentication as the rest of the
service. Supervisor status omits commands and secrets.

## Prometheus projection

`/metrics` projects worker counts, health, restart counts, uptime, and last
observation time. The existing metric label remains named `session` for
backward dashboard compatibility. Schema-v3 status no longer emits that old
field, so the label is empty; use the JSON endpoint's stable project/unit IDs
until the metrics contract receives a separately versioned label migration.

The service smoke covers status-file projection and the operational gauges.
