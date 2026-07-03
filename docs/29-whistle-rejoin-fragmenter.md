# Whistle/Moan Rejoin Fragmenter

Date: 2026-07-01

This checkpoint adds PAMGuard fragmentation method `3`: re-link branch fragments after splitting.

## Implemented

- Extended `ConnectedRegionTracker` fragmentation handling:
  - method `0`: leave regions intact;
  - method `1`: discard branched regions;
  - method `2`: split branches into single-peak fragments;
  - method `3`: split branches, then rejoin likely continuous contours.
- Ported PAMGuard rejoin metadata:
  - `nJoinedStart` is captured when a fragment starts;
  - `nJoinedEnd` is captured when a fragment closes;
  - short fragments are preserved during initial splitting for method `3`;
  - final short-fragment pruning happens after rejoining.
- Ported PAMGuard rejoin maths:
  - `gradLength = 20`;
  - start-gradient and end-gradient formulas from `ConnectedRegion`;
  - Java integer-style `shortPenalty`;
  - merge and split selection by nearest gradient;
  - short crossing re-link by ordered input/output frequencies.
- Added PAMGuard `maxCrossLength` configuration with default `5`.
- Added C++ parity checker:
  - `cpp-engine/tools/connected_region_rejoin_fixture_check.cpp`
- Added Java reference exporter:
  - `reference-tools/java/src/org/pamguard/port/reference/ConnectedRegionRejoinFixtureExporter.java`
- Added fixture script:
  - `reference-tools/scripts/generate-connected-region-rejoin-fixture.ps1`

## Configuration

HTTP session creation accepts:

```json
{
  "whistle": {
    "regionEnabled": true,
    "fragmentationMethod": 3,
    "maxCrossLength": 5
  }
}
```

`GET /sessions/{id}` now reports the stored `whistle.fragmentationMethod` and `whistle.maxCrossLength` fields.

## Validation target

New parity test:

```text
connected_region_rejoin_fragmenter_parity
connected_region_rejoin_cross_fragmenter_parity
connected_region_rejoin_split_fragmenter_parity
```

## Remaining whistle/moan parity work

- Larger PAMGuard-generated branch/rejoin fixture bank across noisy contours.
- Full contour localisation and grouped whistle/moan detections.
- UI controls for all whistle/moan fragmentation settings.
