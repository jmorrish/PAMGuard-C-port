# Click Bitmap Configuration Surface

Date: 2026-07-01

This checkpoint exposes click detector channel and trigger bitmaps in operator-facing configuration.

## Implemented

- Browser console now includes:
  - `channelBitmap`;
  - `triggerBitmap`.
- Defaults remain all channels for the current channel count.
- OpenAPI documents both fields.
- `station-session.example.json` includes explicit two-channel bitmaps.

## Why this matters

PAMGuard click detector deployments often separate channels that are processed from channels that are allowed to trigger detections. Exposing the bitmaps is needed for real multi-channel arrays and mixed-channel streams.
