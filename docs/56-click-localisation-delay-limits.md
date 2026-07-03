# Click Localisation Delay Limits

Date: 2026-07-01

This checkpoint constrains click delay estimation using configured array geometry.

## Implemented

- `AnalysisSession` now derives maximum correlation delays per hydrophone pair from:
  - hydrophone coordinates;
  - speed of sound;
  - session sample rate.
- The delay estimator falls back to unconstrained search if geometry is incomplete.

## Formula

```text
maxDelaySamples = ceil(distanceMetres / speedOfSoundMps * sampleRateHz) + 1
```

## Why this matters

For real multi-channel arrays, physically impossible delays waste compute and can produce false correlation peaks. Geometry-derived limits make the click localisation foundation more production-like while preserving the existing unconstrained fallback.
