# Signal routing and detector graph evidence

Date: 2026-07-24

Java authority: PAMGuard `2.02.18e`,
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`

## Claim

The parity-tested detector maths can now run behind independently named,
typed graph nodes. Existing kernels were wrapped, not re-derived.

## Foundational routing nodes

- Acquisition produces one timestamped, clock-domain-labelled raw-audio block
  with explicit optional per-channel calibration.
- Amplifier applies independent linear channel gains and adjusts calibrated
  amplitude offsets.
- Patch Panel applies the PAMGuard input-by-output mixing matrix and preserves
  calibration only where the resulting lineage is unambiguous.
- IIR Filter exposes the existing PAMGuard-compatible filter path and prevents
  unselected channels leaking into its output.
- Decimator exposes selectable output rate, anti-alias order, interpolation,
  channels, and a Java-generated cross-chunk fixture, with selected-channel
  metadata on its output.
- FFT owns independent source, length, hop, PAMGuard window, channel selection,
  Java-exact click removal, and a frequency/channel-complete output block.
  Multiple FFT instances share one acquisition without duplicating it.
- Spectrogram Noise Reduction is a reusable FFT-to-FFT node exposing the
  already parity-tested median, average-subtraction, Gaussian-kernel, and
  threshold stages, so independent FFT branches can select independent noise
  chains.

## Graph-wrapped analysis nodes

- Click detector, features, Basic/Sweep classifier, angle-veto behaviour,
  matched-template classifier, simple click trains, MHT trains and train
  classifiers.
- Correlation delay, array-shape selection, pair/LSQ/ML-grid bearings, array
  and earth vectors.
- Noise Band Monitor, FFT Noise Monitor, LTSA.
- Ishmael energy sum, spectrogram correlation, and matched filter.
- Whistles & Moans noise reduction, peaks, connected regions, grouping, and
  contour output.

Outputs are separate typed blocks for raw audio, FFT, click, classification,
train, contour, noise, LTSA, function/detection, delay, and bearing results.
Any number of displays or downstream nodes can subscribe to each output.
Click outputs advertise detection/waveform/overlay capabilities, so the same
typed block can simultaneously drive classification, trains, localisation,
Alarm/Event Counter, Clip Generator, and displays.

## Evidence

- Every pre-existing Java fixture remains in CTest.
- `module_graph_and_typed_data_blocks` executes independent full-band and
  decimated FFT branches and graph-wrapped click, classification, localisation,
  MHT, noise, LTSA, Ishmael, and whistle paths.
- The click graph retains the pinned 71-sample start and 43-sample duration
  result for the focused parity signal.
- The graph MHT test retains a six-or-more-click best train and the pinned IDI
  classifier species result.
- `decimator_java_stream_parity` proves Java equivalence including state across
  chunk boundaries.
- `pamfft_frame_click_removal_parity` proves the FFT pre-window click-removal
  path against a fixture exported from the pinned Java implementation, with
  maximum absolute error `1.42109e-13`; a disabled-removal negative control
  diverges by 613.
- The executable click graph also proves alarm activation and complete
  pre/post-trigger clip publication without changing the pinned click result.
- The focused graph test routes FFT data through the reusable spectrogram-noise
  node and verifies transformed output, metadata propagation, invalid channel
  rejection, clock-domain rejection, and calibrated gain lineage.

## Claim boundary

The graph does not upgrade a foundation-only detector claim to Java parity.
The parity ledger and the individual pre-existing fixture documents remain
authoritative for each algorithm.
