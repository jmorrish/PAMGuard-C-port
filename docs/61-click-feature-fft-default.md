# Click feature FFT default

Click feature extraction now treats `click.features.fftLength` as an explicit override.

If `click.features.fftLength` is omitted or set to `0`, the service normalises it to the session FFT length from `fft.length` when the session is created. `AnalysisSession` also applies the same default defensively before constructing the click feature extractor.

This keeps the reported session configuration, browser-created sessions, archived results, and runtime feature spectra aligned with the same FFT contract. Operators can still override the click feature FFT length independently when they need PAMGuard-style module tuning that differs from the spectrogram FFT.
