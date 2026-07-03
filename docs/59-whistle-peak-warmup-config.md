# Whistle Peak Warmup Configuration

Date: 2026-07-01

This checkpoint exposes whistle peak detector warmup control.

## Implemented

- Added `whistle.warmupSlices` parsing in the service.
- Session status reports `warmupSlices`.
- Browser console includes a warmup slice input.
- OpenAPI and `station-session.example.json` document the field.

## Why this matters

The whistle peak detector uses background estimates before accepting detections. Warmup control is important when balancing startup stability against short offline files where detections may occur early.
