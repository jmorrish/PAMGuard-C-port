# Click train localisation summary

Engine results now include `clickTrainLocalisations`.

This is a derived train-level delay summary built from existing per-click localisation results. For each reported click train, the engine groups localisations whose `clickStartSample` belongs to the train and reports averaged delay evidence per channel pair.

Each summary includes:

- `trainId`;
- `channelBitmap`;
- `firstStartSample`;
- `lastStartSample`;
- `clickCount`;
- `localisationCount`;
- `valid`;
- `pairDelays`.

Each pair delay includes:

- `pairIndex`;
- `channelA`;
- `channelB`;
- `delayCount`;
- `meanDelaySamples`;
- `meanDelayScore`.

This is intentionally a derived API/backend summary. It does not claim to be full PAMGuard click-train localisation parity; it exposes the train-level view needed by the web/API layer while PAMGuard reference fixture work continues.
