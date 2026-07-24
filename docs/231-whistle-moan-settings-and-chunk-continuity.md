# Whistles & Moans settings and chunk continuity

Date: 2026-07-24

## Corrected detector wiring

The authoritative `whistlesAndMoans.WhistleToneConnectProcess` does not use
the older `whistleDetector.BetterPeakDetector` to decide which pixels enter a
contour. `ShapeConnector.newData` reads the output of
`SpectrogramNoiseProcess` and marks every positive-power bin in the configured
frequency interval:

```java
newCol[i] = complexData.magsq(i) > 0;
```

The C++ session now follows that path directly. `whistle.enabled` remains the
optional legacy BetterPeak-style result, while `whistle.regionEnabled` is the
Whistles & Moans contour detector. Turning legacy peaks off cannot suppress
contours.

## Settings semantics

The region defaults now match `WhistleToneParameters`:

- 8-connected pixels;
- minimum 20 pixels and 10 slices;
- re-link fragmentation (`FRAGMENT_RELINK`, value 3);
- maximum crossing length 5;
- remove shape stubs;
- minimum frequency 0 and maximum frequency Nyquist.

`minFrequencyHz` and `maxFrequencyHz` are exposed through the service,
OpenAPI, `.psfx` converter, session readback, and browser. Frequency conversion
uses `DataBlock2D.value2bin` followed by Java's integer truncation and clamps
both ends to `dataWidth - 1`. The upper bin is exclusive. As in
`WhistleToneParameters.getMaxFrequency`, a non-positive or at/above-Nyquist
maximum becomes Nyquist.

The browser separates contour settings, the four-stage spectrogram noise
chain, and the optional legacy peak output. Its default Whistles & Moans setup
enables all four noise methods, as PAMGuard's Restore Defaults action does.

## Validation

`session_whistle_delay_wiring` now runs with the legacy peak detector disabled
and proves that:

- contours and their cross-channel delays are still produced;
- the default 8 dB noise threshold admits the test contour while 200 dB mutes
  it;
- a contour that starts in one PCM chunk and finishes in the next is not
  prematurely closed or lost, and its reported start sample remains before
  the split;
- the frequency-bin conversion preserves the authoritative truncation,
  Nyquist, and clamping cases.

The real `.psfx` fixture now maps whistle frequencies in Hz, sets the legacy
peak output off, and round-trips all mapped contour and noise settings through
the live service smoke.

## Subsequent closures

The formerly open background-spectrum output is now ported and served at
result schema v32 (`docs/233`). Ordinary audio-channel
`GroupedSourceParameters` behavior is now implemented too: only the first
channel of each group creates contours/backgrounds and delay measurements stay
inside that group (`docs/234`). Beamformer sequence maps remain outside the
engine's source model.
