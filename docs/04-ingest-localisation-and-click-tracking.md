# Ingest, Localisation, And Click Tracking

## Input Normalization

All sources must become:

```text
timestamped PCM audio frames + sample rate + channel count + source metadata
```

The C++ engine should not know whether the source was Icecast, BUTT, WAV, MP3, FLAC, or a direct Ethernet device.

## Supported Source Types

- WAV / BWF / FLAC files
- Streamed WAV / PCM
- Icecast / BUTT HTTP streams
- MP3 or AAC streams via decoder layer
- Direct Ethernet protocols via source-specific connectors

## Lossy Audio Warning

MP3/AAC can affect high-frequency transients and inter-channel timing. They may be useful for monitoring and broad detection, but localisation and click tracking should prefer PCM/FLAC or another lossless/synchronized format.

## Multi-Channel Model

Multi-channel sources must be represented as one coherent array stream:

```text
frame 0: ch0, ch1, ch2, ...
frame 1: ch0, ch1, ch2, ...
```

Required metadata:

- Sample rate
- Channel count
- Channel ordering
- Channel-to-hydrophone mapping
- Hydrophone positions
- Calibration data where available
- Speed of sound
- Array orientation / heading data where needed
- GPS/vessel track where needed

## Localisation Flow

```text
multi-channel PCM
-> per-channel filtering / trigger functions
-> group trigger decision
-> synchronized waveform extraction across channel group
-> time-delay estimation
-> bearing/range/localisation calculation
-> click tracking / train association
```

## Click Tracking Data Model

Each click should retain:

- Source/session ID
- Absolute/sample time
- Channel group
- Waveform window by channel
- Trigger channel(s)
- Peak amplitude and spectral features
- Time-delay estimates
- Bearing/localisation outputs
- Classifier result
- Echo rejection result
- Train/event assignment

