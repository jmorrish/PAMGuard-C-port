# Static Web UI Serving

Date: 2026-07-01 (asset route updated 2026-07-25)

This checkpoint lets the C++ engine service optionally serve the browser console.

## Implemented

- Added `PAMGUARD_WEB_UI_FILE`.
- When set, the service serves that file at:
  - `/`;
  - `/index.html`.
- Added the confined `GET /assets/...` route needed to extract CSS and
  JavaScript from the monolithic page:
  - `PAMGUARD_WEB_ASSET_DIR` may explicitly name the asset directory;
  - otherwise, an existing `assets` directory beside the validated
    `PAMGUARD_WEB_UI_FILE` is used;
  - every request is structurally checked, canonicalized, confined again
    after symlink resolution, and restricted to regular files with allowlisted
    MIME types.
- `GET /health` reports `webUiEnabled` and `webAssetsEnabled`.
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
# Optional explicit override:
# $env:PAMGUARD_WEB_ASSET_DIR = "C:\python\PAMGuard_Port\web-ui\assets"
.\cpp-engine\build\pamguard_engine_service.exe 8080
```

Then open:

```text
http://localhost:8080/
```

## Security note

The static HTML and allowlisted assets are public when enabled. Protected API
calls still require the configured API key. `/assets/...` is not a general
static mount: parent segments, backslashes, encoded traversal, alternate data
streams, unsupported extensions, non-regular files, and symlink/junction
escapes are rejected.
