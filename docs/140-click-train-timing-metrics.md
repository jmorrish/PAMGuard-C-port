# Click train timing metrics

Date: 2026-07-01

## What changed

Click train summaries now include richer timing metrics:

- `durationSamples`
- `durationSeconds`
- `timeSpanSeconds`
- `minIciSeconds`
- `maxIciSeconds`
- `iciCv`
- `clickRateHz`

Existing fields remain:

- `lastIciSeconds`
- `meanIciSeconds`
- `medianIciSeconds`
- `stdIciSeconds`
- click sample/time arrays
- completion state

## Rate definition

`clickRateHz` is interval-based:

```text
(clickCount - 1) / durationSeconds
```

It is zero when a train has fewer than two clicks or zero duration.

## Why this matters

PAMGuard click train work depends heavily on ICI and train timing behavior. These metrics make train outputs more useful for parity review, downstream classification, and web/archive inspection while the full PAMGuard click train module is still being pinned down.

## Validation

`click_train_tracker_foundation` now checks:

- duration in samples and seconds
- time span
- min/max ICI
- mean/median/std ICI
- ICI coefficient of variation
- interval click rate
