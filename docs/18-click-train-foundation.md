# Click Train Foundation

Date: 2026-06-30

This checkpoint adds the first click-train/event grouping foundation to the C++ backend.

## Implemented

- Added `ClickTrainTracker`.
- Maintains active trains per channel bitmap.
- Uses the PAMGuard offline-event ICI calculation rule:
  - calculate ICI from sample numbers;
  - calculate ICI from millisecond timestamps;
  - if the two agree within 1 second, use sample-based ICI;
  - otherwise use millisecond-based ICI.
- Emits train summaries once a train reaches `minClicks`.
- Emits a completed summary when a new click exceeds `maxIciSeconds` and closes the prior train.
- Calculates IDI/ICI summary values using PAMGuard-compatible statistics:
  - mean;
  - sorted median;
  - population standard deviation.
- Added service config:

```json
{
  "click": {
    "enabled": true,
    "train": {
      "enabled": true,
      "maxIciSeconds": 0.2,
      "minClicks": 3
    }
  }
}
```

- Added service result array:

```json
"clickTrains": []
```

## Validation

CTest status after this checkpoint:

```text
20/20 tests passed
```

New test:

```text
click_train_tracker_foundation
```

HTTP smoke test with three synthetic clicks spaced 0.1 seconds apart:

```json
{"clicks":3,"clickTrains":1,"trainClicks":3,"meanIci":0.1}
```

## Important scope note

This is not yet full PAMGuard MHT click train detector parity. The modern PAMGuard click train detector includes MHT kernels, chi-square variables, pruning, coasting, species defaults, train classifiers, and localisation/classification post-processing.

This checkpoint establishes the session/service plumbing and PAMGuard-compatible ICI summary layer so the full MHT implementation can be added behind a tested result contract.
