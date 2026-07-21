# Result schema version

All engine result JSON bodies now include:

```json
{
  "schemaVersion": 13
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
