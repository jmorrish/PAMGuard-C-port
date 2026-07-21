# Whistle Detection Grouper

Date: 2026-07-10

## Purpose

Ports `WhistleDetectionGrouper` and the `DetectionGrouper` scan it inherits — the last unported detector-side module. It associates whistle contours detected on *different* channel groups as views of the same call, which is what makes cross-group whistle localisation meaningful.

## Reference semantics ported

`match` requires:

- **Different** sequence bitmaps. Contours from the same channel group never group with each other; PAMGuard uses the sequence bitmap rather than the channel bitmap so beamformer output works.
- Time overlap **and** frequency overlap both greater than 0.5, each taken as the larger of the two directional overlaps.

`findGroups` scans recent detections newest-first, collecting matches, and requires at least two channel groups to do anything at all.

## Two quirks preserved deliberately

**The frequency overlap is not a true intersection.** `PamDataUnit.getFrequencyOverlap` computes its upper bound with `Math.max` where an intersection needs `Math.min` — compare `getTimeOverlap`, which correctly uses `min`. The result can therefore exceed one, and the grouper's 0.5 frequency test is far easier to pass than it appears. The port reproduces this and a test pins a value above one, so the behaviour cannot drift silently. Changing it would alter which contours group.

**The two-second cutoff only fires after a match.** The `break` sits *inside* the match branch, so a long run of non-matching detections does not stop the scan; only a match older than two seconds does. The port keeps that structure and a test covers it.

## Validation

`whistle_grouper_coverage` covers same-group rejection, a matching cross-group contour, time-disjoint and frequency-disjoint rejection, zero overlap for disjoint ranges, the above-one frequency overlap quirk, grouping disabled below two channel groups, and the newest-first scan stopping after an old match. Full CTest suite passes `66/66`.

## Claim boundary

Branch coverage, not fixture parity: `DetectionGrouper` operates over live `PamDataBlock` iterators, so driving it headlessly hits the same `PamController` coupling as the classifiers (`docs/177`). The grouper is **not yet wired into the session** — whistle regions are detected per channel and the delay path already pairs channels directly (`docs/180`), so grouping would add a distinct cross-group association output rather than change existing results. `GroupDetection`/`DetectionGroupLocaliser`, which localise a group once formed, are also unported.
