# Offline Job Queue

Date: 2026-07-22

## Purpose

Delivers WP7's "job queue / worker scheduler" and its acceptance line "offline jobs can be queued and replayed" (`docs/05`). Until now the engine only analysed audio pushed at it live; a queued WAV analysis had to be scripted client-side.

## Design

A job is a WAV file analysed through the same session machinery as live audio — the same `parse_config`, the same `AnalysisSession`, the same archive writers — so job output is queryable through the **existing** archive endpoints under the job's session id (`job-<jobId>`), with nothing new to learn on the query side.

- **Enable**: set `PAMGUARD_JOB_AUDIO_DIR` (the root jobs may read from) and optionally `PAMGUARD_JOB_WORKERS` (default 1). Without the env var, `/jobs` returns 404 and no worker threads exist.
- **Submit**: `POST /jobs` with `{jobId?, wavFile, session{...}}`. `wavFile` is relative to the audio root; `session` is the same body `/sessions` accepts, minus `sampleRateHz`/`channelCount`, which come from the WAV unless pinned — and a pinned mismatch is an error, not a silent resample. Bad paths and unparseable configs fail at submission (400), not later as a failed record.
- **Run**: worker threads pop the queue, read the WAV, and process it in one-second chunks, updating `processedFrames` and detection counters as they go. Each chunk's full result body is archived (when archiving is enabled), then the flush result.
- **Observe**: `GET /jobs`, `GET /jobs/{id}` (state machine: `queued → running → completed | failed | cancelled`), `/health` gains `jobQueueEnabled`/`jobWorkers`.
- **Cancel/remove**: `DELETE /jobs/{id}` cancels a queued job immediately, requests cancellation of a running one (it lands between chunks), and removes a finished record.

### Security posture

The endpoint takes file paths, which is a door that must not open onto the filesystem: paths must be relative, and the canonicalised result must stay inside the audio root, or the submission is rejected. The smoke proves the rejection with a `..\..` attempt. This is the item to revisit first in the security review.

### Replay determinism

The WP3 acceptance line "replay produces identical PCM chunks for deterministic analysis" holds by construction here — the chunker is deterministic — and the smoke proves the consequence: submitting the same file with the same config twice produces identical chunk and click counts.

## Validation

`service_job_smoke` (new CTest, 75/75 total): writes a 3-second WAV with transients every 4800 samples, submits a job, polls to completion, and asserts:

- the whole file was processed and exactly **30 clicks** were detected (10 transients/second × 3 s — the detector genuinely fired, not just ran);
- the archive holds one record per chunk plus the flush, and `archive/detections?type=click` returns the clicks — through the ordinary session-archive endpoints;
- a second identical job is **deterministic** (equal click and chunk counts);
- path traversal is rejected at submission;
- listing and completed-job deletion behave.

## Claim boundary

Jobs read whole WAVs into memory before chunking; a multi-gigabyte file wants a streaming reader, which `WavReader` does not yet have. One file per job — a directory/batch submission would be a thin layer over this but does not exist. Job records live in memory: a service restart forgets the queue and history (the *archived results* survive, since they are ordinary archive files). No priorities, no scheduling beyond FIFO, no per-tenant quotas.

Job sessions are isolated from the live-session map by design: they do not count against `PAMGUARD_MAX_SESSIONS`, do not appear in `/sessions`, and cannot receive live PCM.
