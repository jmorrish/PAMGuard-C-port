# Whistle Peak Configuration Surface

Date: 2026-07-01

This checkpoint exposes whistle/moan peak detector settings through the browser console and session-status JSON.

## Implemented

- Added browser controls for:
  - detection threshold in dB;
  - background peak time constants;
  - maximum percentage over threshold;
  - minimum and maximum peak width;
  - search bin bounds.
- Session status now reports the stored whistle peak settings.
- OpenAPI now documents whistle peak detector fields.

## Why this matters

The connected-region whistle/moan tracker operates on peak detector output. Making peak detection configurable is necessary before contour fragmentation/rejoin settings can be tuned meaningfully against real hydrophone streams.
