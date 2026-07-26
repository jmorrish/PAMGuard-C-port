# PAMGuard-style visual module graph editor

Date: 2026-07-25

Java authority: PAMGuard `2.02.18e`,
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

## Claim

The browser Module Graph view now follows the operator interaction model of
PAMGuard's JavaFX `dataModelFX` package rather than exposing the graph as a
collection of JSON cards.

This is an interaction and information-architecture port, not a pixel copy of
JavaFX.

## Java semantics retained

The implementation was checked against:

- `DataModelPaneFX`, which combines a categorized module selector with a
  scrollable connection pane and zoom controls;
- `DataModelModulePane`, which groups available modules by PAMGuard menu
  category and supports drag creation;
- `ModuleConnectionNode`, which exposes module identity, settings, removal,
  processes, and data blocks;
- `DataModelSettingsManager` and `ModuleNodeParams`, which preserve node
  positions; and
- `DataModelStyle`, which distinguishes normal processing, display, and output
  connections.

The browser equivalent provides:

- a searchable, categorized module palette;
- drag/drop or double-click module creation;
- a pannable and zoomable dotted canvas;
- draggable named nodes with typed input and output sockets;
- compatible-target highlighting and drag-to-connect lines;
- blue processing, plum display, and orange output connections;
- node and connection selection, keyboard deletion, undo/redo, fit, and
  dependency-aware automatic layout;
- node runtime state and parity/category summaries;
- a right-click menu for Configure, Inspect data blocks, Rename, Duplicate,
  Enable/Disable, Reset processing state, and Remove; and
- saved per-browser node positions, pan, and zoom.

The fixed-session sidebar is hidden while the visual graph is active. This
prevents the legacy and composable configuration models appearing to be one
workflow.

## Configuration

Configure opens a modern module dialog instead of raw JSON.

Dedicated PAMGuard-ordered sections are provided for:

- Acquisition;
- FFT, including Java click removal;
- Decimator;
- Filter;
- Spectrogram Noise Reduction; and
- Click Detector.

The forms use typed number, enum, boolean, channel, array, and nested-object
controls generated from the authoritative module settings schema. Channel
bitmaps and FFT channel lists appear as channel selectors. Java choices such as
None/Linear/Quadratic decimator interpolation and threshold final-output modes
retain their Java ordering and stored integer values.

Every other registered module receives a schema-generated guided form. The
complete JSON object remains available under Advanced JSON for development and
forward compatibility.

Saving a dialog changes only the browser draft. The existing Validate and
Apply transaction remains authoritative: invalid graphs do not replace the
running runtime.

## Validation

`visual-graph-browser-smoke.ps1` drives the editor through Chrome DevTools and
proves:

1. the graph tab, 31-item categorized palette, and source node render;
2. an FFT can be added and connected from Acquisition by socket drag;
3. the right-click Configure action opens guided FFT and Click Removal
   sections without showing raw JSON;
4. a guided FFT-length edit reaches the draft;
5. server validation accepts the draft;
6. Apply persists exactly two modules and one typed connection; and
7. an actual node drag plus pan/zoom survives browser reload.

`workspace-browser-smoke.ps1` also passes with the visual editor alongside all
seven display providers, two independent spectrograms, click and level
displays, source-loss handling, and service-restored workspace layouts.

## Boundary

Visual layout is currently browser-local UI metadata. Scientific graph
identity, settings, and connections remain service-persisted and portable.
Cross-browser/server synchronization of canvas positions can be added later
without changing the processing graph contract.

The legacy fixed-session pages remain available as diagnostic/reference
surfaces outside the Module Graph view. They are not the composable graph
editor.
