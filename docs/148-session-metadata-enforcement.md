# Session Metadata Enforcement

Date: 2026-07-01

## Purpose

Multi-user deployments should avoid anonymous analysis sessions. The detector maths does not use ownership metadata, but the web/API layer needs `ownerId` and `tenantId` for filtering, audit, retention, and access-control integration.

## Configuration

Enable strict metadata enforcement:

```powershell
$env:PAMGUARD_REQUIRE_SESSION_METADATA = "1"
```

When enabled:

- `POST /sessions` rejects new sessions missing `ownerId` or `tenantId`.
- Persisted session configs missing `ownerId` or `tenantId` are skipped at service startup.
- `GET /health` reports `sessionMetadataRequired`.

When unset, local development remains permissive and metadata is optional.

## Validation

`cpp-engine/scripts/service-smoke.ps1` now starts the service with enforcement enabled, asserts the health flag, verifies that a metadata-less session is rejected with `400`, and then creates the normal smoke session with `ownerId` and `tenantId`.

The Kubernetes engine example enables `PAMGUARD_REQUIRE_SESSION_METADATA`, and the example ingest worker passes `--owner-id` and `--tenant-id` when bootstrapping its session.
