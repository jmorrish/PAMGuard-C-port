# FFT Window Configuration Surface

Date: 2026-07-01

This checkpoint exposes the existing PAMGuard-parity FFT window implementations through the service and browser configuration paths.

## Implemented

- Added `fft.windowType` parsing in `pamguard_engine_service`.
- Accepted values:
  - `Rectangular` / `0`;
  - `Hamming` / `1`;
  - `Hann` / `2`;
  - `Bartlett` / `3`;
  - `Blackman` / `4`;
  - `Blackman-Harris` / `5`.
- Session status now reports:
  - `fft.windowType`;
  - `fft.windowTypeId`.
- Browser console now includes an FFT window selector.
- OpenAPI now documents `fft.windowType`.

## Parity note

The maths was already covered by window and FFT parity fixtures. This checkpoint makes the parity-tested window choices operator-configurable per session.
