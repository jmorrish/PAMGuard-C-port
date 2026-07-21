# Whistle Region Bearing

Date: 2026-07-10

## Purpose

Closes the whistle bearing conversion item. `docs/164`/`docs/165` ported and served whistle contour delays; PAMGuard turns those delays into a bearing by passing them to the channel group's bearing localiser and wrapping the result in `WhistleBearingInfo`. This slice adds the equivalent region-level bearing output.

## Reference semantics

`WhistleToneConnectProcess` calls `bearingLocaliser.localise(delays, timeMillis)` and stores the angle set in a `WhistleBearingInfo`, whose `bearingAmbiguity()` is true when the localiser returned a single angle. For a two-element group that localiser is `PairBearingLocaliser` — already ported with fixture parity (`docs/157`) and already applied per delay pair in the whistle path — so the conversion here is the region-level consolidation plus the ambiguity semantics.

Each `whistleDelays` entry now carries a `bearing` object when a geometry-constrained pair bearing was resolved:

- `bearingRadians` / `bearingDegrees` — the group's bearing angle.
- `bearingErrorRadians` — omitted when the reference error formula is non-finite (the endfire clamp).
- `bearingAmbiguity` — true for the pair localiser's single cone angle, matching `WhistleBearingInfo.bearingAmbiguity()`.
- `pairCount` — contributing delay pairs.

## Schema

Bumps the engine result `schemaVersion` to `11` (purely additive).

## Validation

- `session_whistle_delay_wiring` asserts every region with delays carries a consistent bearing summary: valid, ambiguity set, pair count matching the delay count, and the angle equal to the contributing pair bearing.
- Service smoke asserts schema version 11; full CTest suite passes `61/61`.

## Claim boundary

Groups with four or more hydrophones would use the LSQ localiser in PAMGuard, giving an unambiguous azimuth/elevation pair; the whistle path currently pairs the region channel with each other channel rather than building the full pair set an LSQ solve needs, so whistle LSQ bearings remain unported and `bearingAmbiguity` is always true. `WhistleBearingInfo`'s array-axis/bearing-reference fields and the whistle detection grouper are also not ported.
