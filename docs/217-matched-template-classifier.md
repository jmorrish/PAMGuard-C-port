# Matched-Template Click Classifier

Date: 2026-07-23

Graph-native controlled-unit update: 2026-07-25

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

The active-project controlled unit has one strict canonical settings document:
`{clickType, normalisationType, peakSearch, peakSmoothing, lengthDb,
restrictedBins, channelClassification, classifiers:
[{thresholdToAccept, normalisation, matchTemplate, rejectTemplate}]}`.
Source identity is owned by the graph binding, not duplicated in settings.
The low-level `matchedTemplate` session object above is retained only as the
compatibility/oracle contract.

## Validation

`matched_template_parity` drives the **real** `MTClassifier.calcCorrelationMatch` (real FastFFT/JTransforms, interpolation, normalisation), the real `ClickLength` peak search, the real `createRestrictedLenghtWave`, and the real `normaliseWaveform` — only the 15-line channel-aggregation loop is transcribed from `MTProcess.newClickData`, mirrored in the port. Eight cases, 16 clicks, 20 correlation results, **maxRelError 5.1e-15**: RMS/peak/none normalisation, peak search on and off, even (300) and odd (301) non-power-of-two FFT lengths, a shorter click against the frozen 300-length template FFT, a 24 kHz template upsampled to a 48 kHz session, a 192 kHz template decimated to 48 kHz, split global/per-classifier normalisation, a zeroed reject template (NaN path), and two-channel require-one vs require-all aggregation. The fixture SHA-256 is `24029E7EBCE3855A3069218812374789D6766D2953C7B1D9C8E4276BECE98968`.

## Claim boundary

Template decimation is now implemented with the same four-pole Butterworth
low-pass and natural-cubic interpolation sequence used by jpamutils'
`WavInterpolator`; the dedicated resampling fixture pins the result. The
comparison tolerance remains 1e-8 relative (the original correlation fixture
observed 5.1e-15), not bit-exact, because JTransforms' mixed-radix FFTs and the
engine's DFT round differently.

The graph-native click path now retains the matched-template annotation and
applies Java's click-type override/reset semantics. One deliberate portable
deviation avoids a Java byte/UI defect: canonical click types `128..255` remain
unsigned and stable across save/reopen, and the dialog's displayed `256` maps
to portable `0`. Java stores that spinner in a signed byte, making `128..255`
negative on reopen and wrapping `256` to zero.

This remains a partial module. The browser does not yet accept Java's
`CTDataUnit` source or classify its `AverageWaveform`; it does not replace or
write `CTClassifierType.MATCHEDCLICK` click-train flags; and it does not expose
the Java `ClickTypeProvider` code/name integration. Template import supports
CSV and built-in templates, but not Java's MAT importer. The optional dormant
pre-classification FFT-filter settings, Java symbol/display preferences, and
viewer-only offline reclassification are also outside the current live
runtime claim.
