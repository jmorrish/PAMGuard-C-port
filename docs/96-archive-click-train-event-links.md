# Archive Click Train Event Links

Date: 2026-07-01

This checkpoint enriches archived detector-event envelopes with click-train relationships.

## Behavior

When a raw archive record contains `clickTrains`, the detection projection builds a `clickStartSample -> trainId` map.

The following event types can then include `relatedTrainIds`:

- `click`
- `click-feature`
- `click-classification`
- `click-localisation`
- `click-bearing`

Example:

```json
{
  "type": "click",
  "startSample": 123456,
  "relatedTrainIds": [4],
  "payload": {}
}
```

## Why this matters

The web layer can now connect individual click detections, features, classifications, localisation delay sets, and bearings back to click tracks without re-deriving train membership from payload arrays client-side.

## Validation

The service smoke now posts three synthetic click chunks, verifies archived `click-track` events, and checks that at least one archived `click` event includes `relatedTrainIds`.
