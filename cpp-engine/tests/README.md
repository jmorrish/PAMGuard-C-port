# Parity Tests

This directory is reserved for PAMGuard parity fixtures.

Planned fixture types:

- Window function CSV fixtures generated from `Spectrogram.WindowFunction`
- FFT complex-bin fixtures generated from `fftManager.PamFFTProcess`
- Click detector trigger/intermediate fixtures generated from `clickDetector.ClickDetector`
- Whistle/moan connected-region and contour fixtures generated from `whistlesAndMoans.WhistleToneConnectProcess`

The C++ engine must compare against these fixtures before detector work is considered complete.

