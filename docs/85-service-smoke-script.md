# Service smoke script

Added:

```text
cpp-engine/scripts/service-smoke.ps1
```

The script starts the built `pamguard_engine_service.exe` with temporary session persistence and archive directories, then checks:

- `GET /health`;
- `GET /ready`;
- `POST /sessions`;
- two contiguous `POST /sessions/{id}/pcm-f32le` chunks;
- PCM `sampleContinuity` and `nextExpectedStartSample`;
- runtime `createdUnixMs` and `lastReceiveUnixMs`;
- `GET /sessions/{id}/archive?limit=2`;
- archive `startSampleFrom`/`startSampleTo` filtering;
- `DELETE /sessions/{id}`.

Run after building the C++ engine:

```powershell
cd cpp-engine
.\scripts\service-smoke.ps1 -Port 18080
```

Optional API-key mode:

```powershell
.\scripts\service-smoke.ps1 -Port 18080 -ApiKey "dev-secret"
```

This is an integration smoke, not a parity fixture. It complements `ctest` by exercising the HTTP/service lifecycle paths.

Current validation:

- unauthenticated smoke passed against the current build;
- API-key smoke passed against the current build.
