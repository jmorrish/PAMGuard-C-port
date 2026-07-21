# Click Train Spectrum Templates

Date: 2026-07-10

## Purpose

`docs/185` ported the template classifier but left the templates themselves caller-supplied. This slice ports PAMGuard's five default spectrum templates so the classifier is usable out of the box, and — unlike the classifier logic — these get genuine **fixture parity**.

## What was ported

`DefualtSpectrumTemplates` provides five templates, each a spectrum shape plus the sample rate its bins span:

| Template | Bins | Sample rate |
| --- | --- | --- |
| Sperm Whale | 64 | 96 kHz |
| Broadband Dolphin | 20 | 500 kHz |
| Beaked Whale | 30 | 192 kHz |
| NBHF (porpoise) | 42 | 500 kHz |
| Boat | 20 | 96 kHz |

`ct_default_spectrum_templates()` returns them in PAMGuard's enum order, matching `getDefaultTemplates()`.

## Fixture parity, not transcription by hand

The constants were **generated from a fixture**, not typed out: `SpectrumTemplateFixtureExporter` drives the real `DefualtSpectrumTemplates` class and writes every value at full precision, and the C++ header was produced from that file. `ct_spectrum_template_parity` then compares the compiled-in constants against the fixture with **exact equality** (not a tolerance), so any future drift in either direction fails the build. This removes the transcription risk that hand-copying 176 floating-point values would carry.

## Caveat from the reference

The PAMGuard source carries a `TODO-> need real values for these` comment above these arrays, so the values are provisional **in the reference itself**. The port reproduces them faithfully and the header says so; anyone relying on template classification for science should supply their own measured templates rather than trusting the defaults.

## Validation

Full CTest suite passes `65/65`.

## Claim boundary

Only the default template constants and their ordering are ported. Template *editing* (PAMGuard's template GUI and the ability to build templates from measured clicks) is not, nor is train average-spectrum construction, which is still what blocks wiring the template classifier into the served path.
