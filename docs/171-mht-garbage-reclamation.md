# MHT Garbage Reclamation

Date: 2026-07-10

## Purpose

`docs/170` shipped MHT click trains with a documented caveat: kernel memory grew with session length. This slice ports PAMGuard's `MHTGarbageBot` decision logic and the `MHTKernel.clearKernelGarbage`/`IDIManager.trimData` reclamation so MHT mode is safe for long live streams.

## Reference semantics ported

- **Hard reset** (`MHTGarbageBot.checkCTGarbageCollect`): before adding a detection, if the kernel holds more than `nPruneBackStart` detections and either the gap since the last detection exceeds `maxCoast * maxICI` (1.2 s at defaults) or the kernel exceeds the detection hard limit (10000), remaining hypotheses are confirmed, confirmed trains drained, and the kernel fully reset (`clearKernel`).
- **Trailing-zeros trim**: every 20 detections, the first index referenced by any hypothesis is found (`getFirstDetectionIndex`); if no hypothesis references anything the kernel is reset, and if the dead prefix exceeds `MIN_TRIM_COUNT` (100) the kernel is trimmed (`clearKernelGarbage`): data units and hypothesis bitsets drop the prefix, `kcount` shifts, and the provider trims its master time series (`IDIManager.trimData` — retained values keep the original time origin and the first-detection reference is unchanged, exactly as in Java).
- PAMGuard deletes confirmed tracks during reclamation, so the session drains confirmed trains before every reset/trim, and its click sample/time logs are trimmed in step with the kernel indices.

## Validation

- `session_mht_train_wiring` gains a three-burst case: bursts of eight clicks separated by ~9 s gaps produce one strong train per burst, none spanning a reset gap — exercising the hard-reset path end-to-end through the session.
- Full CTest suite passes.

## Claim boundary

The garbage thresholds are PAMGuard constants (not configurable). `docs/170`'s long-stream caveat is closed; per-session MHT parameters, the remaining chi2 variables, the electrical noise filter, and click train classification are still open.
