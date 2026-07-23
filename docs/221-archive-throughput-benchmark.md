# Archive-Enabled Throughput Benchmark

Date: 2026-07-23

## Purpose

Closes the "archive-enabled throughput remains unmeasured" gap recorded in `docs/207` and the README: the original benchmark deliberately cleared the archive environment, so the cost of writing every result and every PCM byte to disk was unknown. Also measures the cost of the monitoring modules added since (`docs/213`–`docs/218`).

## Method

`service-throughput-bench.ps1` gains two switches:

- `-Archive`: sets `PAMGUARD_RESULT_ARCHIVE_DIR` and `PAMGUARD_AUDIO_ARCHIVE_DIR` to a temp root for the run, reports the bytes written, and cleans up.
- `-Monitors`: adds `noiseBand` (third-octave), `ltsa`, `ishmael`, and `sgramCorr` to every session's config on top of `-Detectors`.

Same protocol as `docs/207`: 50 concurrent sessions × 30 s of synthetic 48 kHz 2-channel audio (noise + 6 kHz tone + periodic transients) in 1 s chunks, click detection + localisation + trains and the whistle chain enabled, single client posting as fast as the service accepts.

## Results (same workstation as docs/207)

| Configuration | Realtime factor | p50 / p95 / p99 latency (ms) | Archive written |
| --- | --- | --- | --- |
| Detectors (baseline re-run) | **23.4×** | 41.0 / 54.8 / 63.3 | — |
| Detectors + archive | **12.0×** | 82.2 / 103.0 / 111.2 | 1 053.6 MB results + 576.2 MB audio |
| Detectors + archive + monitors | **11.6×** | 84.4 / 104.5 / 114.2 | 1 055.2 MB results + 576.2 MB audio |

- The baseline re-run (23.4×) is in family with the 26.7× recorded in `docs/207`; the spread is run-to-run machine state, and both say the same thing — 50 unarchived sessions sustain with a wide margin.
- **Full archiving costs roughly half the throughput** (23.4× → 12.0×): the run writes ~1.6 GB in ~125 s (~13 MB/s sustained), and per-chunk latency doubles but stays regular (p99 within 1.4× of p50). 50 archived detector-loaded sessions still sustain 12× realtime.
- **The four monitoring modules are nearly free** on top (12.0× → 11.6×, ~4%): band filters, LTSA accumulation, and the two Ishmael FFT-stream detectors are cheap next to the click/whistle chains and the archive I/O.

## Claim boundary

One workstation, one client, synthetic audio, 30 s per session — this is the same *can-it-sustain-N-sessions* question as `docs/207` with archiving added, not a soak. Result NDJSON dominates the archive volume (~1.8× the raw audio) because the detector config is chatty on this signal; quieter audio archives less. Multi-hour soak and concurrent-client (many-writer) runs remain unmeasured, as before. The numbers are a dated measurement, not a CI assertion.
