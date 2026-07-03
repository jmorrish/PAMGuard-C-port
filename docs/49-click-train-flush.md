# Click Train Flush

Date: 2026-07-01

This checkpoint emits active click train summaries when a finite stream or file is flushed.

## Implemented

- Added `ClickTrainTracker::flush()`.
- `AnalysisSession::flush()` now includes completed click train summaries.
- `POST /sessions/{sessionId}/flush` now returns pending click trains as well as whistle/moan regions.
- Service runtime counters include flushed click trains.
- Extended `click_train_tracker_foundation` coverage with a flush scenario.

## Why this matters

Without a final flush, a click train that reaches the minimum click count near the end of a file remains active and may never be reported as completed. This is especially important for offline WAV/MP3 analysis.
