# Static Web UI Serving

Date: 2026-07-01

This checkpoint lets the C++ engine service optionally serve the browser console.

## Implemented

- Added `PAMGUARD_WEB_UI_FILE`.
- When set, the service serves that file at:
  - `/`;
  - `/index.html`.
- `GET /health` reports `webUiEnabled`.
- The container image copies `web-ui/index.html` into `/app/web-ui/index.html`.
- `docker-compose.engine.yml` enables web UI serving by default.
- The Click Detector dialog uses eight focused in-dialog sections rather than
  one continuously scrolling form (`docs/232-click-detector-control-sections.md`).
- The Noise & monitoring dialog uses eight focused sections with structured
  common controls and module-specific advanced JSON
  (`docs/236-monitoring-module-browser-and-contract.md`).

## Example

```powershell
$env:PAMGUARD_WEB_UI_FILE = "C:\python\PAMGuard_Port\web-ui\index.html"
.\cpp-engine\build\pamguard_engine_service.exe 8080
```

Then open:

```text
http://localhost:8080/
```

## Security note

The static HTML is public when enabled. Protected API calls still require the configured API key, and the browser console provides an API-key field for that case.
