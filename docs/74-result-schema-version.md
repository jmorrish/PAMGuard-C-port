# Result schema version

All engine result JSON bodies now include:

```json
{
  "schemaVersion": 15
}
```

This applies to:

- `POST /sessions/{sessionId}/pcm-f32le` responses;
- `POST /sessions/{sessionId}/flush` responses;
- optional NDJSON archive records.

The version identifies the response-envelope schema, not PAMGuard detector maths. Increment it when output field names, required fields, or nested result shapes change in a way that clients need to branch on.

`GET /health` reports the current result schema version as `resultSchemaVersion`.

The HTTP service smoke asserts the health endpoint, live PCM result body, and archived result records all report the current version.

## Version history

- `25`: Adds an `ishmaelDetections` array (ported PAMGuard IshmaelDetector energy sum + peak picker): threshold crossings of the band energy sum with duration/refractory gating, reporting the event's sample span, peak value/time, and the configured band. Configured via `ishmael: {enabled, f0, f1, ratioF0, ratioF1, useRatio, useLog, adaptiveThreshold, longFilter, spikeDecay, outputSmoothing, shortFilter, thresh, minTimeSeconds, maxTimeSeconds, refractoryTimeSeconds}`.
- `24`: Adds an `ltsa` array (ported PAMGuard LTSA): per channel per completed averaging period, `magnitude` holds the RMS spectral magnitude per FFT bin (`sqrt(mean magnitude-squared)`, uncalibrated exactly as the reference stores it), with the wall-clock-aligned window, `nFft`, and the covered sample span. Configured via `ltsa: {enabled, intervalSeconds}`.
- `23`: Adds a `noiseBands` array (ported PAMGuard noiseBandMonitor): per channel per output interval, ANSI band `rmsDb`/`peakDb` levels ascending in frequency, calibrated to dB re 1 uPa when `acquisition` and hydrophone sensitivity/preamp gain are configured, honest relative dB otherwise.
- `22`: Adds an `echo` boolean to click results when online echo detection runs (`click.echo.runOnline`), via the ported PAMGuard SimpleEchoDetector. Absent when the gate is off, so unchecked is distinguishable from not-an-echo. With `discardEchoes`, echoes are removed before any output at all.
- `21`: Adds `earthWorldVectors` to `gridBearing` and `lsqBearing` and `pairBearingWorldVectors`' counterpart `pairBearingEarthWorldVectors`, present when session config declares `array.orientation` (heading/pitch/roll of the whole array in the world). Same vectors rotated by `getRealWorldVectors`' quaternion; without a declared orientation the fields are absent rather than silently carrying array-frame values.
- `20`: Adds `earlierRegionCount` to `whistleGroups` entries — members that completed in earlier chunks and are therefore not addressable through `regionIndices`. `firstStartSample` now means the group's true first sample rather than this chunk's earliest member, so a group re-reported in a later chunk no longer appears to move forward in time.
- `19`: Adds `pairBearingWorldVectors` to click localisation and whistle delay entries carrying a pair bearing (always two cone vectors, the left/right pair), and `worldVectors` to the `lsqBearing` object (always one). LSQ's vector is **not** array-axis rotated: it fits raw inter-hydrophone vectors, so its angles are already in that frame.
- `18`: Adds `worldVectors` to the `gridBearing` object — the same direction as unit `{x, y, z, cone}` vectors in the hydrophone array's own xyz frame, via PAMGuard's `AbstractLocalisation.getWorldVectors`. One vector for a volume sub-array; two for a plane (mirror pair) or line (left/right cones).
- `17`: Adds a `gridBearing` object to click localisations and whistle delays, carrying the ported PAMGuard `MLGridBearingLocaliser2` result for sub-arrays whose shape selects it. Angles are the reference's theta/phi in the array's principal axis frame, not compass azimuth/elevation. Session config gains `array.hydrophones[].xErrorM`/`yErrorM`/`zErrorM`. `lsqBearing` and the whistle region `bearing` object are unchanged.
- `16`: Adds `arrayShape` and `bearingLocaliser` to every click localisation and whistle delay entry, reporting which localiser PAMGuard's `BearingLocaliserSelector` picks for that sub-array's shape. Localiser choice is now made by shape rather than channel count, so a **line** sub-array of four or more hydrophones no longer produces `lsqBearing` — those solves were rank deficient and reported no bearing anyway.
- `15`: Adds a `whistleGroups` array associating whistle contours detected on different channels as one call, via the ported detection grouper.
- `14`: Adds a `clickTrainClassifications` array carrying classifier verdicts for the ICI tracker's click trains, keyed by `trainId`.
- `13`: Adds a `classification` object on `mhtClickTrains` when `click.train.classifier` is enabled (junk flag, species id, every classifier's verdict, and the template correlation).
- `12`: Whistle regions now measure **every** channel pair (matching PAMGuard `WhistleDelays`), so multi-channel sessions report more `delays` entries per region, and groups with four or more fully-geometry hydrophones gain an `lsqBearing` object with an unambiguous region bearing.
- `11`: Adds a region-level `bearing` object on `whistleDelays` entries (PAMGuard `WhistleBearingInfo` equivalent, including the ambiguity flag).
- `10`: Adds `mhtClickTrains` output when `click.train.algorithm` is `"mht"` — the ported PAMGuard MHT stack as an alternative click train former — plus the `trainAlgorithm` config echo field.
- `9`: Adds `whistleDelays` entries carrying per-region cross-channel whistle contour delays with geometry, physical units, and pair bearing metadata.
- `8`: Pair bearing angles now follow PAMGuard's array-axis reference direction (spacing negated when the pair vector aligns with the pair's principal array axis). Field shapes are unchanged; angle values flip for axis-aligned pairs.
- `7`: Adds pair bearing aggregation (`pairBearingCount`, `meanPairBearingRadians`, `meanPairBearingDegrees`) to click train localisation pair-delay summaries.
- `6`: Adds an `lsqBearing` object on click localisations (PAMGuard LSQ bearing localiser) for sessions with four or more fully-geometry hydrophones and positive `spacingErrorM`.
- `5`: Adds PAMGuard pair bearing outputs (`pairBearingRadians`, `pairBearingDegrees`, `pairBearingErrorRadians`) on geometry-constrained click localisation delay pairs, plus array error/wobble session config fields.
- `4`: Adds audio-channel mapping and geometry constraint metadata to click localisation and click train localisation pair-delay outputs.
- `3`: Adds physical delay-unit fields to click localisation and click train localisation outputs.
- `2`: Adds named whistle-contour timing, duration, frequency envelope, peak, and sweep-rate summary fields while retaining the original raw contour arrays.
- `1`: Initial versioned result envelope.
