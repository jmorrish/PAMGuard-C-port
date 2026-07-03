# Click Feature Edge Coverage

Date: 2026-07-01

This checkpoint expands the focused click feature extraction test.

## Added cases

- non-positive sample rate is rejected;
- non-power-of-two FFT length is rejected;
- empty click waveform is rejected;
- minimal waveform uses PAMGuard-style minimum FFT length;
- channel index falls back to waveform index when click channel metadata is absent.

## Test

```text
click_feature_basic_parity
```
