# Readiness endpoint

The service now exposes:

```text
GET /ready
```

This is a capacity-aware readiness check for load balancers and container orchestrators.

Response fields:

- `ok`
- `ready`
- `sessions`
- `maxSessions`
- `capacityAvailable`

When `PAMGUARD_MAX_SESSIONS=0`, capacity is unlimited and readiness remains true. When a positive session cap is reached, `/ready` returns HTTP `503` while `/health` continues to return HTTP `200` for liveness.
