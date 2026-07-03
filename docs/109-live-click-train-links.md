# Live Click Train Links

Date: 2026-07-01

This checkpoint adds click-train relationship metadata to live engine responses.

## Fields

When a processing result contains click trains, the service now attaches `relatedTrainIds` to matching click-related JSON objects:

- `clicks`
- `clickFeatures`
- `clickClassifications`
- `clickLocalisations`
- `clickBearings`

## Why

Archived detector events already expose click-to-train links. Live processing responses now expose the same relationship so browser clients can connect current detections to tracks without waiting for archive queries.

## Validation

The service smoke test posts three synthetic click chunks and asserts that the third live response includes at least one click with `relatedTrainIds`.
