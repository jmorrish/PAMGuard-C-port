# Phase 7 PAMGuard operator acceptance record

Status: **awaiting independent operator execution and sign-off**

Plan: `docs/247-pamguard-authoritative-web-workflow-plan.md`

Java authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

## Purpose

This is the human acceptance gate that automation cannot self-certify. It is
for an experienced PAMGuard operator starting from a blank project without
implementation guidance. It tests whether the Data Model workflow is
understandable and operational, and whether selected physical Sound Output is
actually audible.

Automated browser tests remain responsible for exact project mutations,
settings round-trip, typed bindings, continuous rendering, non-zero
AudioWorklet delivery, lifecycle teardown, save/restart identities, and
conflict handling. A person must not be asked to substitute for those tests.

## Build and operator record

| Field | Record |
|---|---|
| Date/time | |
| Operator | |
| PAMGuard operating experience | |
| Port commit/build identifier | |
| Browser and version | |
| Operating system | |
| Acquisition device/source | |
| Physical output device | |
| Test audio/source description | |

## Unscripted tasks

Give the operator only the desired outcomes below. Do not tell them which
buttons, menus, node types, or ordering to use.

1. Starting from a blank project, configure live or file-backed sound input.
2. Create one full-band spectrogram.
3. Create a second spectrogram from a decimated processing branch with a
   different useful frequency range.
4. Add click detection and find the continuous click-history display.
5. Select which audio stream to monitor and listen through the stated physical
   output device.
6. Start processing, identify whether input, both spectrograms, clicks, and
   monitored audio are operating, then stop cleanly.
7. Save the project, close/restart the service and browser, and recover the
   same processing units, sources, display arrangement, and selected audio
   source.
8. Remove a source used by one display, recover from the visible disconnected
   state, then remove and re-add a processing/display branch.

## Outcome record

| Task | Pass / fail | Time or observation | Confusion, defect, or workaround |
|---|---|---|---|
| Blank-project orientation | | | |
| Acquisition configuration | | | |
| Full-band spectrogram | | | |
| Decimated spectrogram branch | | | |
| Click Detector and continuous Click display | | | |
| Source-selected physical audio is audible | | | |
| Start/Stop and clean finalisation | | | |
| Save/service restart/restore | | | |
| Source loss and recovery | | | |
| Remove/re-add workflow | | | |

## Acceptance decision

Phase 7 operator acceptance passes only when:

- every task is completed without implementation guidance;
- the selected physical output is confirmed audible;
- no severe data-loss, stale-source, orphan-display, lifecycle, or misleading
  readiness defect occurs; and
- usability problems are either fixed and retested or explicitly accepted by
  the project owner with a recorded reason.

Decision: **pending**

Operator sign-off:

Project-owner sign-off:
