# PAMGuard Parity Strategy

## Principle

The C++ engine is correct only when it can reproduce PAMGuard behaviour for the supported module/config/source combinations within agreed tolerances.

## Reference Implementation

The Java source in the parent repository is the oracle. Important reference areas include:

- `src/Spectrogram/WindowFunction.java`
- `src/fftManager/PamFFTProcess.java`
- `src/fftManager/FastFFT.java`
- `src/clickDetector/ClickDetector.java`
- `src/clickDetector/ClickParameters.java`
- `src/whistlesAndMoans/WhistleToneConnectProcess.java`
- `src/whistlesAndMoans/WhistleToneParameters.java`

## Golden Fixture Levels

Parity should be tested at several layers:

- Window values and window gain
- FFT complex bins and power bins
- Spectrogram frame sample indices and timestamps
- Spectrogram noise reduction output
- Click trigger functions
- Click extraction windows and timestamps
- Click spectra and features
- Time delays and localisation outputs
- Whistle connected regions
- Whistle contour start/end/frequency points

## Tolerance Policy

Tolerance must be explicit per stage.

Early defaults:

- Window values: exact within floating-point representation
- Sample indices: exact
- Channel grouping: exact
- FFT magnitudes: strict numeric tolerance after matching scaling/packing
- Detector event times: exact sample index where expected, otherwise documented tolerance
- Contour points: exact bin/time match where expected, otherwise documented tolerance

## Known PAMGuard Details To Preserve

- PAMGuard Hann window uses `length` in the cosine denominator.
- Hamming, Blackman, Blackman-Harris, and Bartlett follow the Java implementation exactly.
- Window gain is RMS gain: `sqrt(sum(w*w) / n)`.
- FFT block emission uses shared sample timelines and overlap/hop behaviour.
- Detector state is history-dependent and must not be shared across unrelated sources/configs.

## Known Open Questions

- Exact JTransforms real FFT packing/scaling semantics must be captured in fixture tests.
- PAMGuard comments refer to FFT data scaled to `1/n`; implementation details need fixture confirmation.
- Click detector parity will require exposing intermediate trigger/background values from Java.
- Whistle/moan parity will require exporting connected-region intermediates, not just final contours.

