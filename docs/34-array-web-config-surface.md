# Array Geometry Web Configuration Surface

Date: 2026-07-01

This checkpoint makes multi-channel click localisation usable from the browser console.

## Implemented

- Added browser controls for:
  - linear hydrophone spacing in metres;
  - speed of sound in metres per second.
- The browser now creates one hydrophone per channel on a linear x-axis array.
- Session JSON responses now include hydrophone positions, not just hydrophone count.
- The OpenAPI session-create schema now documents array geometry fields.

## Browser default

For a two-channel session, the console sends:

```json
{
  "array": {
    "id": "browser-linear-array",
    "speedOfSoundMps": 1500,
    "hydrophones": [
      {"channel": 0, "xM": 0, "yM": 0, "zM": 0, "sensitivityDb": 0},
      {"channel": 1, "xM": 1, "yM": 0, "zM": 0, "sensitivityDb": 0}
    ]
  }
}
```

## Localisation note

The current C++ localiser uses the configured hydrophone coordinates for far-field click bearings. This is a foundation for multi-channel streamed WAV/MP3/Icecast inputs; production arrays will still need proper geometry/profile management.

## Remaining localisation work

- Add full array profile editor and persistence.
- Add per-channel calibration/sensitivity handling.
- Add richer 2D/3D array layouts beyond a generated line array.
