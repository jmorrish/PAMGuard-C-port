# Cross-Chunk Whistle Groups

Date: 2026-07-21

## Purpose

`docs/191` left an honest caveat: "Groups are reported in the result where their newest member completed, so a long-lived group appears once per chunk that adds to it rather than as a single consolidated record." Investigating that turned up two real defects in what such a group reported about itself, plus a misconception about what "cross-chunk grouping" even means here.

## What cross-chunk grouping actually is

`whistle_detections_match` requires **time overlap above 0.5** between two contours. Two successive whistles therefore never group with each other, however close together — they do not overlap in time at all.

What spans a chunk boundary is a *single call* whose contours finish in different chunks: one channel's contour ends just before the boundary and its partner on another channel ends just after. The two overlap heavily in time; they simply complete at different moments. The retained region history exists for exactly that case.

This matters because the original test for this behaviour fed the same two-channel burst as two successive chunks and asserted that groups formed. They did — one per chunk, independently — so the test passed while proving nothing about the boundary. A guard requiring at least one group to actually span the boundary failed immediately, which is how the misconception surfaced.

## The defects fixed

A group formed in a later chunk from an earlier-chunk member reported:

1. **The wrong first sample.** `first_start_sample` was set from the group's first member *in this chunk*, so a group whose earliest contour completed in an earlier chunk claimed to start later than it did — and a group re-reported across chunks would appear to move forward in time.
2. **Channels with no corresponding region.** `channels` included the retained member's channel, but `region_indices` only ever held current-chunk indices, so a consumer saw a channel listed with no region to look at and no way to tell why. `channels` could also list the same channel twice.

A third, subtler case was wrong too: a region processed **earlier in the same chunk** and ungrouped at the time could be pulled into a group by a later region, and was then counted as an earlier-chunk member even though its index was perfectly available.

## Implementation

Per-group facts that outlive a chunk now live in `whistle_group_states_`, keyed by group id: the true first sample, the member count, and the accumulated channel set. A group re-reported later seeds itself from that state rather than starting blank.

Retained regions carry `result_index` and `from_current_chunk`, the latter reset at the top of each grouping pass. When a retained region joins a group, it goes into `region_indices` if it is from this chunk and into `earlier_region_count` if it is not — so the two are exhaustive and a consumer can always tell whether a listed channel is inspectable in the result at hand.

`channels` is now a set: `add_channel` checks before appending.

The state map is pruned against the retained history each pass, so a long session does not accumulate one entry per group ever formed.

## API output

Schema v20 adds `earlierRegionCount` to `whistleGroups` entries: members that completed in earlier chunks and are therefore not addressable through `regionIndices`. `firstStartSample` now means the group's true first sample rather than this chunk's earliest member.

`regionIndices` still indexes the current chunk only. Consolidating a group into a single record across chunks would mean withholding it until it is provably complete, which for a streaming engine means either unbounded latency or an arbitrary timeout — the per-chunk report with an explicit earlier-member count keeps the data honest without either.

## Validation

`session_whistle_delay_wiring` gains a case built for the real boundary condition: `staggered_chunk` runs channel 1's tone well past channel 0's, and the audio is split between the two completions. Channel 0's region completes in the head chunk and is ungrouped; channel 1's region completes in the tail chunk, matches the retained one, and forms the group.

The test asserts the group carries `earlier_region_count > 0`, spans two channels, and reports a `first_start_sample` **before the split point** — the earlier-chunk member's, not its own. The observed values confirm the mechanism: head chunk one region on channel 0, tail chunk one region on channel 1 with a group holding one current index, one earlier member, two channels, and a first sample of 12799 against a split at 19200.

The existing cross-chunk case keeps its group-id and channel-set assertions, now including a duplicate-channel check.

Full CTest suite passes `71/71`.

## Claim boundary

Grouping is still association only, and the grouper is still branch-covered rather than fixture-pinned (`docs/189`) — `DetectionGrouper` runs over live `PamDataBlock` iterators, so headless fixture generation hits the same `PamController` wall as the classifiers.

A group is still reported once per contributing chunk. `earlierRegionCount` makes that visible rather than removing it.

The retained history is bounded at 256 regions. PAMGuard's grouper stops scanning at matches older than two seconds, so retention far beyond that cannot change results — but a session with very many channels producing very many contours per second could in principle evict a region still inside the two-second window. Nothing in the current test coverage exercises that.
