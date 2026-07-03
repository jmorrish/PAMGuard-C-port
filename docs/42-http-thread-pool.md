# HTTP Worker Thread Pool

Date: 2026-07-01

This checkpoint adds an explicit service concurrency control for larger deployments.

## Implemented

- Added `PAMGUARD_HTTP_THREADS`.
- When unset, the service keeps the cpp-httplib default task queue.
- When set, the service creates a fixed `httplib::ThreadPool`.
- `GET /health` reports `httpThreads`.

## Example

```powershell
$env:PAMGUARD_HTTP_THREADS = 32
.\build\pamguard_engine_service.exe 8080
```

## Scale note

For 50+ concurrent users/sources, this should be tuned with:

- `PAMGUARD_MAX_SESSIONS`;
- process CPU allocation;
- ingest chunk size;
- observed `pamguard_session_last_process_ms`;
- reverse-proxy timeout settings.
