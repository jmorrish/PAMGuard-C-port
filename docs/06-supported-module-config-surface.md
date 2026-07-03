# Supported Module Config Surface

This document tracks the PAMGuard settings that must be represented in the C++ engine and web UI for supported modules.

The source of truth is the Java reference implementation. This document is a working inventory and must be completed during the requirements/scope freeze phase.

## FFT / Spectrogram Engine

Reference:

- `src/fftManager/FFTParameters.java`
- `src/fftManager/PamFFTProcess.java`
- `src/Spectrogram/WindowFunction.java`

Initial settings:

- `fftLength`
- `fftHop`
- `channelMap`
- `dataSource`
- `dataSourceName`
- `windowFunction`
- `clickRemoval`
- `clickThreshold`
- `clickPower`
- `spectrogramNoiseSettings`

Parity-sensitive behaviours:

- Channel selection and output map sorting
- Overlap/hop buffer handling
- Per-channel FFT slice counts
- Window function exact values
- Window RMS gain
- FFT packing/scaling
- Optional click removal before FFT

## Click Detector

Reference:

- `src/clickDetector/ClickParameters.java`
- `src/clickDetector/ClickDetector.java`

Initial settings:

- Raw data source
- Channel bitmap
- Trigger bitmap
- Channel groups
- Grouping type
- Minimum trigger channels
- Detection threshold in dB
- Long and short filter constants
- Pre-sample count
- Post-sample count
- Minimum separation
- Maximum click length
- Pre-filter settings
- Trigger-filter settings
- Noise sampling settings
- Background storage interval
- Click classifier type
- Online classification flag
- Echo rejection flags
- Trigger-function publishing flag
- Waveform display/filter settings where needed for output parity
- Localisation parameters
- Delay measurement parameters
- Click alarm settings where in scope

Parity-sensitive behaviours:

- Filter coefficient generation
- Trigger background state
- Trigger threshold crossing
- Multi-channel group trigger decision
- Click extraction sample window
- Click end/blanking/min-separation behaviour
- Echo rejection and classification timing
- Click spectra and amplitude calculations
- Offline/event/tracking state if included

## Whistle And Moan Detector

Reference:

- `src/whistlesAndMoans/WhistleToneParameters.java`
- `src/whistlesAndMoans/WhistleToneConnectProcess.java`

Initial settings:

- Data source
- Channel/sequence bitmap
- Channel groups
- Grouping type
- Connectivity type: 4 or 8
- Minimum frequency
- Maximum frequency
- Minimum pixels
- Minimum length
- Maximum crossing length
- Fragmentation method
- Spectrogram noise settings
- Show contour outline
- Keep/remove shape stubs
- Stretch contours
- Short whistle length
- Short whistle display policy
- Background measurement interval

Parity-sensitive behaviours:

- Noise-reduced FFT source selection
- Channel grouping and first-channel selection
- Frequency bin search range
- Connected-region construction
- Connectivity mode
- Region length/pixel filtering
- Fragmentation/relinking/stub removal
- Contour extraction and output timing
- Background measurements
- Localisation enablement based on channel/sequence maps

## Localisation And Click Tracking

Reference areas:

- `src/clickDetector/ClickDetector.java`
- `src/clickDetector/toad`
- `src/clickDetector/localisation`
- `src/Localiser`
- `src/targetMotionOld`
- `src/targetMotionModule`

Required config groups:

- Hydrophone positions
- Channel-to-hydrophone mapping
- Speed of sound
- Array orientation/heading
- GPS/vessel track source
- Delay measurement settings
- Bearing/localisation algorithm selection
- Click train/event tracking parameters

Parity-sensitive behaviours:

- Time-delay estimation windows
- Correlation settings
- Bearing ambiguity handling
- Group/event assignment
- Moving platform coordinate transforms
- Missing metadata fallback behaviour

