# Matched-Template Click Classifier

Date: 2026-07-23

## Purpose

Ports PAMGuard's matched-template click classifier (`matchedTemplateClassifer`: `MTProcess` + `MTClassifier`) — the click-level species discriminator that cross-correlates each detected click against a *match* template and a *reject* template and classifies on the difference of the correlation peaks. Item 6, the last of the low-hanging-fruit list. The engine already served the click-train-level template correlation (docs on the CT classifier chain); this is the finer per-click classifier.

## Reference semantics ported

Per click, per channel: optionally window the waveform around its Hilbert-envelope peak (`ClickLength.createLengthData`: analytic envelope via power-of-two FFT, `SmoothingFilter` box smoothing, walk out to the `lengthdB` drop; then `createRestrictedLenghtWave`: centre `restrictedBins` on the event, zero-pad, Hann window). Normalise (peak/RMS/none), FFT at the waveform's own length, and for each template pair run `MTClassifier.calcCorrelationMatch`: multiply the click spectrum by the conjugated template spectrum, inverse FFT, take `2×` the real part, and score `max(match) − max(reject)`. Per template pair the annotation keeps the best result across channels; the click classifies when the score beats `thresholdToAccept`, aggregated by `channelClassification` (all channels / any channel).

**Quirks preserved and pinned:**

- `FastFFT` delegates to JTransforms, so the FFTs run at **arbitrary lengths** with JTransforms' packed real-spectrum layout — and the reference multiplies the **packed bin 0** (DC and Nyquist sharing one complex slot) as if it were an ordinary complex bin, and `conj()` negates the packed Nyquist. A new `dsp::JtFft` reproduces `realForward`'s even- and odd-length packings and `complexInverse` exactly (radix-2 fast path, direct DFT otherwise).
- The template FFT is cached **by sample rate only**, so it freezes at the **first** click's FFT length; later clicks of other lengths correlate against it over `min(template, click)` bins.
- A template longer than the FFT length is windowed around its peak with an end-**exclusive** subarray that leaves it one sample short.
- Peak normalisation divides by the **signed** maximum (`PamArrayUtils.max`), not the absolute peak.
- Template upsampling is `PamInterp.interpWaveform` verbatim: FFT, copy the packed half-spectrum scaled by the ratio, **unscaled** inverse, take real parts.
- The `2×` on the inverse FFT ("to get same as ifft function in MATLAB - dunno why this is...") and the NaN reject path: a zeroed/none reject template propagates NaN through its whole branch and the score falls back to the match correlation alone (`Double.isNaN` check, ported).
- `channelClassification = 2` (use means) classifies nothing — the reference's aggregation simply has no branch for it.

## Configuration and results

`matchedTemplate`: `{enabled, normalisationType (0 peak | 1 RMS — the reference default | 2 none), peakSearch, peakSmoothing, lengthDb, restrictedBins, channelClassification (0 all | 1 one), classifiers: [{thresholdToAccept, match: {name, sampleRateHz, waveform[]}, reject: {...}}]}` — template waveforms travel inline in the session config.

Results: `matchedTemplateClassifications` at schema v26 — per click: `clickIndex`, `clickStartSample`, `classified`, and per template pair `{threshold, matchCorr, rejectCorr}` (rejectCorr omitted when NaN). Runs on finished clicks, after the echo gate.

The importer maps `MatchedTemplateParams` with full template waveforms; the sample `.psfx` carries a classifier with synthetic 48 kHz templates (the reference's 192 kHz defaults would need decimation against the 96 kHz sample acquisition — see the boundary below).

## Validation

`matched_template_parity` (new, suite `80/80`): `MatchedTemplateFixtureExporter` drives the **real** `MTClassifier.calcCorrelationMatch` (real FastFFT/JTransforms, interpolation, normalisation), the real `ClickLength` peak search, the real `createRestrictedLenghtWave`, and the real `normaliseWaveform` — only the 15-line channel-aggregation loop is transcribed from `MTProcess.newClickData`, mirrored in the port. Six cases, 13 clicks, 17 correlation results, **maxRelError 5.1e-15**: RMS/peak/none normalisation, peak search on and off, even (300) and odd (301) non-power-of-two FFT lengths, a shorter click against the frozen 300-length template FFT, a 24 kHz template upsampled to a 48 kHz session, a zeroed reject template (NaN path), and two-channel require-one vs require-all aggregation.

## Claim boundary

Template **decimation** (template rate above the session rate) is not ported: the reference calls jpamutils' `WavInterpolator.decimate`, an external library whose source is not in this tree, and substituting different resampling would silently change every correlation. Sessions configuring such templates are refused at creation with that reason — which means PAMGuard's shipped 192 kHz default templates only work on sessions at ≥ 192 kHz, exactly the honest statement of what is proven. The comparison tolerance is 1e-8 relative (observed 5.1e-15), not bit-exact, because JTransforms' mixed-radix FFTs and the engine's DFT round differently. `MTProcess`'s annotation/symbol/bespoke-flag plumbing (display and storage) and the optional pre-classification FFT filter (`enableFFTFilter`, default off) are out of scope; the per-click type byte is subsumed by the `classified` flag.
