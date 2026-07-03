# Click classifier JSON override

Date: 2026-07-01

## Implemented

- Added a browser `Classifier type JSON override` field.
- When populated, it replaces the preset dropdown with an explicit `click.basicClassifier.standardTypes` array.
- The override accepts the existing backend parser forms:
  - preset strings such as `"beakedWhale"` and `"porpoise"`;
  - objects with `standard`, `speciesCode`, `discard`, and `whichSelections`.
- OpenAPI now documents mixed string/object `standardTypes` entries.
- The HTTP service smoke sends one classifier type as an object and one as a string to keep both parser paths covered.

## Example

```json
[
  {
    "standard": "beakedWhale",
    "speciesCode": 11,
    "discard": false
  },
  {
    "standard": "porpoise",
    "speciesCode": 22,
    "whichSelections": 127
  }
]
```

## Boundary

This exposes the standard preset type parser already implemented in the backend. It is not yet a full PAMGuard click classifier editor for every Java-side classifier mode.
