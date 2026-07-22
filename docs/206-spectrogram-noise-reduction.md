# Spectrogram Noise Reduction

Date: 2026-07-22

## Purpose

Closes the last unported deliverable of the whistle/moan work package (`docs/05`, WP6: "Spectrogram noise reduction path"). In PAMGuard, `SpectrogramNoiseProcess` sits between the FFT engine and `WhistleToneConnectProcess`, running up to four `SpecNoiseMethod`s over each complex FFT slice; until now the engine's whistle path consumed raw spectra with only the peak detector's own background estimate standing in.

## Reference semantics ported

The four methods, run in `SpectrogramNoiseProcess`'s fixed order over a **copy** of each slice:

1. **Median filter** (`SpectrogramMedianFilter` + `whistlesAndMoans.MedianFilter`, Paul White's `medfilt_prw_c.c`): the running median of each slice's magnitudes, divided off each complex bin. Edge bins pad with the data itself, not zeros; even filter lengths gain one; the bubble sort's comment says descending while its comparison sorts ascending — behaviour, not comments, is what is ported.
2. **Average subtraction** (`AverageSubtraction`): a per-channel decaying average of log magnitude, divided off. Faithful oddities: a ten-slice run-in that *divides the data by ten* rather than by the average, an accumulation doubled by `runInScale`, exact-zero and NaN bins skipped entirely, and the scale taken from the average **before** this slice updates it.
3. **Gaussian kernel smoothing** (`KernelSmoothing`): a 3×3 {1 2 1 / 2 4 2 / 1 2 1}/16 kernel over power, applied as a scale factor to the **middle** column's complex values — so the method delays its output by one slice, and the first two slices pass through untouched. A zero centre bin divides by zero, exactly as `Math.sqrt(dumTot/cenVal)` does.
4. **Threshold** (`SpectrogramThreshold`): bins below `10^(dB/10)` in power become zero; survivors become `(1, 0)` unless `finalOutput` is `OUTPUT_INPUT`. With `OUTPUT_RAW` (PAMGuard's default), `pickEarlierData` then copies the **un-noise-reduced input** back into surviving bins — downstream sees raw data where something was detected and zeros elsewhere.

## A zero-transcription fixture

Every one of the four methods constructs headlessly, `FFTDataUnit` and `ComplexArray` are plain classes, and the chain loop is four lines — so the exporter drives the **real PAMGuard classes end to end with no transcription at all**. Eight cases (each method alone, threshold in all three output modes, and the full chain in two modes) over a 24-slice synthetic spectrogram with a drifting tonal ridge, a transient, and exact-zero bins.

The raw input slices travel **in the fixture**, so the C++ check replays the same bytes rather than re-deriving them from a duplicated formula that could hide a shared mistake.

Result: all 8 cases × 24 slices × 32 complex bins match with `max_abs_error` **0** — bit-exact against PAMGuard's own bytecode, first run.

## Engine wiring

`whistle.noise` in session config: `medianFilter`/`medianFilterLength`, `averageSubtraction`/`updateConstant`, `kernelSmoothing`, `threshold`/`thresholdDb`/`finalOutput` — defaults matching PAMGuard's parameter classes, validated on parse.

The reduced slice feeds **both** the peak/region detectors and the retained FFT history, because `WhistleDelays.sourceData` is the whistle process's parent block — the noise process's *output* — so PAMGuard correlates whistle delays on noise-reduced data. The engine's frames keep their N/2+1 layout; the reducer runs over the PAMGuard-packed N/2 view (DC and Nyquist sharing slot zero) and the result unpacks back, which is exactly the representation PAMGuard's chain operates on.

The served spectrogram and the click path are untouched: PAMGuard's noise process feeds the whistle detector specifically.

No result fields change — this is config-side, schema stays at v22. The `whistle` config echo gains four `noise*` booleans.

The project importer maps `SpectrogramNoiseSettings` (which rides inside `WhistleToneParameters`: `runMethod` flags parallel to the fixed method order, per-method settings in the same order), and the sample `.psfx` now enables median + average + threshold with non-default values.

## Validation

`spectrogram_noise_parity` (new) replays the fixture — 8 cases, exact. `session_whistle_delay_wiring` gains a placement check: a 200 dB threshold mutes every whistle region from a signal that otherwise produces them, and a −100 dB threshold leaves them detectable — pinning that the reducer actually sits in the path, distinct from the fixture pinning its maths. Full CTest suite passes `74/74`.

## Claim boundary

The noise-reduced data is used exactly where PAMGuard uses it: the whistle chain. PAMGuard configurations that point *other* consumers (spectrogram displays, other detectors) at a noise process output have no engine equivalent.

`SpectrogramNoiseProcess`'s `delayedInputData` shuffle is not ported: the reference maintains it but `pickEarlierData` is called with the **current** slice's input, so the delayed store is written and never read — dead state, recorded here rather than reproduced.

Kernel smoothing's one-slice delay means PAMGuard's noise output block runs a slice behind its input block, and PAMGuard compensates nowhere visible to this chain; the engine reproduces the same behaviour, so contour timing with kernel smoothing enabled shifts by one hop exactly as the reference's does.
