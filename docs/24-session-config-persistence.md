# Session Config Persistence

Date: 2026-07-01

This checkpoint adds opt-in persistence for session configuration JSON.

## Implemented

The service reads:

```text
PAMGUARD_SESSION_CONFIG_DIR
```

When set:

- `POST /sessions` writes the original session-create JSON to `<sessionId>.json`.
- Service startup reloads all `*.json` session configs in the directory.
- `DELETE /sessions/{sessionId}` removes the persisted config file.

## Scope

This persists session configuration, not live detector/audio state. After a service restart, sessions are recreated with fresh detector state and can receive new PCM chunks.

## Validation

CTest status after this checkpoint:

```text
22/22 tests passed
```

Persistence smoke test:

```json
{"filesAfterCreate":1,"reloaded":true,"sourceId":"persist-smoke","filesAfterDelete":0}
```

## Remaining persistence work

- Persist detector results.
- Persist runtime counters and stream health history.
- Add migrations/versioned config schemas.
- Add database-backed storage for multi-service deployments.
