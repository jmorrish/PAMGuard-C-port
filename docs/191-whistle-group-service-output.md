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

## Claim boundary

**Scope of association differs from PAMGuard in one respect worth stating plainly.** PAMGuard's `DetectionGrouper` scans a live data block that spans earlier processing, so it can associate a contour with one completed some seconds earlier. The engine associates regions completed within the same result, which for streamed chunks means association is bounded by the chunk in which regions complete. Contours completing in different chunks will not group. Widening that would need a retained cross-chunk region history like the FFT history used for whistle delays (`docs/165`).

Grouping is association only: `GroupDetection` and `DetectionGroupLocaliser`, which localise a group once formed, remain unported, and the grouper itself is branch-covered rather than fixture-pinned (`docs/189`).
