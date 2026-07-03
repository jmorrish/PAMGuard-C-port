# Click train bearing summary

Engine results now include `clickTrainBearings`.

This is a derived summary built from:

- the existing click train grouping output;
- valid per-click far-field bearing outputs whose `clickStartSample` belongs to that train.

For each reported train, the service returns:

- `trainId`;
- `channelBitmap`;
- `firstStartSample`;
- `lastStartSample`;
- `clickCount`;
- `bearingCount`;
- `valid`;
- averaged unit vector;
- `azimuthDegrees`;
- `elevationDegrees`;
- `meanResidualRmsSeconds`.

This does not replace a full PAMGuard click-train localisation clone. It gives the web/API layer a train-level bearing summary using the existing per-click localisation evidence, while keeping the current train grouping decisions unchanged.
