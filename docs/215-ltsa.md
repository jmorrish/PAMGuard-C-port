# Long-Term Spectral Average (LTSA)

Date: 2026-07-23

## Purpose

Ports PAMGuard's `ltsa` module — the long-term spectral average that turns the FFT stream into one RMS spectrum per averaging period per channel, the standard way to view hours of recording on one screen. Item 4 of the low-hanging-fruit list. The maths is deliberately tiny; the value is the honest port of its boundary semantics.

## Reference semantics ported

`LtsaProcess.ChannelProcess`, verbatim:

- Accumulate `magsq(i)` per FFT bin across slices; on close, store `sqrt(mean)` per bin — an **uncalibrated RMS spectral magnitude**, exactly the value `LtsaDataUnit` carries (display gain is applied downstream in PAMGuard, not in the stored data).
- Periods align to **absolute wall-clock interval boundaries** (`timeMillis / interval * interval`), so the first period is normally partial. The engine's FFT frame timestamps derive deterministically from the chunk timestamps, so this is reproducible under replay — no sample-counting substitution was needed here, unlike the noise bands (`docs/214`).
- The slice whose timestamp reaches the period end closes the period **first** and is accumulated into the **next** one.
- `closePeriod` advances by exactly **one** interval however far past the boundary the closing slice is: after a time gap, slices land in stale windows until the window catches up. A quirk, preserved and pinned.
- A close with nothing accumulated emits nothing and does not advance the window.
- Each period reports `nFFT`, the covered sample span (first slice's start to last slice's start + fftLength — PAMGuard stamps FFT units with a duration of fftLength), and the window times.
- The LTSA sources the FFT data block directly — **before** the whistle path's spectrogram noise reduction. Bin 0 uses PAMGuard's packed convention (DC² + Nyquist²), as the engine's `pamguard_packed_magnitude_squared` already provides.

## Configuration and results

- `ltsa`: `{enabled, intervalSeconds}` (`LtsaParameters.intervalSeconds`, default 60). Channels follow the FFT config, as `LtsaProcess.setupProcess` follows its source block.
- Results: `ltsa` at schema v24 — per channel per completed period: `startTimeMs`/`endTimeMs`, `nFft`, `startSample`/`durationSamples`, `magnitude[]` ascending in frequency. The session flush closes the in-progress period (`flushDataBlockBuffers` equivalent).
- The importer maps `LtsaParameters` (skipping a non-positive interval with a printed reason) and the sample `.psfx` carries one.

## Validation

`ltsa_parity` (new, suite `78/78`): `LtsaFixtureExporter` drives the **real** `LtsaProcess.ChannelProcess` — control and process allocated the way deserialisation allocates, the output block replaced by a capture subclass so `closePeriod` runs PAMGuard's own bytecode end to end. Five cases, 77 slices, 16 periods, **maxError 0** (bit-identical): aligned and non-aligned starts, the 7-second gap driving the stale-window quirk, a flush-only period, and an odd 617 ms slice step against a 5 s interval.

End-to-end: a live service check (1 kHz tone at 8 kHz, fft 256) produced two 1-second periods, 63 slices each, spectral peak exactly at bin 32.

## Claim boundary

The `longerFactor` "longer LTSA" block is commented out in the reference (`LtsaProcess` lines 35–36) and is not ported. The datagram/binary-store/viewer machinery around the data unit is PAMGuard display infrastructure, not maths, and is out of scope. Mid-run FFT-length changes re-prepare in the reference without resetting `nFFT`; in the engine the FFT length is fixed per session, so that path cannot arise and is not claimed.

Alongside this slice, the `iir_filter_parity` gate scenario (`docs/213`) was corrected: its low-frequency rumble burst previously switched on as an instantaneous step, which is itself a broadband transient that PAMGuard's filters would legitimately click on — the burst is now raised-cosine ramped so "the filters reject the rumble" asserts what it claims. The earlier green run of that scenario is attributed to a stale test binary (the known narrow-error-filter build trap).
