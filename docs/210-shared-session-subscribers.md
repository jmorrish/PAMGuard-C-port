# Shared-Session Subscribers

Date: 2026-07-22

## Purpose

Delivers WP7's "multiple subscribers can watch one shared session" and the original brief's "multi-user viewing of the same source/config should share one engine session where possible" (`docs/01`). Until now, live results went only to whoever posted the PCM; other viewers had the archive, which lags and requires archiving to be on.

## Design

A per-session ring of the most recent result bodies, each stamped with a monotonically increasing `seq`:

- The PCM handler publishes every result into the ring (depth `PAMGUARD_RESULT_FEED_DEPTH`, default 16; `0` disables) and stamps the poster's own response with the same `seq`.
- **`GET /sessions/{id}/results?sinceSeq=K`** returns everything newer than `K` plus `latestSeq`, so any number of viewers poll incrementally: first call with `sinceSeq=0` (or omitted) for the backlog, then with the last seen `latestSeq`. A caught-up subscriber gets an empty list, cheaply.
- The **engine session stays shared** — one detector state per source, exactly as the brief asked. A subscriber costs a ring lookup under a mutex, not a session, which is why the throughput numbers in `docs/207` are unaffected by viewer count in any way that matters.
- The feed is deleted with its session.

Polling over Server-Sent Events or WebSockets was a deliberate choice: the sequence-number contract gives loss-free incremental reads with plain HTTP semantics that every client (including the single-file web UI) already speaks, and the ring bounds memory regardless of subscriber behaviour. A push transport can sit on top later without changing the contract.

## Validation

The HTTP smoke (all three auth variants) asserts: a viewer that never posted PCM reads the four posted chunks' results through the feed with intact click payloads; an incremental read from `latestSeq - 1` returns exactly the newest result with the right `seq`; a caught-up read returns nothing. Suite `75/75`.

## Claim boundary

A subscriber slower than `depth` chunks behind loses the gap — the ring is bounded by design, and the sequence numbers make the loss *detectable* (a jump between `seq` values) but not recoverable except through the archive. The default of 16 one-second chunks tolerates ~16 s of viewer stall.

Results in the feed are the poster's full result bodies; there is no per-viewer filtering or field selection. The feed is in-memory and does not survive a service restart (the archive does).
