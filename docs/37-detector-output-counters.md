# Detector Output Counters

Date: 2026-07-01

This checkpoint adds per-session detector output counters for operations and debugging.

## Implemented

- Runtime session JSON now accumulates:
  - spectrogram frame count;
  - click count;
  - click feature count;
  - click classification count;
  - click train count;
  - click train localisation count;
  - click train bearing count;
  - click localisation count;
  - click bearing count;
  - whistle peak count;
  - whistle region count.
- `/metrics` exposes the same detector outputs as Prometheus counters:

```text
pamguard_session_detector_outputs{session="station-001",type="clicks"} 42
pamguard_session_detector_outputs{session="station-001",type="whistle_regions"} 7
```

## Why this matters

For 50+ concurrent users/sources, ingest volume alone is not enough. These counters make it obvious whether a stream is alive but quiet, misconfigured, producing detections, or failing downstream detector stages.
