# Web UI continuity status

The browser dashboard now displays the latest PCM `sampleContinuity` state returned by `POST /sessions/{sessionId}/pcm-f32le`.

Healthy stream posts should show:

- `first` on the initial chunk;
- `contiguous` after that.

`gap` or `overlap` indicates the posted `startSample` did not match the engine session's expected timeline and should be investigated before trusting stateful detector outputs from that session.
