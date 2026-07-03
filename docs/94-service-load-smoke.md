# Service Multi-Session Load Smoke

Date: 2026-07-01

This checkpoint adds:

```text
cpp-engine/scripts/service-load-smoke.ps1
```

## Purpose

The script starts `pamguard_engine_service`, creates multiple independent sessions, posts PCM chunks to each session, verifies runtime counters, and deletes the sessions.

This is a functional concurrency guard for the session model. It is not a throughput benchmark.

## Example

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\service-load-smoke.ps1 -Sessions 8 -Chunks 2
```

With API key:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\service-load-smoke.ps1 -Sessions 8 -Chunks 2 -ApiKey dev-secret
```

## Enterprise relevance

The intended production scaling model is one active engine session per source/input/user analysis pipeline. This smoke test gives a quick regression check that several sessions can be created, fed, introspected, and removed in one service process.
