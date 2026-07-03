# Whistle/Moan Cross Rejoin Fixture

Date: 2026-07-01

This checkpoint adds a parity fixture for PAMGuard method `3` crossing behaviour.

## Implemented

- Extended `connected_region_rejoin_fixture_check` with a `cross` scenario.
- Added `connected-region-rejoin-cross.csv`.
- Added reference fixture generator:
  - `reference-tools/scripts/generate-connected-region-rejoin-cross-fixture.ps1`
- Added CTest:

```text
connected_region_rejoin_cross_fragmenter_parity
```

## What it covers

The fixture creates two single-bin contours that cross through a short shared bridge:

```text
low in  -> cross bridge -> high out
high in -> cross bridge -> low out
```

This exercises PAMGuard `jumpCross` behaviour, where input fragments and output fragments are sorted by frequency and linked in crossing order while the short crossing bridge itself is discarded.
