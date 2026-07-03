# Optional Click Spectra Output

Date: 2026-07-01

This checkpoint makes large click feature spectra opt-in.

## Implemented

- Added request query parameter:

```text
includeClickSpectra=true
```

- Click feature summaries remain present in every response:
  - click length;
  - peak frequency;
  - peak width;
  - mean frequency;
  - band energies.
- Large arrays are only included when requested:
  - `totalPowerSpectrum`;
  - per-channel `powerSpectrum`.
- Compact responses include bin counts:
  - `totalPowerSpectrumBins`;
  - per-channel `powerSpectrumBins`.
- Browser console now includes a click spectra output selector.
- OpenAPI documents `includeClickSpectra`.

## Why this matters

For many concurrent sources, always serializing spectra is unnecessarily expensive. Analyst clients can still opt in when plotting or detailed review needs the arrays.
