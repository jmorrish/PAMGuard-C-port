# FFT statistics noise monitor

Date: 2026-07-24

Authority: PAMGuard `2.02.18e`, commit
`dca55c81ef6f1498a8a3b926c69e7182afb915ee`.

This ports `noiseMonitor.NoiseProcess`, which is separate from both the
octave-family `noiseBandMonitor` filter bank and LTSA.

## Processing

For each selected FFT channel and configured frequency band the monitor:

- integrates magnitude-squared FFT bins, including fractional first/last-bin
  contributions;
- samples either every FFT slice (`useAll`, the default) or `nMeasures`
  jittered positions per interval;
- emits mean, median, lower/upper 95%, minimum, and maximum;
- converts the six powers through PAMGuard's `fftBandAmplitude2dB`, including
  FFT length, negative-frequency, acquisition calibration, and FFT window RMS
  gain corrections.

The port preserves observable Java quirks: the highest selected channel closes
the shared interval before its boundary slice is measured, while lower
channels have already seen that slice; the lower median and percentile
indices and the unused-slot offsets are reproduced exactly. PAMGuard does not
flush a partial interval on stop, and neither does the port.

## Validation

`FftNoiseFixtureExporter` drives the real `NoiseProcess` private FFT-data path.
Only the final hardware-calibration call is replaced by an identity fixture
seam, leaving Java's integration, interval, multi-channel, and statistics
bytecode intact. `fft_noise_monitor_parity` replays two channels, two bands,
fractional bin edges, and two interval boundaries with maximum error below
`1e-12`.

## API

```json
{
  "fftNoise": {
    "enabled": true,
    "channelBitmap": 3,
    "measurementIntervalSeconds": 60,
    "nMeasures": 100,
    "useAll": true,
    "bands": [
      {
        "name": "Audible",
        "lowFrequencyHz": 100,
        "highFrequencyHz": 20000
      }
    ]
  }
}
```

Results are returned as `fftNoise` at schema version 31. The `.psfx`
converter maps `NoiseSettings` and `NoiseMeasurementBand`, session readback
returns all settings, and the browser monitoring panel renders band mean and
median values.
