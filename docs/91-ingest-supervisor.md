# Active-project ingest supervisor

Updated: 2026-07-25

`ops/ingest_supervisor.py` runs one FFmpeg worker per external audio source and
feeds an existing Sound Acquisition controlled-unit instance in the active
PAMGuard project.

The production path is:

```text
source
  -> shell-free FFmpeg argv
  -> POST /v1/projects/active/acquisitions/{acquisitionUnitId}/pcm-f32le
  -> selected Acquisition raw-audio block
```

It does not create an `AnalysisSession`, inspect a generated runtime node ID, or
write through `/sessions/**`.

## Before starting

1. Save or open the intended project in the engine.
2. Record its stable `projectId`.
3. Record each Sound Acquisition unit's stable `unitId` from
   `GET /v1/projects/active/acquisitions`.
4. Make sure each configured sample rate/channel count matches that Acquisition.
5. Start the project runtime.

The supervisor verifies all five facts and rejects a target whose built-in host
capture is already running before FFmpeg starts. Every PCM request contains the
configured `expectedProjectId` and discovered `expectedWorkingRevision`. A
project switch or edit therefore fences the old worker instead of letting it
write into a newly configured runtime.

## Production manifest

`platform/ingest-sources.example.json` is schema version 2 and defaults to
`active-project` mode. Replace its example UUIDs and enable only the sources
you intend to run.

Required target fields are:

- top-level or per-source `projectId` (lowercase UUIDv4);
- per-source `acquisitionUnitId` (lowercase UUIDv4);
- per-source `id`; and
- per-source `source`.

Session fields such as `sessionId`, `sessionConfig`, `ownerId`,
`allowExistingSession`, and `resumeFromEngine` are rejected in this mode.
`ingestExecutable` is also rejected because the old bridge cannot address the
stable project-Acquisition endpoint.

Run:

```powershell
python .\ops\ingest_supervisor.py `
  --config .\platform\ingest-sources.example.json
```

Static validation and redacted command preview:

```powershell
python .\ops\ingest_supervisor.py `
  --config .\platform\ingest-sources.example.json `
  --validate

python .\ops\ingest_supervisor.py `
  --config .\platform\ingest-sources.example.json `
  --dry-run
```

Validation does not mutate or start the engine. Each real worker re-reads the
active Acquisition list immediately before launching FFmpeg.

## Timeline cursor

After every accepted PCM chunk, a worker atomically records:

- project ID;
- Acquisition unit ID;
- working revision; and
- next start sample.

`cursorDirectory` must be writable and durable across worker restarts. A cursor
is reused only for the exact project/unit/revision tuple. A project revision
change resets to the configured `startSample` (zero by default), which matches
the newly prepared runtime.

Run only one worker replica for a given Acquisition unit. Multiple writers
would race the same sample timeline. The supervisor rejects duplicate enabled
targets within one manifest, and refuses to reset silently from a corrupt
cursor.

Graceful restarts resume from the durable cursor. This is not a server-backed
exactly-once protocol: a hard host/process loss in the narrow interval after
the engine accepts a chunk but before the cursor replacement completes may
replay that final chunk. Server-side idempotency or a stable server-reported
next-sample cursor is still required to close that crash window.

## Process and restart model

The supervisor starts FFmpeg with an argument vector, never a shell command.
Source text and FFmpeg input options remain individual arguments. The internal
worker can restart FFmpeg while preserving its sample cursor; the outer
supervisor restarts the entire worker after process or API failures.

API keys should use `apiKeyEnv`. Literal `apiKey` remains supported but is
redacted from dry-run and launch logs. Source URL user-info and query strings
are also removed from logs and status metadata; the actual worker argument
still contains the configured source needed by FFmpeg.

## Status document

Schema version 3 identifies the stable target:

```json
{
  "schemaVersion": 3,
  "health": "healthy",
  "workerCount": 1,
  "workers": [
    {
      "sourceId": "station-001",
      "targetMode": "active-project",
      "projectId": "11111111-1111-4111-8111-111111111111",
      "acquisitionUnitId": "22222222-2222-4222-8222-222222222222",
      "compatibilitySessionId": null,
      "status": "running",
      "health": "healthy",
      "pid": 1234,
      "restarts": 0
    }
  ]
}
```

Full commands and API keys are never written to the status file. URL
credentials/query strings are removed from the displayed `source`. The engine
can project this document through `GET /ingest/status` when
`PAMGUARD_INGEST_STATUS_FILE` points to the same file.

## Deprecated session compatibility

`platform/ingest-sources.legacy-session-compat.example.json` is deliberately
named and selects:

```json
{"schemaVersion": 1, "mode": "legacy-session-compatibility"}
```

Only that mode accepts `sessionId`, `sessionConfig`, and `ingestExecutable`, and
only that mode launches `ffmpeg_stream_ingest --session`. It exists for legacy
test/archive workflows and is not the production default.

## Validation

CTest covers:

- strict manifest/command expansion and secret redaction;
- project/unit discovery and audio-shape validation;
- working-revision PCM requests with no session/runtime-node ID;
- revision-fenced cursor persistence; and
- schema-v3 status health summaries.
