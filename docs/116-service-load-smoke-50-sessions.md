# Service load smoke: 50 sessions

Date: 2026-07-01

## Result

The C++ HTTP service load smoke passed locally with `50` independent sessions and `2` PCM chunks per session.

Both modes passed:

- Unauthenticated local service mode.
- API-key protected service mode using `dev-secret`.

The same 50-session smoke was repeated after the archive index, operational status, localisation smoke, correlation-delay, metadata propagation, API-key-file, archive metadata filter, archive summary, click-train timing metric, whistle contour summary/schema, ingest supervisor health-summary, service audit, and stream ingest hardening updates; both modes still passed on the current build.

The load smoke now enables `PAMGUARD_REQUIRE_SESSION_METADATA=1` and creates each session with owner/tenant metadata.

## Commands

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\service-load-smoke.ps1 -Port 18085 -Sessions 50 -Chunks 2
powershell -ExecutionPolicy Bypass -File .\scripts\service-load-smoke.ps1 -Port 18086 -Sessions 50 -Chunks 2 -ApiKey dev-secret
```

## Interpretation

This is a functional multi-session isolation and service-path smoke test. It proves the current engine can create, feed, and query many independent sessions through the HTTP API in one process on this Windows build.

It is not yet a production throughput benchmark. Detector-heavy streams, long-running archive pressure, real Icecast/BUTT ingest, database indexing, and horizontal deployment still need dedicated load and soak testing.

## Relevance to the enterprise architecture

The result supports the current session-per-source design:

- One session can represent one user stream, one uploaded file, or one live hydrophone source.
- API-key enforcement does not break the multi-session path.
- Owner/tenant metadata enforcement does not break the multi-session path.
- Session isolation is suitable for scaling behind a service layer, provided archive indexing and deployment orchestration are added before production use.
