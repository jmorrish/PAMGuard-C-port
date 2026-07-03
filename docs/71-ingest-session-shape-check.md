# Ingest session shape check

`ffmpeg_stream_ingest` now queries `GET /sessions/{sessionId}` before launching FFmpeg.

The bridge verifies:

- engine `sampleRateHz` matches `--sample-rate`;
- engine `channelCount` matches `--channels`.

If either value differs, the bridge exits before posting PCM. This prevents a high-risk multi-channel failure mode where decoded PCM is valid bytes but interpreted with the wrong channel interleave or sample-rate timeline by the engine session.
