# Whistle/Moan Discard Fragmenter

Date: 2026-07-01

This checkpoint adds PAMGuard's simplest whistle/moan fragmentation mode: discard branched/complex connected regions.

## Implemented

- Added `ConnectedRegionConfig.fragmentation_method`.
- Added support for PAMGuard fragmentation method `1`:
  - discard connected regions where any condensed slice contains more than one peak;
  - return no connected-region result for those complex shapes.
- Exposed service config:

```json
{
  "whistle": {
    "regionEnabled": true,
    "fragmentationMethod": 1
  }
}
```

- Added Java reference exporter:
  - `reference-tools/java/src/org/pamguard/port/reference/ConnectedRegionDiscardFixtureExporter.java`
- Added fixture script:
  - `reference-tools/scripts/generate-connected-region-discard-fixture.ps1`
- Added C++ parity checker:
  - `cpp-engine/tools/connected_region_discard_fixture_check.cpp`

## Validation

CTest status after this checkpoint:

```text
23/23 tests passed
```

New parity test:

```text
connected_region_discard_fragmenter_parity
```

## Remaining fragmentation work

- Fragmentation method `2`: separate all branches.
- Fragmentation method `3`: re-link across joins.
- `maxCrossLength` cross handling.
- Gradient matching and short-fragment penalties in `RejoiningFragmenter`.
