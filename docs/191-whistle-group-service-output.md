# Whistle Group Service Output

Date: 2026-07-10

## Purpose

Wires the ported detection grouper (`docs/189`) into the session, so whistle contours detected independently on different channels are associated as views of one call and served.

## Behaviour

For sessions running whistle region detection on two or more channels, completed regions are converted to grouper candidates — channel as sequence bitmap, start and end samples, and the frequency range converted from bins to Hz using the session FFT length — and associated with the ported `match` rule (different channels, both time and frequency overlap above 0.5).

Each `whistleGroups` entry carries `groupId`, `regionIndices` into the result's `whistleRegions`, the contributing `channels`, and the group's first and last start samples.

## Schema

Bumps the engine result `schemaVersion` to `15` (purely additive).

## Validation

Full CTest suite passes `66/66`, including the grouper's own eight-case coverage (`whistle_grouper_coverage`) which pins the match semantics this wiring depends on.

## Cross-chunk association

Grouping matches each new region against a **retained history** of regions completed in earlier chunks, mirroring PAMGuard's grouper scanning a live data block that spans earlier processing. Without it, contours finishing in different chunks would never group, which for streamed audio would miss most associations.

The history is bounded at 256 regions. That bound cannot change results: PAMGuard's scan stops at matches older than two seconds, so regions far beyond that never contribute. Group identity is carried on the retained entries, so a region joining an existing group re-uses its `groupId` across chunks rather than starting a new one.

`session_whistle_delay_wiring` covers this by feeding the same two-channel burst as two successive chunks and asserting groups still form and always span at least two channels.

## Claim boundary

Grouping is association only: `GroupDetection` and `DetectionGroupLocaliser`, which localise a group once formed, remain unported, and the grouper itself is branch-covered rather than fixture-pinned (`docs/189`). Groups are reported once per chunk that contributes a member, so a call whose contours complete in different chunks appears more than once. `docs/201-cross-chunk-whistle-groups.md` makes that self-describing with `earlierRegionCount` and fixes what such a group reported about its own extent.
