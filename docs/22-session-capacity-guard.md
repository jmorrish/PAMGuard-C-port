# Session Capacity Guard

Date: 2026-06-30

This checkpoint adds a simple service-level capacity guard for multi-user/source deployments.

## Implemented

The service reads:

```text
PAMGUARD_MAX_SESSIONS
```

Behaviour:

- unset or `0`: unlimited sessions in this process;
- positive integer: reject new session creation once that many sessions are active;
- rejection status: HTTP `429`;
- health response includes `maxSessions`.

## Validation

CTest status after this checkpoint:

```text
21/21 tests passed
```

HTTP smoke test with `PAMGUARD_MAX_SESSIONS=1`:

```json
{"sessions":1,"maxSessions":1,"secondStatus":429}
```

## Notes

This is a process-level guardrail, not a complete scheduler. A production deployment should combine this with orchestration-level autoscaling, per-tenant quotas, and external admission control.
