# Click localisation readiness status

Date: 2026-07-01

## Implemented

`GET /sessions/{sessionId}` and session-create/status JSON now expose:

```json
{
  "array": {
    "clickLocalisationReadiness": {
      "enabled": true,
      "mode": "geometry-constrained",
      "geometryComplete": true,
      "bearingEnabled": true,
      "delayLimitMode": "geometry-constrained",
      "clickChannels": [0, 1],
      "hydrophoneChannels": [0, 1],
      "matchedClickHydrophoneChannels": [0, 1],
      "missingClickHydrophoneChannels": []
    }
  }
}
```

## Semantics

- `geometry-constrained`: all active click channels have hydrophone geometry, so delay limits can be derived from the array.
- `partial-geometry`: at least two active click channels have hydrophone geometry, but one or more active click channels are missing.
- `delay-only-unconstrained`: click localisation can estimate delays, but array geometry is insufficient for geometry-constrained delay limits or bearings.
- `disabled`: click localisation is not enabled.
- `invalid-click-channel-count`: click localisation was requested with fewer than two click channels. Service validation should normally reject this before a session is created.

## Validation

The HTTP service smoke now asserts the default two-channel smoke session reports `geometry-constrained` readiness with no missing hydrophone channels.

The browser session cards show the readiness mode and any missing hydrophone channels for quick operator inspection.
