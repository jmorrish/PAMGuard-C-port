# Far-Field Bearing Foundation

Date: 2026-06-30

This checkpoint adds a geometry-aware bearing estimate on top of existing click pair-delay localisation.

## Implemented

- Added `FarFieldBearingLocaliser`.
- Accepts hydrophone geometry:
  - channel;
  - `xM`;
  - `yM`;
  - `zM`;
  - speed of sound.
- Uses existing pairwise click delay estimates.
- Solves a far-field least-squares unit-vector estimate.
- Emits:
  - unit vector;
  - azimuth degrees;
  - elevation degrees;
  - RMS residual;
  - used pair count.
- Service config now accepts:

```json
{
  "array": {
    "speedOfSoundMps": 1500.0,
    "hydrophones": [
      { "channel": 0, "xM": 0.0, "yM": 0.0, "zM": 0.0 },
      { "channel": 1, "xM": 1.0, "yM": 0.0, "zM": 0.0 },
      { "channel": 2, "xM": 0.0, "yM": 1.0, "zM": 0.0 }
    ]
  }
}
```

- Service results now include:

```json
"clickBearings": []
```

## Validation

CTest status after this checkpoint:

```text
21/21 tests passed
```

New test:

```text
far_field_bearing_foundation
```

HTTP smoke test with one 3-channel click and three hydrophones:

```json
{"clicks":1,"delaySets":1,"bearings":1,"usedPairs":3}
```

## Important scope note

This is a far-field bearing foundation, not full PAMGuard target-motion localisation parity. Full parity still needs PAMGuard's localisation model selection, ambiguity handling, target-motion algorithms, error models, and array manager semantics.
