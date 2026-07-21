# Whistle LSQ Bearing And Full Pair Set

Date: 2026-07-10

## Purpose

Closes the whistle LSQ item left open by `docs/175`. Two changes: whistle regions now measure every channel pair, and groups with enough hydrophones get an unambiguous LSQ bearing.

## Full pair set (correctness fix)

PAMGuard's `WhistleDelays` measures `nChannels * (nChannels - 1) / 2` delays — "channels 0-1, 0-2, 1-2, etc". The engine previously paired the region's own channel with each other channel, which for a four-channel group produced three delays instead of six. Regions now iterate all `i < j` pairs, matching the reference and supplying the full set an LSQ solve requires.

This is a **behaviour change** for whistle sessions with more than two channels: more `delays` entries per region. Field shapes are unchanged and two-channel sessions are unaffected.

## LSQ bearing

When the whistle group has four or more channels, all pairs are geometry-constrained, and `array.spacingErrorM` is positive, the region delays go through the ported LSQ localiser (`docs/158`). A valid solve populates `lsqBearing` (azimuth/elevation in radians and degrees, curvature errors when finite, `usedPairs`) and sets the region bearing from it with `bearingAmbiguity` **false** — the unambiguous case PAMGuard gets from its LSQ localiser. Otherwise the pair-localiser bearing and its ambiguity flag are used, exactly as before.

## Schema

Bumps the engine result `schemaVersion` to `12`.

## Validation

`session_whistle_delay_wiring` gains a four-hydrophone tetrahedron case asserting regions carry all six channel pairs and that any LSQ bearing produced is unambiguous over six pairs. Full CTest suite passes `64/64`.

## Claim boundary

Bearing localiser selection remains a channel-count rule rather than PAMGuard's array-shape-based `BearingLocaliserSelector`. Rank-deficient geometries (collinear or coplanar hydrophones) yield no LSQ solve and fall back to the pair bearing, matching the Jama behaviour pinned in `docs/158`. The whistle detection grouper is still unported.
