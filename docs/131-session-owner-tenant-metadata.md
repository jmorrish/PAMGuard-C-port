# Session owner and tenant metadata

Date: 2026-07-01

## What changed

Engine session config now accepts optional metadata fields:

- `ownerId`
- `tenantId`

These fields are returned by:

- `POST /sessions`
- `GET /sessions`
- `GET /sessions/{sessionId}`

They are also exposed in the browser session form and session operation cards.

The station/array session templates and ingest source manifest now include example `ownerId` and `tenantId` values.

## Scope

These are metadata-only fields. They do not affect detector maths, array geometry, archive contents, or session routing inside the C++ engine.

## Why this matters

A production web tier with many users needs stable ownership tags for dashboards, routing, quota decisions, and audit trails.

The C++ engine remains deliberately simple: one configured analysis session per source/input, with optional metadata for higher-level orchestration.
