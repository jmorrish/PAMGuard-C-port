# Container healthcheck

Date: 2026-07-01

## What changed

`Dockerfile.engine` now defines a Docker `HEALTHCHECK`.

The check uses Python's standard library to call:

```text
http://127.0.0.1:8080/health
```

The container is healthy when the JSON response contains `ok: true`.

## Design note

This is a liveness-style check for container runtimes.

Kubernetes deployments should continue using:

- `/health` for liveness.
- `/ready` for capacity-aware readiness.
