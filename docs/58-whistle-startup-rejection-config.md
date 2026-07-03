# Whistle Startup Rejection Configuration

Date: 2026-07-01

This checkpoint exposes the connected-region startup rejection guard.

## Implemented

- Added `whistle.rejectFirstQuarterSecond` to service parsing.
- Session status reports the stored value.
- Browser console includes a startup rejection selector.
- OpenAPI and `station-session.example.json` document the field.

## Why this matters

The connected-region tracker can reject detections in the first quarter second to avoid startup transients. Operators now control that behaviour per session, which is useful for offline files where detections may legitimately begin at sample zero.
