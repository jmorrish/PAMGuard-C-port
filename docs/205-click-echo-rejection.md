# Click Echo Rejection

Date: 2026-07-22

## Purpose

Closes a named deliverable from the original click detector work package (`docs/05`, WP4: "Echo rejection and classifier extension points") that had never been ported. PAMGuard's online echo gate sits in `ClickDetector.processData` **before** amplitude, classification, and the click being added at all, so with `discardEchoes` set an echo never reaches any downstream consumer.

## Reference semantics ported

`SimpleEchoDetector` is the default and only headlessly-reachable echo system (`ClickControl` constructs `SimpleEchoDetectionSystem` unconditionally; `JamieEchoDetector` exists but nothing selects it). The algorithm is small and precise:

- A click is an echo when its delay from the last **non-echo** click is within `SimpleEchoParams.maxIntervalSeconds` (default 0.1 s).
- The anchor only advances on a non-echo, so a reverberation burst is measured entirely against the original click — not chained, where every second click would escape.
- The first click is never an echo and becomes the anchor.
- The delay must be non-negative (an out-of-order click re-anchors), and the boundary is **inclusive**.

The gate itself (`ClickParameters.runEchoOnline` / `discardEchoes`): discard drops the click with an early return; otherwise the click is flagged (`ClickDetection.setEcho`).

## A real-class fixture despite the PamController wall

The detector's constructor wants `ClickControl` and `ChannelGroupDetector`, but `isEcho` reads only three things: its private `sampleRate`, the system's `SimpleEchoParams`, and the click's start sample. So the exporter allocates the detector, its system, and each `ClickDetection` the way deserialisation allocates (`ReflectionFactory.newConstructorForSerialization`, the docs/203 trick), sets exactly those fields reflectively, and lets **PAMGuard's own unmodified bytecode** make every decision. Pre-setting a non-zero sample rate keeps `isEcho` off its `initialise()` path — the one line that would touch `ClickControl`.

Six scenarios, 23 decisions, all matching: first click, the inclusive boundary (a delay of exactly `maxInterval` is an echo; one sample later is not), burst anchoring, out-of-order re-anchoring, a slow-rate/long-window variant, and a zero interval (only an identical start sample echoes).

## Engine wiring

`click.echo` in session config: `runOnline`, `discardEchoes`, `maxIntervalSeconds` (default 0.1, finite-checked). The session filters clicks **immediately after detection**, preserving the reference's placement: a discarded echo never reaches features, the classifier, train formers, or localisation. Echo state (the anchor) lives on the session and survives chunk boundaries, matching PAMGuard's per-channel-group detector — the engine's click detector is one group per session, so one state per session is the faithful shape.

Schema v22 adds an `echo` boolean to click results, present only when the gate is running — absent otherwise, so a consumer cannot mistake "not checked" for "not an echo".

The project importer maps `runEchoOnline`/`discardEchoes` from `ClickParameters` and `maxIntervalSeconds` from the `SimpleEchoParams` settings unit (its own unit — `SimpleEchoDetectionSystem` implements `PamSettings`), and the sample `.psfx` now carries both.

## Validation

`simple_echo_parity` (new) replays the fixture and additionally runs three session-level configurations over a two-transient chunk crafted inside the echo window: gate off (two clicks, no flags), flag mode (two clicks, second flagged), discard mode (**one** click — the echo removed before any consumer saw it). Full CTest suite passes `73/73`.

## Claim boundary

`JamieEchoDetector` is not ported: nothing in PAMGuard selects it (its system is never constructed outside its own package), putting it in the same evidence class as the docs/200 non-ports.

PAMGuard runs one echo detector per channel *group*; a PAMGuard configuration with several click channel groups would keep several anchors. The engine has one group per session, and the port matches that structure — multi-group sessions would need the gate replicated per group along with everything else group-scoped.

Offline echo re-classification (`offlineFuncs.EchoDetectionTask`) is out of scope; the engine has no offline click re-processing pipeline.
