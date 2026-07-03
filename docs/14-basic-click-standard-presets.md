# Basic Click Standard Presets

Date: 2026-06-30

This checkpoint adds built-in C++ helpers for PAMGuard's standard basic click classifier presets.

## Implemented

- Added `standard_basic_click_type(...)`.
- Added preset enum:
  - `BeakedWhale`
  - `Porpoise`
- Copied preset constants from PAMGuard `ClickTypeParams`:
  - energy-band settings;
  - peak-frequency search and accepted range;
  - mean-frequency range;
  - default selection bitmask;
  - default energy-difference threshold.
- Added service config support:

```json
{
  "click": {
    "enabled": true,
    "basicClassifier": {
      "enabled": true,
      "standardTypes": [
        { "standard": "beakedWhale", "speciesCode": 17 },
        { "standard": "porpoise", "speciesCode": 23 }
      ]
    }
  }
}
```

String shortcuts are also accepted:

```json
{
  "standardTypes": ["beakedWhale", "porpoise"]
}
```

When string shortcuts are used, species code `1` is assigned to beaked whale and species code `2` to porpoise.

## Validation

CTest status after this checkpoint:

```text
19/19 tests passed
```

HTTP standard preset parse smoke test:

```json
{"created":true,"sessions":1}
```

## Notes

The porpoise preset uses frequency ranges above 100 kHz, so streams must have a sufficiently high sample rate for that preset to be meaningful.
