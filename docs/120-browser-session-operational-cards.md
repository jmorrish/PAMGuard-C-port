# Browser session operational cards

Date: 2026-07-01

## What changed

The browser dashboard now renders a `Session operations` panel from `GET /sessions`.

Each session card shows:

- Session ID.
- Source ID.
- Activity state.
- Chunk count.
- Idle time.
- Continuity state.
- Total detector outputs.
- Channel count.
- Next expected sample.
- Mean processing time.
- Timeline health.

The raw `/sessions` response is still written to the log for debugging.

If `Owner ID` or `Tenant ID` is filled in the session form, the `List sessions` button applies those filters to `GET /sessions`.

## Why this matters

For a multi-user deployment with many live streams, operators need a quick way to see which sessions are receiving audio, which are idle, and whether sample continuity is clean.

This is still an engineering dashboard, not the final analyst UI, but it now exposes the service-level operational signals needed for scale testing.
