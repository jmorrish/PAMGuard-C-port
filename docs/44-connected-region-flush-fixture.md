# Connected Region Flush Fixture

Date: 2026-07-01

This checkpoint adds detector-level coverage for flushing a still-growing whistle/moan connected region.

## Implemented

- Extended `connected_region_fixture_check` with `flush` mode.
- Added CTest:

```text
connected_region_flush_parity
```

## What it covers

The fixture uses the same contour as `connected_region_basic_parity`, but emits it through `ConnectedRegionTracker::flush()` instead of closing it with a following empty slice. This protects the finite-file/session flush path used by:

```text
POST /sessions/{sessionId}/flush
```
