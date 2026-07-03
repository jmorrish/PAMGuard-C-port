# Session configuration validation

Session creation and persisted-session startup reload now use the same validation path before constructing an engine instance.

The service rejects configurations that would make detector maths undefined or route audio incorrectly:

- `sessionId` must be non-empty and contain only letters, numbers, `-`, `_`, and `.`.
- `sampleRateHz` must be positive.
- `channelCount` must be in the service-supported range `1..1024`.
- `fft.length` must be a non-zero power of two.
- `fft.hop` must be positive and no larger than `fft.length`.
- `fft.channels` must be non-empty, unique, and inside `channelCount`.
- Array hydrophone channels must be unique and inside `channelCount`; positions and sensitivity must be finite.
- Click detector bitmaps must reference only configured channels.
- Click localisation requires at least two selected click detector channels.
- Click feature FFT length must be a non-zero power of two after defaulting to `fft.length`.
- Whistle/moan region fragmentation method must be `0..3`, and connect type must be `4` or `8`.

Invalid API requests return HTTP `400` with a field-oriented error message. Invalid persisted session files are skipped during startup, with the error written to stderr.
