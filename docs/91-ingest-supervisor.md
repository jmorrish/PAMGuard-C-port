# Multi-Source Ingest Supervisor

Date: 2026-07-01

This checkpoint adds an operations supervisor for running many `ffmpeg_stream_ingest` workers.

## Added

- `ops/ingest_supervisor.py`
- `platform/ingest-sources.example.json`

The supervisor reads a JSON source list and launches one ingest bridge process per enabled source.

## Model

The deployment unit remains:

```text
one source/input -> one ffmpeg_stream_ingest process -> one engine session
```

This is the cleanest scaling boundary for PAMGuard-style stateful detectors because FFT state, click train tracking, whistle region state, and localisation timelines remain isolated per source/session.

## Example

Edit `platform/ingest-sources.example.json`, enable the required sources, then run:

```powershell
python .\ops\ingest_supervisor.py --config .\platform\ingest-sources.example.json
```

Dry run:

```powershell
python .\ops\ingest_supervisor.py --config .\platform\ingest-sources.example.json --dry-run
```

Validate enabled sources without launching workers:

```powershell
python .\ops\ingest_supervisor.py --config .\platform\ingest-sources.example.json --validate
```

## Config highlights

- `engine`: service URL.
- `ingestExecutable`: path to `ffmpeg_stream_ingest`.
- `ffmpeg`: FFmpeg executable path or command name.
- `apiKeyEnv`: environment variable name passed to `ffmpeg_stream_ingest --api-key-env`.
- `statusFile`: optional JSON status file for monitoring.
- `statusIntervalSeconds`: status write cadence.
- `defaults`: shared bridge flags.
- `sources`: source-specific overrides.

Source fields:

- `enabled`
- `id`
- `session`
- `source`
- `sessionConfig`
- `sampleRateHz`
- `channels`
- `chunkFrames`
- `ffmpegInputOptions`
- `audioFilter`

## Restart policy

The ingest bridge already restarts FFmpeg internally when `--restart` is enabled.

The supervisor adds a second layer: if the entire bridge process exits, the supervisor can restart that worker after `workerRestartDelaySeconds`.

## Status file

When `statusFile` is configured, the supervisor writes JSON like:

```json
{
  "schemaVersion": 2,
  "generatedUnixMs": 1782921600000,
  "health": "healthy",
  "workerCount": 1,
  "statusCounts": {
    "running": 1,
    "waiting_restart": 0,
    "not_started": 0,
    "exited": 0,
    "stopped": 0
  },
  "healthCounts": {
    "healthy": 1,
    "degraded": 0,
    "pending": 0,
    "stopped": 0
  },
  "workers": [
    {
      "sourceId": "station-001",
      "status": "running",
      "health": "healthy",
      "pid": 1234,
      "restarts": 0,
      "uptimeMs": 1000,
      "lastObservedUnixMs": 1782921600000,
      "lastStartUnixMs": 1782921599000,
      "lastExitUnixMs": null,
      "lastExitCode": null,
      "nextStartUnixMs": null
    }
  ]
}
```

The status file deliberately omits full launch commands and API keys.

Console command previews and launch logs redact literal `--api-key` values so dry-run/preflight output can be captured safely in deployment logs. Prefer `apiKeyEnv` so worker process arguments carry only the environment variable name, not the secret value.

The supervisor passes the manifest `id` to `ffmpeg_stream_ingest --source-id`, so engine session metadata preserves source identity even when `session` / `sessionId` differs from the source ID.

## Service endpoint

When the engine service is started with `PAMGUARD_INGEST_STATUS_FILE` pointing at the same status file, the authenticated endpoint `GET /ingest/status` exposes the supervisor status to API clients and the browser dashboard.

The endpoint wraps the supervisor document so the status-file schema remains stable:

```json
{
  "configured": true,
  "exists": true,
  "status": {
    "schemaVersion": 2,
    "health": "healthy",
    "workerCount": 1,
    "workers": []
  }
}
```

If the environment variable is unset or the file is missing, the endpoint returns `404` with `configured`/`exists` flags instead of leaking launch commands or secrets.

## Validation

The supervisor has been syntax-checked with Python, dry-run tested against an enabled temporary source manifest, has CTest coverage for status health summary generation and manifest command expansion, and service smoke coverage for the optional `/ingest/status` projection.
