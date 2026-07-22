# Security Review

Date: 2026-07-22

## Scope and method

WP9's "security review" line (`docs/05`). Scope: the engine service's HTTP surface, its filesystem interactions, and the ingest tooling — reviewed by walking every registered route, every path construction, every body parse, and the auth helper. This is a structured self-review by the implementer, not an independent penetration test; that distinction matters and is restated in the claim boundary.

## Findings and their state

### Fixed during this review

1. **Unbounded request bodies.** JSON endpoints parsed `req.body` with no size ceiling; only the PCM endpoint had a cap, and only when configured. A global `set_payload_max_length` now applies: `PAMGUARD_MAX_PCM_BODY_BYTES + 1 MB` when the PCM cap is configured, 256 MB otherwise. The PCM endpoint keeps its tighter, clearer 413.
2. **Timing side-channel in API-key comparison.** `==` on `std::string` short-circuits at the first mismatching byte; the comparison is now constant-time over equal-length inputs (length itself still short-circuits — leaking the key *length* is accepted, leaking a matched *prefix* is not).

### Verified sound

3. **Auth coverage** — every data endpoint (23 routes) requires the API key when one is configured, verified route-by-route: sessions, PCM, flush, archive queries/CSV, results feed, audio index, jobs, ingest status, metrics, and the OpenAPI document. Open by design: `/health` and `/ready` (orchestrator probes) and the web-UI shell (`/`, `/index.html`) — the UI itself holds no data; every API call it makes is authenticated.
4. **Path traversal** — session ids are sanitised to `[A-Za-z0-9._-]` before becoming filenames, with separators replaced (so `../` cannot survive), and every archive/persistence path is built from the sanitised form. Job WAV paths must be relative and their canonical form must stay inside `PAMGUARD_JOB_AUDIO_DIR`; the job smoke proves the `..\..` rejection. The audio archive reuses the sanitised-session scheme.
5. **PCM parsing** — body size is validated against frame arithmetic before decoding; float decoding is bounds-driven by the validated size; `startSample + frames` is checked against `uint64` overflow.
6. **Job inputs** — submission validates path residency, file existence, and config parseability before queueing (400, not a delayed failure), and jobs are isolated from the live-session map.

### Accepted risks, stated

7. **CORS defaults to `*`.** Deliberate for the single-file UI's development ergonomics; a deployment that fronts the service publicly should set `PAMGUARD_CORS_ORIGIN`. With key auth in headers (never cookies), the classic CSRF amplification does not apply, but data endpoints being auth-gated is what carries that weight.
8. **`/health` discloses configuration shape** (which features are enabled, limits) without auth. Useful to operators and probes; discloses no data or paths. Accepted.
9. **Error messages echo validation text** (`error.what()`) to authenticated callers, including job failure reasons that may contain a resolved path *inside* the job audio root. Accepted for operability; nothing outside configured roots can appear because nothing outside them is ever opened.
10. **No TLS in the service itself.** Terminate TLS at an ingress/reverse proxy, as the Kubernetes starters assume. The API key travels in a header and must not cross untrusted networks unencrypted; the deployment docs say so.
11. **No rate limiting or per-tenant quotas.** `PAMGUARD_MAX_SESSIONS`, the payload ceiling, and the archive query caps bound resource use per request class, but a hostile authenticated client can still saturate CPU. Multi-tenant deployments need an upstream limiter.
12. **Audit log** records session lifecycle events, not every read. Query-level audit would be a policy decision with real volume costs.

## Claim boundary

One reviewer, who is also the author — the classic blind spot. The review covered the C++ service surface and job/audio path handling; it did **not** cover the Python ops tooling's own file handling in depth (it runs operator-side, not internet-facing), the web UI's DOM handling of archived strings (the UI renders detector output; archived `sourceId`/session ids are attacker-influenced only by authenticated callers), or third-party library CVEs (`httplib`, `nlohmann/json` vendored copies should be refreshed on a cadence). An independent review before any internet-facing deployment remains the right call, and this document is its starting inventory.
