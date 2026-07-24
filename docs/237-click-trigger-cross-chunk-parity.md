# Click trigger cross-chunk parity

Date: 2026-07-24

This checkpoint pins the click trigger when a transient begins in one PCM
chunk and ends in the next.

## Java authority

`ClickTriggerFixtureExporter` continues to use PAMGuard 2.02.18e's real
`clickDetector.TriggerFilter`. Its optional `processChunkLength` now models
the live detector's boundary-sensitive setup:

- short/long trigger memories are initialised from the first chunk mean;
- both real Java filter objects then run continuously over later chunks;
- click state, trigger bitmap, maximum signal excess, and separation countdown
  remain live across the boundary.

The new `boundary-transient` signal spans samples 124–132 in a 256-sample
stream processed as two 128-sample chunks. Java emits one click at sample 115,
duration 45, trigger bitmap 3, time 2 ms, and signal excess
24.312714237937870 dB.

## C++ validation

`click_trigger_fixture_check` accepts a processing chunk length and feeds the
same global signal as separate `AudioChunk` objects. The comparison therefore
also exercises waveform history across chunks and repeats the split stream
after `reset()`.

The registered `click_trigger_cross_chunk_parity` test matches every Java
field exactly and signal excess within the existing `1e-10` dB bound.

## Claim boundary

This proves the selected 2.02.18e trigger path for this active-click boundary
case. It is not a sweep over arbitrary chunk sizes or discontinuous sample
timelines. `longFilter2` is retained for settings parity, but the authoritative
live Java detector constructs its long filter with
`TriggerFilter(longFilter, 1)`, so it does not select a second alpha.
