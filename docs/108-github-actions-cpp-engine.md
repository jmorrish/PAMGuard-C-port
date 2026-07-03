# GitHub Actions C++ Engine Workflow

Date: 2026-07-01

This checkpoint adds:

```text
.github/workflows/cpp-engine.yml
```

## What it runs

On Windows:

- configure CMake with Visual Studio 2022;
- build the C++ engine in Release;
- run CTest;
- compile-check Python ops scripts.

CTest includes the registered service smoke tests when PowerShell is available.

## Trigger paths

The workflow runs for changes under:

- `cpp-engine/**`
- `platform/**`
- `web-ui/**`
- `ops/**`
- the workflow file itself
