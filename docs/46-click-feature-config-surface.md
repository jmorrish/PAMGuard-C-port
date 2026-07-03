# Click Feature Configuration Surface

Date: 2026-07-01

This checkpoint exposes click feature extraction ranges through the browser console and session-status JSON.

## Implemented

- Added browser controls for:
  - two energy bands;
  - peak-frequency search range;
  - mean-frequency range;
  - click length energy fraction;
  - peak width energy fraction.
- Session status now reports the stored click feature configuration under `click.features`.
- OpenAPI now documents click feature configuration fields.

## Why this matters

Click classification and analyst review depend on the feature extraction ranges. The C++ backend already computed these values, but operators could not tune them from the web configuration surface.
