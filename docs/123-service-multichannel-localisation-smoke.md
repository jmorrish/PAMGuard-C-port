# Service multi-channel localisation smoke

Date: 2026-07-01

## What changed

`cpp-engine/scripts/service-smoke.ps1` now explicitly checks that the HTTP service returns multi-channel click localisation outputs.

The smoke session uses:

- Two channels.
- Two hydrophones.
- Click localisation enabled.
- Interleaved `f32le` PCM.

The smoke now asserts:

- Live `clickLocalisations` include at least one delay pair.
- Live `clickBearings` include at least one used pair.
- Archived `click-localisation` events include delay payloads.
- Archived `click-bearing` events include used-pair metadata.

## Why this matters

This proves the web/API path can carry multi-channel click timing and bearing outputs, not just single-channel detections.

It does not claim full PAMGuard array/localiser parity yet; it protects the current localisation foundation from regressions.
