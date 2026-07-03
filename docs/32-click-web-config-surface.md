# Click Detector Web Configuration Surface

Date: 2026-07-01

This checkpoint exposes the core click-detector controls through the browser console and session-status JSON.

## Implemented

- Added browser controls for threshold, trigger channel count, short/long filters, waveform capture length, minimum separation, and maximum click length.
- Added browser controls for feature extraction, standard basic classifier presets, and click train tracking.
- Added a browser JSON override for custom standard basic classifier type entries, including species code, discard flag, and selection bitmask fields accepted by the backend parser.
- Extended `GET /sessions/{id}` session JSON to report stored click detector settings.
- Updated the OpenAPI session-create schema for click detector fields.

## Browser default

The browser console now sends both standard basic classifier presets by default:

```json
{
  "click": {
    "enabled": true,
    "localisation": true,
    "thresholdDb": 10,
    "featuresEnabled": true,
    "basicClassifier": {
      "enabled": true,
      "standardTypes": ["beakedWhale", "porpoise"]
    },
    "train": {
      "enabled": true,
      "maxIciSeconds": 0.5,
      "minClicks": 3
    }
  }
}
```

## Remaining click module work

- Add click detector pre-filter controls once the full PAMGuard pre-filter chain is ported.
- Add classifier profile persistence per deployment.
