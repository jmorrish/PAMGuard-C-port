# Service Audit Log

Date: 2026-07-01

## Purpose

Enterprise deployments need a durable trail of session lifecycle decisions without changing detector maths or storing audio payloads.

## Configuration

Enable append-only JSONL audit logging:

```powershell
$env:PAMGUARD_AUDIT_LOG_FILE = "C:\pamguard\data\audit.ndjson"
```

When configured, `GET /health` reports `auditLogEnabled`.

The Kubernetes engine manifest includes a commented `PAMGUARD_AUDIT_LOG_FILE` example for deployments that want audit JSONL retained on the engine data volume.

## Events

The current audit log records:

- `session_create`
- `session_create_rejected`
- `session_flush`
- `session_delete`

Events include `timeMs`, `sessionId`, `sourceId`, `ownerId`, `tenantId`, and event-specific fields such as rejection reason or delete result.

The log deliberately excludes API keys, PCM bodies, detector payloads, and FFmpeg launch commands.

Audit writes are best-effort: failures are reported to stderr but do not block live detector processing.

## Validation

`cpp-engine/scripts/service-smoke.ps1` now enables `PAMGUARD_AUDIT_LOG_FILE`, asserts the health flag, and verifies the expected lifecycle audit events were written.
