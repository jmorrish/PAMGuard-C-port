# Audio Archive and Replay

Date: 2026-07-22

## Purpose

Delivers WP3's "audio archive/replay store" and completes its acceptance line "replay produces identical PCM chunks for deterministic analysis" (`docs/05`) in its strongest form: replay feeds the **same bytes through the same chunk boundaries** the original session analysed.

## Design

Enabled by `PAMGUARD_AUDIO_ARCHIVE_DIR`. When set, every accepted PCM POST appends its raw f32le body to a per-session `.f32le` file and one NDJSON index line — `{startSample, frames, timeMs, byteOffset, byteLength, sampleRateHz, channelCount}` — under a dedicated mutex. Append-only raw-plus-index was chosen over WAV because it needs no header patching on close (crash-safe), has no 4 GB ceiling, and preserves the exact bytes as posted; the acquisition facts ride in every index record so the archive is self-describing.

- **`GET /sessions/{id}/audio/index`** — the records plus `totalFrames` and a `contiguous` flag. Gaps and overlaps stay visible as `startSample` discontinuities, which also serves WP3's "dropouts are visible" line from the stored side.
- **Replay** — the job queue (`docs/208`) gains a second source: `POST /jobs {audioSession: "<id>", session:{...}}` re-analyses a session's archived audio. Unlike WAV jobs, replay preserves the **original** chunk boundaries, start samples, timestamps, and gaps — including the discontinuities — because chunk-boundary-sensitive state (echo anchors, kernel-smoothing columns, MHT coasting) makes chunking part of the result. Re-analysis with a *different* config is the intended scientific workflow: same bytes, new detector settings.

## Validation

The job smoke extends to the full round trip: a live session receives two raw PCM chunks with a **deliberate one-chunk gap**; the audio index reports both records, the right frame total, and `contiguous: false`; then a replay job over that archived audio completes with **exactly the live run's click count** (20 = 20) and the original two chunk boundaries. Suite `75/75`.

## Claim boundary

The archive stores what was **accepted** — a chunk rejected for size or shape is not archived, and nothing records audio the client never sent. No retention policy applies to audio files yet (the grouped archive-retention utility handles result archives only); raw audio at 384 KB/s/channel fills disks quickly and a deployment must plan for that. There is no download endpoint for the raw audio — replay happens server-side; exporting audio is a file-copy operation on the archive directory by design, not an HTTP surface.

Replay requires the audio and the job queue both enabled (`PAMGUARD_AUDIO_ARCHIVE_DIR` + `PAMGUARD_JOB_AUDIO_DIR`); index records written by builds before this feature lack the acquisition fields and are rejected with a clear error rather than guessed at.
