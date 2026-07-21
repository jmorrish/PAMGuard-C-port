# PAMGuard Project Import Feasibility

Date: 2026-07-10

## Purpose

PAMGuard project/config import has been the largest open Gap in the ledger since the port began. This slice does not implement it; it establishes **how it must be built**, with evidence, because the obvious plan — "port the reader to C++" — is not viable.

## Finding 1: the settings payloads are Java-serialised object graphs

Both project formats store each module's configuration as a Java-serialised object:

- `.psf` is a single `ObjectInputStream` stream of `PamControlledUnitSettings`.
- `.psfx` wraps the **same serialised byte arrays** in PAMGuard's binary-store framing (`BinaryHeader`/`BinaryFooter`/`ModuleNameObject` records, read in `PSFXReadWriter.loadFileSettings`); `PamControlledUnitSettings.getSettings()` then deserialises the payload.

Java object serialisation encodes class graphs, `serialVersionUID`s, and private field layouts. Reproducing it in C++ would mean reimplementing the JVM serialisation format *and* mirroring the private field layout of every PAMGuard settings class. That is not a defensible engineering path for a parity-critical feature.

**Conclusion: project import must run on the JVM**, using PAMGuard's own classes, and emit the engine's JSON session config. This is the same reference-tool pattern every fixture exporter in this port already uses, so it fits the existing architecture rather than fighting it.

## Finding 2: the format is version-brittle even within Java

`PamguardSettingsInspector` (new, in `reference-tools`) reads a settings file and reports each module's type, name, version, and settings class. Run against the sample `PamguardSettings.psf` in the PAMGuard repository root, it fails — **in Java, against PAMGuard's own current build**:

```text
Settings file is not loadable by this PAMGuard build.
Serialisation incompatibility: PamView.GuiParameters; local class incompatible:
stream classdesc serialVersionUID = 1, local class serialVersionUID = 2
```

A single settings class bumping its `serialVersionUID` makes an older file unreadable. PAMGuard acknowledges this itself: `SettingsImportDialog` warns that "the format of older psf files is not supported by the new import functions".

This matters for scoping: import can only be promised against files written by a **declared, matching PAMGuard version**, and that version must be pinned alongside the engine — consistent with the programme's existing "frozen target PAMGuard version" acceptance criterion (`docs/01`).

## What the tool does today

`PamguardSettingsInspector` handles both formats, degrades gracefully on serialisation incompatibility (exit code 3 with a clear diagnostic rather than a stack trace), and reports unreadable individual settings classes without aborting the whole file. It enumerates exactly what a converter would need to map.

## Recommended shape for the import work

1. Pin a target PAMGuard version and obtain a `.psfx` written by it.
2. Extend the inspector into a converter: for each module whose settings map onto a ported detector, read the relevant fields (`ClickParameters`, `WhistleToneParameters`, FFT/array settings) and emit engine session JSON.
3. Cover it with fixtures the same way as the rest of the port: a checked-in `.psfx` plus its expected JSON, so conversion is regression-tested.
4. Report unmapped modules explicitly rather than silently dropping them — an imported config that quietly ignores a module is worse than one that refuses.

## Claim boundary

Nothing is imported yet. The ledger row stays a Gap; what changes is that it now has a viable, evidenced plan and a working inspection tool, rather than an assumption that it could be ported to C++.
