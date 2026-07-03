# CTest Service Smoke Registration

Date: 2026-07-01

This checkpoint registers service smoke scripts with CTest on Windows when PowerShell is available.

## Tests

- `service_smoke_http`
- `service_smoke_api_key`
- `service_load_smoke_foundation`

## Behavior

The tests start `pamguard_engine_service` on dedicated local ports, exercise the HTTP API, and shut the service down.

They are registered only when:

- `WIN32` is true in CMake;
- `powershell` or `pwsh` is found.

Linux/container CTest runs keep the pure C++ parity suite until equivalent portable smoke scripts are added.
