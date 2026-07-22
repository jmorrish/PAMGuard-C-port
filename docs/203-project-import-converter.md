# Project Import Converter

Date: 2026-07-22

## Purpose

Closes the ledger's largest Gap to the extent it can be closed without external input. `docs/182` established that PAMGuard project import must run on the JVM (the payload is Java-serialised object graphs) and was recorded as blocked on "a pinned PAMGuard version and a `.psfx` written by it". Re-examining that: **the pinned version is already here.** The local source tree at the repo root — version `2.02.18b` — is the tree every parity fixture treats as the oracle, and its `PSFXReadWriter.writePSFX(String, PamSettingsGroup)` takes an explicit settings group, so a genuine `.psfx` can be *written* headlessly by the pinned version itself.

That turns "blocked on a file" into "blocked on nothing" for the version in hand.

## The converter

`PamguardProjectConverter` has two modes:

- **`convert <settings.psfx> <session.json>`** — loads the file with the real `PSFXReadWriter.loadFileSettings`, matches settings objects **by their real class** (not unit-name strings, so renamed modules still convert), and emits an engine session-create JSON document. Serialisation incompatibility with a foreign PAMGuard build exits 3 with the same diagnostic as `PamguardSettingsInspector`.
- **`write-sample <sample.psfx>`** — constructs real `AcquisitionParameters`, `PamArray`/`Hydrophone`/`Streamer`, `FFTParameters`, `ClickParameters`, and `WhistleToneParameters` objects and writes them through the real `writePSFX`, producing the validation fixture from the pinned version itself.

### What maps

| PAMGuard settings class | Engine JSON |
| --- | --- |
| `AcquisitionParameters` | `sampleRateHz`, `channelCount` — required; conversion fails without it |
| `PamArray` + `Hydrophone` + `Streamer` | `array`: speed of sound ± error, hydrophones (coordinates, per-axis errors, sensitivity, streamer id), streamers (position, heading/pitch/roll) |
| `FFTParameters` | `fft`: length, hop, window (PAMGuard's `WindowFunction` integer passes straight through — the engine accepts 0..5 natively), channels from the channel map |
| `ClickParameters` | `click`: threshold, both filters, pre/post samples, min separation, max length, trigger bitmap, min trigger channels, channel bitmap |
| `WhistleToneParameters` | `whistle`: min pixels/length, max cross length, connect type, fragmentation method, shape stubs, and search bins derived from the frequency limits via the FFT length and sample rate |

Whistle settings without FFT settings are reported as unmappable rather than guessed: the frequency-to-bin conversion needs the FFT length.

**Nothing is silently dropped.** Every unit that did not contribute to the JSON is listed on stdout with its class, and the sample file deliberately contains an unmappable unit so the skip path is always exercised.

### The PamController wall, on the writing side only

Reading real files was never blocked: Java deserialisation does not run constructors. Sample *writing* hit the wall twice — `AcquisitionParameters`' constructor reaches `PamController.getInstance()`, and `PamArray`'s reaches it through `HydrophoneLocators`. The fix is the same trick serialisation itself uses: `sun.reflect.ReflectionFactory.newConstructorForSerialization` allocates without running the constructor, and `PamArray`'s two list fields (initialisers the allocation skips) are seeded reflectively. This is confined to sample generation; `convert` touches none of it.

## Validation

`generate-project-import-fixture.ps1` writes `sample-project.psfx` and converts it to `sample-project-session.json`, both checked in under `tests/fixtures/project-import/`.

The HTTP smoke then closes the loop end to end: it loads the converted JSON, adds the owner/tenant metadata the smoke's service enforces (a PAMGuard settings file has no notion of tenancy), POSTs it to the live engine, asserts creation succeeds and the acquisition and FFT settings **round-trip** through the session status endpoint, and deletes the session. A `.psfx` written by PAMGuard's real writer therefore drives a real engine session with no hand-editing beyond deployment metadata.

Full CTest suite passes `72/72`.

## Claim boundary

**Pinned to `2.02.18b`** — the local tree. A `.psfx` from a different PAMGuard build may fail to deserialise (exit 3), and that brittleness is a property of Java serialisation PAMGuard itself lives with; `docs/182`'s demonstration that the repo's old sample `.psf` fails against this very build still stands. Extending the support matrix to another version needs a file from that version — that much genuinely does require external input, but it is now a *compatibility test* away, not a *feature* away.

The mapping covers the modules the engine implements. PAMGuard settings the engine has no equivalent for — displays, binary storage, database, GPS — are reported and skipped by design. Click classifier and click-train settings are not yet mapped: their engine config exists (`docs/155`, `docs/173`), and extending the converter to them is mechanical, but each mapping deserves its own verification rather than a bulk pass.

The sample `.psfx` is representative, not exhaustive: one streamer, identity channel-to-hydrophone mapping. PAMGuard's acquisition-side channel remapping is not modelled — the converter assumes the identity mapping the engine uses, and says so in a comment where it assigns channels.
