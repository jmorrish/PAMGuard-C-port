# Whistle/Moan Branch Fragmenter

Date: 2026-07-01

This checkpoint adds PAMGuard fragmentation method `2`: separate all branches.

## Implemented

- Extended `ConnectedRegionTracker` fragmentation handling:
  - method `0`: leave regions intact;
  - method `1`: discard branched regions;
  - method `2`: split branch/merge regions into single-peak fragments.
- Mirrored PAMGuard `FragmentingFragmenter` flow:
  - first slice starts one fragment per peak;
  - equal peak counts attempt ordered `matchAll`;
  - otherwise forward/back links are counted;
  - one-to-one links extend fragments;
  - branch/merge points close existing fragments and start new ones;
  - fragments below `min_pixels` or `min_length` are discarded.
- Fragment result metadata follows PAMGuard fragmented-region behaviour:
  - per-fragment slices carry one peak;
  - peak previous-link index is cleaned to `0`;
  - fragments inherit the mother region number.
- Added Java reference exporter:
  - `reference-tools/java/src/org/pamguard/port/reference/ConnectedRegionFragmentFixtureExporter.java`
- Added fixture script:
  - `reference-tools/scripts/generate-connected-region-fragment-fixture.ps1`
- Added C++ parity checker:
  - `cpp-engine/tools/connected_region_fragment_fixture_check.cpp`

## Validation

CTest status after this checkpoint:

```text
24/24 tests passed
```

New parity test:

```text
connected_region_branch_fragmenter_parity
```

## Remaining whistle/moan fragmentation work

- Fragmentation method `3`: re-link across joins.
- `maxCrossLength` cross handling.
- Rejoining gradient matching.
- Short-fragment penalty logic.
- Full contour localisation and grouped detections.
