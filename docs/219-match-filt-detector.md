# Ishmael Matched Filter Detector

Date: 2026-07-23

## Purpose

Ports PAMGuard's Ishmael matched filter — the third and last of the Ishmael detectors, completing the family. The port targets `MatchFiltProcess2`, the live implementation; `MatchFiltProcess` v1 is `@Deprecated` in the reference ("use MAtchFiltProc2") and is not ported.

## Reference semantics ported

**Kernel prep** (`prepareKernel`): the kernel waveform comes from a WAV file (channel 0, via `AudioSystem` + `FileInputSystem.bytesToSamples`); the file's own sample rate is **ignored** — no check, no resampling (quirk kept). The FFT length is `nextBinaryExp(max(round(0.1 s × sr), 2 × kernelLen))`; the kernel is zero-padded (at the START — the "pack at the end" comment's code is dead) and forward-transformed once; `usefulSamples = fftLength − kernelLen`; `normConst = Σ kernel²`.

**Per channel** (`ChannelDetector`): raw samples fill an `fftLength` buffer; each full buffer runs `conjTimes` — conj(data spectrum) × kernel spectrum over the **packed** pairs, the packed DC/Nyquist bin multiplied blindly as ever — then `FastFFT.realInverse` (the scaled packed real inverse, added to `dsp::JtFft` as `real_inverse`). Output is `usefulSamples` values of `xCorr[i]/√(normConst · norm2)` where `norm2` is the kernel-length window energy maintained as a sliding sum (subtract first, add next — verbatim, so a zero-energy window divides to NaN/Inf exactly as Java does). The buffer keeps a kernel's length of overlap (`shuffleBuffer`). Blocks report `startSample = totalSamples − fftLength`.

The values feed the **shared Ishmael peak picker** — this time at the **audio** rate (`getDetSampleRate` returns `getSampleRate`, so the picker's hop is 1) and in **multi-sample blocks**, pinning `IshPeakProcess`'s per-sample walk over `detData[0]` for the first time. The reported band is 0..sr/2 (`getLoFreq`/`getHiFreq`).

Channel selection: with no channel groups the reference runs channel 0 only; with groups, the first channel of each group. The engine's `channels` list mirrors this — empty means channel 0.

## Configuration and results

`matchFilt`: `{enabled, kernel: [samples], channels, thresh, minTimeSeconds, maxTimeSeconds, refractoryTimeSeconds}` — the kernel travels **inline**; the importer reads the `.psfx`-configured WAV through the same `AudioSystem` path the reference uses and embeds the samples (missing/unreadable files are skipped with the reason and path printed). The sample `.psfx` writes a real 16-bit kernel WAV next to itself and references it, so the mapping exercises the real read-back including its quantisation.

Results: `matchFiltDetections` at schema v28, same fields as the other Ishmael detection arrays.

## Validation

`match_filt_parity` (new, suite `82/82`): `MatchFiltFixtureExporter` drives the **real** `MatchFiltProcess2` — the kernel written to a real WAV and read back through the reference's own bytecode, `prepareKernel` invoked directly, the real inner `ChannelDetector` fed 1000-sample raw units — chained into the **real** `IshPeakProcess`. Two cases (300-sample kernel at 8 kHz → fft 1024/useful 724; 150-sample kernel at 2 kHz → fft 512/useful 362), with scaled kernel copies embedded in noise: **23,168 normalised correlation values across 48 blocks and 6 detections match to 7.5e-14**, plus exact `fftLength`/`usefulSamples`/`normConst`.

## Claim boundary

The deprecated `MatchFiltProcess` v1 (different buffering, `FFT.crossCorrelation`, unnormalised output) is not ported — the reference itself directs users to v2. The exporter drives the inner `ChannelDetector` directly, so `prepareMyParams`' group-to-first-channel selection is config-level mapping, not fixture-pinned (it is four lines). `WarnOnce` dialog behaviour on kernel errors is GUI, out of scope; the engine surfaces an invalid kernel as a session-creation error instead. Detection parity tolerance is 1e-9 relative (observed 7.5e-14) — JTransforms vs the engine's DFT rounding, as recorded for the other JtFft consumers.
