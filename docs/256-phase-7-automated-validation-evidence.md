# Phase 7 automated validation evidence

Status: **automated exit gates passed; independent operator acceptance pending**

Date: 2026-07-25

Plan: `docs/247-pamguard-authoritative-web-workflow-plan.md`

Human acceptance record: `docs/257-phase-7-operator-acceptance.md`

Java authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

## Integrated build and test result

The authoritative Windows build completed with
`cpp-engine/scripts/build-msvc.ps1`. The reconfigured Release corpus then
passed:

```text
163/163 CTest tests passed
0 failed
Total real test time: 96.77 seconds
```

This was a complete run, not a selected test subset. It included the C++
scientific fixtures, controlled-unit/project authority, lifecycle, stable
project HTTP operations, compatibility isolation, service tests, browser
contracts, real-Chromium workflows, recorder/clip runtimes, tracked-click
target motion, and concurrency soaks.

## Pinned-Java regeneration

The complete Java oracle regeneration ran from a clean detached
`PAMGuard_Java` checkout at the pinned commit:

- 61/61 generator families passed in 911.329 seconds;
- the fixture corpus contained 85 files before and after;
- the corpus had zero content deltas;
- all 17 pre-existing modified/untracked fixture files retained the same Git
  status and SHA-256;
- the deterministic project fixture reproduced SHA-256
  `982BF6268F1B9D74EBBA11D1A551B073E9C7DD7667F0F527B6DA047DCD8032E7`;
  and
- the only non-empty stderr logs were expected JDK warnings about internal
  `Unsafe`/`ReflectionFactory` APIs.

The subsequent 163/163 CTest run therefore exercised the C++ comparisons
against the freshly regenerated, unchanged pinned-Java corpus.

## Operator workflow coverage

`project_shell_browser_smoke` passed against the real service and Chromium. It
starts blank and proves:

- independent raw-Acquisition and true Decimator-to-FFT Spectrogram branches;
- multiple Spectrograms with distinct sources, channels, and useful frequency
  ranges;
- Click Detector plus continuously retained Click display;
- source-selected Sound Output with non-zero AudioWorklet PCM delivery;
- Sound Recorder Continuous/Off transport and completed WAV finalisation;
- source loss with an explicitly unbound surviving display;
- remove/re-add with fresh stable controlled-unit/display identities;
- Save As, service restart, restored settings/bindings/layout/audio source;
- stale-ETag conflict visibility and owner-cascade cleanup; and
- absence of normal-browser Session, Workspace, or low-level graph authority.

`project_shell_dialog_browser_contract` additionally forces a native dialog
open failure, proves listener/action cleanup, and proves that the next dialog
can open and accept normally. The real-Chromium workflow exercises the rapid
Recorder close/reopen path that originally exposed the stale-close race.

## Scientific isolation and resilience

The project-authoritative concurrency soak compares an identical eight-chunk
PCM prefix first without presentation pressure and then with six simultaneous
fast/throttled FFT, Click, and audio subscribers. FFT and retained-Click
scientific counts and normalized payload SHA-256 signatures must be exact
between the two runs.

The 60-second candidate passed with:

- 1,255 chunks / 5,140,480 input frames;
- 20,204 FFT units and 2,526 retained clicks;
- exact repeated-prefix parity: 126 FFT units and 16 clicks;
- FFT hash prefix `706b1782d6a467e1` and Click hash prefix
  `e86b2a90a4647abb`;
- six concurrent streams, maximum observed queue depth four;
- 22.7 ms maximum ingest time and 48.5 MiB working-set growth; and
- clean subscriber drain, detector flush/finalisation, and runtime stop.

The integrated CTest form of that soak also passed. Presentation clients use
bounded drop-oldest queues, so slow displays/audio consumers cannot feed
pressure back into the scientific processing sequence.

## Localisation and support-unit gates

The stable project tracked-click HTTP smoke passed with five posed,
two-channel PCM observations. It retains trigger-onset navigation/orientation
and ordered earth-bearing ambiguities, assigns the clicks to one event, and
recovers the known target through the Least Squares run with beam, range, and
height-filter evidence.

Sound Recorder and Clip Generator settings/runtime checks passed, including
portable host-path separation, collision-safe relative WAV events,
complete-only clip publication, multi-source trigger policy, and Java-style
budget/truncation semantics. Alarm, Scroll Effort, Aural Listening, and User
Input remain explicitly unavailable/experimental and are hidden from the
normal palette rather than being presented as PAMGuard-equivalent.

## Compatibility and packaging boundary

Normal project mode rejects fixed sessions/jobs, Workspace reads, legacy
capture identities, generated-ID Acquisition ingress, and generated-ID
operator-event ingress. Compatibility tests still pass in the explicit oracle
mode, and every Compatibility-tagged OpenAPI operation is deprecated.

The production-isolation contract verifies the project-shell asset allowlist
and every local Dockerfile `COPY` source against existence and
`.dockerignore`. Docker CLI is not installed on this validation host, so an
actual Linux image build remains unexecuted; the packaging boundary is
statically validated and this limitation is not hidden.

## Remaining human gate

Automation proves selected-source PCM delivery to the browser audio worklet,
but it cannot prove that a person heard the selected physical output device or
that the workflow is understandable to an experienced PAMGuard operator.

Phase 7 therefore remains open until an experienced operator completes and
signs `docs/257-phase-7-operator-acceptance.md` from a blank project without
implementation guidance. That single task sheet also supplies the remaining
physical Sound Output confirmation required by the Phase 3 exit gate.
