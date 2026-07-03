# Optional Click Waveform Output

Date: 2026-07-01

This checkpoint adds opt-in click waveform samples to live processing responses.

## Implemented

- Added request query parameter:

```text
includeClickWaveforms=true
```

- When enabled, each click result includes:
  - `channels`;
  - `waveform`, as channel-major sample arrays.
- Default responses remain compact and continue to report only waveform dimensions.
- Browser console now includes a click waveform output selector.
- OpenAPI documents `includeClickWaveforms`.

## Why this matters

Analyst-facing click modules need waveform inspection, but streaming deployments should not pay the JSON size cost unless the client asks for the samples.
