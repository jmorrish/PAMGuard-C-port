# Ingest session shape check

Compatibility boundary (2026-07-25): this documents the retained session
bridge. The active-project supervisor performs the equivalent sample-rate and
channel check against a stable Acquisition unit before launching FFmpeg.

`ffmpeg_stream_ingest` now queries `GET /sessions/{sessionId}` before launching FFmpeg.

The bridge verifies:

- engine `sampleRateHz` matches `--sample-rate`;
- engine `channelCount` matches `--channels`.

If either value differs, the bridge exits before posting PCM. This prevents a high-risk multi-channel failure mode where decoded PCM is valid bytes but interpreted with the wrong channel interleave or sample-rate timeline by the engine session.
