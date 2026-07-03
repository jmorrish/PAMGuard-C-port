# Reference Tools

Reference tools generate PAMGuard-side golden fixtures. These are not substitutes for PAMGuard; they are thin exporters that call PAMGuard Java classes so the C++ port can compare against the real implementation.

Initial target:

- Window function fixtures from `Spectrogram.WindowFunction`
- FFT fixtures from `fftManager.FastFFT`

Future targets:

- Click trigger and click extraction fixtures from `clickDetector.ClickDetector`
- Whistle/moan connected-region and contour fixtures from `whistlesAndMoans.WhistleToneConnectProcess`
