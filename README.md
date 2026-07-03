# PAMGuard Enterprise Port

This workspace is the enterprise-scoped plan and implementation track for a scalable web platform that reproduces selected PAMGuard module maths in C++.

The Java PAMGuard source in the parent repository is treated as the reference implementation. The C++ engine in this workspace is not intended to be a loose rewrite; it is intended to become a parity-tested implementation of the supported maths and detector behaviour.

## Programme Goal

Build a production web platform that can ingest file and live audio sources, run PAMGuard-compatible FFT/spectrogram, click detector, and whistle/moan detector processing, and present browser-based review, monitoring, configuration, and export workflows.

## Key Principle

Correctness comes before convenience.

Every C++ module should be validated against PAMGuard reference outputs using golden fixtures and explicit numeric tolerances.

## Workspace Layout

```text
docs/
  Enterprise work package, architecture, ingest, localisation, and parity strategy.

cpp-engine/
  C++ PAMGuard-math engine scaffold.

platform/
  Backend/web platform contracts and operational notes.
```

## First Vertical Slice

The first delivery slice is:

```text
WAV/PCM input
-> PAMGuard-compatible windowing
-> FFT/spectrogram frames
-> parity fixtures against PAMGuard
-> browser/API-ready spectrogram output
```

Click detection and whistle/moan contours build on this foundation.

## Current Validated State

Tooling has been installed and the initial C++ engine scaffold builds on Windows with MSVC.

Validated commands:

```powershell
.\pamguard-enterprise-port\reference-tools\scripts\mvn-local.ps1 -DskipTests compile
.\pamguard-enterprise-port\reference-tools\scripts\generate-all-window-fixtures.ps1
.\pamguard-enterprise-port\reference-tools\scripts\generate-all-fft-fixtures.ps1
cd .\pamguard-enterprise-port\cpp-engine
.\scripts\build-msvc.ps1
.\scripts\test-msvc.ps1
```

Current result:

- PAMGuard Java compile succeeds.
- C++ engine builds.
- 10/10 CTest checks pass.
- Window and FFT fixtures are generated from PAMGuard Java and checked by C++ parity tools.
- A WAV file can be read and processed into spectrogram frames via `spectrogram_file_cli`.

See [current implementation status](docs/08-current-implementation-status.md) for details.
