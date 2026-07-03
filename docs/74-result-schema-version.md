# Result schema version

All engine result JSON bodies now include:

```json
{
  "schemaVersion": 4
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

- `4`: Adds audio-channel mapping and geometry constraint metadata to click localisation and click train localisation pair-delay outputs.
- `3`: Adds physical delay-unit fields to click localisation and click train localisation outputs.
- `2`: Adds named whistle-contour timing, duration, frequency envelope, peak, and sweep-rate summary fields while retaining the original raw contour arrays.
- `1`: Initial versioned result envelope.
