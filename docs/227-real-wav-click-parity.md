# Real-WAV click detector parity

Date: 2026-07-23

Authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`.

## Purpose

Synthetic fixtures pin individual click-detector branches. The real-WAV
differential runner checks the whole numerical chain over every sample:

1. PAMGuard's real `ByteConverter` decodes the WAV.
2. PAMGuard's real pre-filter and trigger-filter classes process it.
3. PAMGuard's real `TriggerFilter` runs inside a direct transcription of
   `ClickDetector.ChannelGroupDetector.lookForClicks`.
4. The production C++ `WavReader`, filters, and `ClickDetectorEngine` process
   the same audio with the same block boundaries.
5. The runner compares every signal-excess value and threshold decision, then
   every emitted click's start, duration, bitmaps, and maximum signal excess.

The Java harness can optionally apply PAMGuard's acquisition `DCFilter`. That
mode is useful for measuring acquisition-path sensitivity; exact trigger-value
comparison should use the default `0` seconds because the C++ click-engine
input contract begins at normalised PCM.

## Reproduce

```powershell
.\reference-tools\scripts\compare-click-wav.ps1 `
  -WavPath "G:\Reaper\wavs\01-260723_2325.wav"
```

Use `-KeepTrace` to retain the large per-sample CSV files, or
`-BlockSamples 2048` to check another chunk schedule.

The script verifies the machine-readable Java pin before it compiles or runs
the oracle. A mismatch is fatal and reports the first divergent click or
trigger sample.

## First controlled result

`01-260723_2325.wav` is mono 24-bit PCM at 44.1 kHz, containing 881,985
samples. With the PAMGuard click defaults and its normal 4,410-sample file
blocks:

- Java clicks: `122`
- C++ clicks: `122`
- click-boundary mismatches: `0`
- trigger-threshold mismatches: `0`
- maximum signal-excess error over all samples: `3.5527136788005009e-15 dB`

Applying PAMGuard's one-second acquisition DC filter still produces the same
122 click boundaries; maximum click signal-excess movement is below
`0.000096 dB`.

## Claim boundary

This is an end-to-end numerical click-core oracle, not a test of Swing display
retention, database storage, alarms, or legacy click files. It currently
accepts mono WAV input and PAMGuard's default detector settings. Multichannel
and arbitrary imported-setting runs remain follow-up extensions.
