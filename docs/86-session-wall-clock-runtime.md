# Session wall-clock runtime fields

Runtime session JSON now includes service wall-clock timestamps:

- `createdUnixMs`
- `lastReceiveUnixMs`

Prometheus also exposes:

- `pamguard_session_created_unix_ms`
- `pamguard_session_last_receive_unix_ms`

These are host/service timestamps in Unix milliseconds. They are intentionally separate from audio-domain fields such as `startSample`, `timeMs`, and detector event timestamps.

Use `lastReceiveUnixMs` to detect stale live streams. A value of `0` means the session has not received any PCM chunks yet.
