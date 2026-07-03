# Browser Array Geometry Override

Date: 2026-07-01

This checkpoint adds an advanced hydrophone geometry override to the browser console.

## Why

Multi-channel click localisation depends on channel order and hydrophone coordinates. The browser previously generated a simple linear array from channel count and spacing.

That remains the default, but operators can now paste arbitrary geometry for real arrays.

## Accepted shapes

Hydrophone array only:

```json
[
  {"channel": 0, "xM": 0.0, "yM": 0.0, "zM": 0.0, "sensitivityDb": 0.0},
  {"channel": 1, "xM": 1.0, "yM": 0.0, "zM": 0.0, "sensitivityDb": 0.0}
]
```

Full array object:

```json
{
  "id": "harbour-array",
  "speedOfSoundMps": 1500.0,
  "hydrophones": [
    {"channel": 0, "xM": 0.0, "yM": 0.0, "zM": 0.0, "sensitivityDb": 0.0},
    {"channel": 1, "xM": 1.0, "yM": 0.0, "zM": 0.0, "sensitivityDb": 0.0}
  ]
}
```

The service still validates duplicate channels, channel range, and finite coordinates.

## Localisation note

For Icecast/BUTT/MP3/WAV streams, FFmpeg preserves decoded channel order unless channel mapping is requested. The engine interprets each interleaved channel against this configured hydrophone geometry.
