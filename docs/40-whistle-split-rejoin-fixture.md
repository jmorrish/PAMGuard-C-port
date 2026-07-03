# Whistle/Moan Split Rejoin Fixture

Date: 2026-07-01

This checkpoint adds a parity fixture for PAMGuard method `3` split behaviour.

## Implemented

- Extended `connected_region_rejoin_fixture_check` with a `split` scenario.
- Added `connected-region-rejoin-split.csv`.
- Added reference fixture generator:
  - `reference-tools/scripts/generate-connected-region-rejoin-split-fixture.ps1`
- Added CTest:

```text
connected_region_rejoin_split_fragmenter_parity
```

## What it covers

The fixture creates a broad contour that splits into two single-peak branches. Method `3` uses the PAMGuard end-gradient comparison and short-fragment penalty to choose one continuation to rejoin, leaving the other branch as a separate contour if it passes size filters.
