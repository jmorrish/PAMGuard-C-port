# Click Pre-Filter and Trigger Filter (IIR Filters Port)

Date: 2026-07-22

## Purpose

Closes a **parity gap in a module previously called ported**. PAMGuard's click detector runs the audio through two Butterworth IIR filters before anything the engine had ported ever sees it — `ClickParameters`' own defaults are a 4th-order 500 Hz highpass prefilter and a 2nd-order 2 kHz highpass trigger filter. Our trigger fixtures passed because the Java exporters fed the trigger directly; on real broadband data, a default-configured PAMGuard rejects low-frequency energy (flow noise, vessel tonals) before triggering, and until now this engine did not.

The port brings PAMGuard's `Filters` package with it: Butterworth and Chebyshev design (Lynn & Fuerst bilinear method) across all four bands, and the runtime the click detector actually uses.

## Reference semantics ported

**The runtime is `FastIIRFilter`, not `IirfFilter`** — `IIRFilterMethod.createFilter` returns the fast implementation, whose coefficients come from `getFastFilterCoefficients` over the *sorted* pole/zero pairs and whose per-sample state recursion this port reproduces exactly. Two consequences worth naming:

- The **sort order matters**: `PoleZeroPair.compareTo` puts the odd (self-conjugate) stage last and orders the rest by descending pole imaginary part. A different order changes intermediate state and therefore rounding; the port uses a stable sort with the same comparator.
- `FastIIRFilter.getFilterDelay()` returns **0** (the older `IirfFilter` returned `nFilterUnits/2`), so the click start-sample corrections in `ClickDetector` are no-ops in the live path — the engine applies none, matching what actually runs.

Design details preserved: the `norm() ≤ 1.0000000001` padded-unit-circle pole keep (norm is magnitude *squared*), the highpass real-part sign flip, the bandpass/bandstop transforms with their principal square root, Chebyshev's historical `c` formula (the source carries a 2005 comment questioning it; what ships is what is ported), the gain constant evaluated at band-dependent frequencies with Chebyshev ripple bumps, and the bandpass `a2` sign flip for zeros on opposite ends. Frequencies stay `float` because `FilterParams` stores floats and the promotion is part of the arithmetic.

## Signal flow in the click detector

Exactly `ClickDetector.newData`'s live path: raw → **preFilter** → `waveformData` (what click waveforms are captured from) → **triggerFilter** → `triggerData` (what the short/long trigger and its whole-block initialisation consume). Filter state runs continuously across chunks. The engine's waveform history now stores the *prefiltered* stream, and the trigger loop and its initialisation read the *trigger-filtered* one.

**Engine defaults are `None` for both filters** — existing sessions keep their behaviour bit-for-bit — while the project importer carries `ClickParameters.preFilter`/`triggerFilter` across, so an imported PAMGuard project gets PAMGuard's filtering. The docs state PAMGuard's own defaults so anyone configuring by hand can match them.

## Validation

`iir_filter_parity` (new, suite `76/76`): ten cases driven end to end by the **real** `ButterworthMethod`/`ChebyshevMethod`/`FastIIRFilter` — zero transcription — over a 512-sample signal of impulse + two tones + xorshift pseudo-noise, including the exact click defaults, even and odd orders (the single-stage path), all four bands, and Chebyshev variants. Max error **2.4e-15** across 5 120 compared samples.

The same check then demonstrates the closed gap end to end: an energetic 100 Hz rumble burst **triggers** an unfiltered engine session, is **rejected** by a session with PAMGuard's default filters, and a broadband transient in the same recording still gets through.

The importer's sample `.psfx` round-trips both filters (`click.preFilter`/`click.triggerFilter` in the emitted session JSON, PAMGuard field names kept — including the convention that a band filter's `highPassFreq` is its *lower* edge).

## Claim boundary

`FIRFilterMethod` (windowed FIR), `FFTFilterMethod`, and `NullMethod` are not ported — nothing in the click path uses them, and the noise band monitor (`docs` to follow) will take FIR if it needs it. The older `IirfFilter` runtime is not ported: it is not what `createFilter` returns.

Filter state is not reset on sample discontinuities, matching the reference, which also never resets on gaps; a dropout therefore rings briefly through the IIR history in both implementations.

The engine-default-`None` decision means an engine session configured by hand *without* filters still differs from a default PAMGuard — by explicit configuration now, not by a missing port. The importer closes it for imported projects; the parity claim for hand-written configs rests on the user setting the filters this document names.
