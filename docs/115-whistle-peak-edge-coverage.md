# Whistle Peak Edge Coverage

Date: 2026-07-01

This checkpoint expands the focused whistle peak detector test.

## Added cases

- invalid FFT length is rejected;
- invalid sample rate is rejected;
- default `searchBin1` resolves to `fftLength / 2 - 2`;
- wrong magnitude slice size is rejected;
- reset is reproducible for the synthetic fixture;
- overly broad over-threshold slices are suppressed;
- peaks narrower than `minPeakWidth` are rejected.

## Test

```text
whistle_peak_basic_parity
```
