# PAMGuard-Style Web UI

Date: 2026-07-23

## Purpose

Restructures the web console into the shape PAM operators already know — PAMGuard's — with a modern skin. The user's direction: "it needs to feel like software they know… however PAMGuard's UI is very outdated so we can modernise it." The previous single-scroll page (two big cards, collapsible groups) worked but read as a developer console, not an analyst application.

## Structure (PAMGuard's, modernised)

- **Menu bar** — File (Connection…, Create/Flush/Delete session, List sessions), **Detection** (one settings dialog per module, exactly as PAMGuard's Detection menu: Hydrophone array, Click detector, Click features, Whistle & moan peaks, Whistle & moan contours, Noise & monitoring modules), Display (tab switching, Test signal…), Help (OpenAPI, health). Right-aligned live status: connection, schema version, session count, capture availability (polled every 10 s).
- **Left sidebar** — PAMGuard's side-panel role: a Session card (id, sample rate, channels, FFT), the **Sound acquisition** card (source type, device/URL, start/stop), and a **Modules list** with per-module Settings buttons opening the same dialogs as the Detection menu.
- **Tabbed displays** — Spectrogram (the live waterfall, now 480 px tall with the delay control), Detections (metric tiles, monitoring cards, contour summaries), Archive (session list + detection archive query/export), Console (raw actions + last response body, for power users).
- **Status bar** — session, rate × channels, sample continuity, capture state (mirrored once a second without touching core handlers).
- **Native `<dialog>` settings windows** — dark-themed modals, no external dependencies; the page remains a single self-contained file served by the engine.

## How it was done safely

The working `<script>` block (capture, push-stream live view, delay-locked waterfall, archive queries, CSV export, session management — everything validated in `docs/222`) was **carried over byte-for-byte** by the page builder; only a thin chrome layer (menus, tabs, dialog open/close, status mirroring) was added. Two automated safety nets: every `$("id")` referenced by the script is asserted to exist in the new DOM (126 ids), and duplicate-id detection across the page (138 unique). The script block parses; the served page returns 200 with the new structure.

## Claim boundary

This is the PAMGuard *shell* — menu bar, module settings dialogs, side panel, tabbed displays, status bar — not yet the full display feature set: no detection overlays (click marks / whistle contours) drawn on the live spectrogram, no map, no data-model editor; module settings apply on session (re)create rather than live. One known minor quirk: a one-shot "Send test PCM block" issued from the Console tab renders its spectrogram while that canvas is hidden, so the drawing is sized wrong until the next live frame or resize — the live capture path (the primary one) is unaffected. Browser-level interaction (menus, dialogs, tabs) is validated by construction and parse checks, not by an automated browser test.
