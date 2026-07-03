# Owner and tenant archive propagation

Date: 2026-07-01

## What changed

Live PCM responses, flush responses, result archive records, and projected detector-event archive records now carry session metadata:

- `sourceId`
- `ownerId`
- `tenantId`

Empty owner/tenant values are returned as `null`.

Detector-event projection treats missing or `null` metadata as empty metadata rather than failing archive writes.

## Why this matters

Archive consumers and web-tier audit views need ownership context without joining every detector event back to a separate session registry.

These fields remain metadata-only and do not affect detector maths.
