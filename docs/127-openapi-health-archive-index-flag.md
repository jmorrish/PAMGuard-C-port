# OpenAPI health archive index flag

Date: 2026-07-01

## What changed

The OpenAPI contract for `GET /health` now includes:

- `archiveEventIndexEnabled`

This matches the C++ service health response, where the flag is true when result archiving is configured and detector-event index sidecars are enabled.
