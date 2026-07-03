# API key file secret

Date: 2026-07-01

## What changed

`pamguard_engine_service` now supports file-based API key configuration:

- `PAMGUARD_API_KEY`
- `PAMGUARD_API_KEY_FILE`

`PAMGUARD_API_KEY` takes precedence when both are set.

The file value is trimmed for leading/trailing whitespace, which matches common mounted-secret behavior where files may end with a newline.

## Validation

`cpp-engine/scripts/service-smoke.ps1` now supports `-ApiKeyFile`.

CTest registers `service_smoke_api_key_file` on Windows to validate mounted-secret style authentication.
