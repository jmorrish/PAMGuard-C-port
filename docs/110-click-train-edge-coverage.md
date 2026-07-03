# Click Train Edge Coverage

Date: 2026-07-01

This checkpoint expands the focused C++ click train tracker test.

## Added cases

- sub-minimum trains are not reported during process or flush;
- a large ICI gap discards a too-short train and starts a new train;
- separate channel bitmaps maintain independent active trains.

## Test

```text
click_train_tracker_foundation
```

The same CTest target now covers the original foundation path plus these edge cases.
