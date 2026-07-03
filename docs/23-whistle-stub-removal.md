# Whistle/Moan Stub Removal

Date: 2026-07-01

This checkpoint adds the PAMGuard small-shape-stub cleanup step to the C++ connected-region foundation.

## Implemented

- Added `ConnectedRegionConfig.keep_shape_stubs`.
- Default behaviour now matches PAMGuard's current default:
  - `keep_shape_stubs = false`;
  - small stubs are removed from per-slice `peak_info` after contour condensation.
- Mirrored PAMGuard `StubRemover` behaviour:
  - forward pass through slices;
  - backward pass through slices;
  - diagonal matching gap depends on 4- or 8-connectivity;
  - stubs are removed when they are not the largest branch and are smaller than `min_pixels`;
  - total-pixel accounting is left unchanged, matching PAMGuard's cleanup intent over peak metadata rather than raw binary pixels.
- Added Java reference exporter:
  - `reference-tools/java/src/org/pamguard/port/reference/ConnectedRegionStubFixtureExporter.java`
- Added fixture script:
  - `reference-tools/scripts/generate-connected-region-stub-fixture.ps1`
- Added C++ parity checker:
  - `cpp-engine/tools/connected_region_stub_fixture_check.cpp`

## Validation

CTest status after this checkpoint:

```text
22/22 tests passed
```

New parity test:

```text
connected_region_stub_removal_parity
```

## Remaining whistle/moan parity work

- Full fragmentation modes:
  - leave branched regions intact;
  - discard branched regions;
  - separate branches;
  - re-link across joins.
- Rejoining fragmenter gradient matching and max-cross-length behaviour.
- Whistle/moan bearing/localisation parity.
- Whistle/moan grouped detection/event behaviour.
