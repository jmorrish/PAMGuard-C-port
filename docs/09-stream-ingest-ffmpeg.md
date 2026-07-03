# Stream ingest bridge

The C++ engine accepts deterministic PCM. Codec, container, and network handling should sit in an ingest layer so the detector maths remains reproducible.

Recommended bridge:

```powershell
ffmpeg -i "<icecast-or-file-url>" -f f32le -acodec pcm_f32le - |
  .\build\raw_pcm_stream_cli.exe 48000 2 4096 1024 512 --click
```

Input contract:

- PCM format: interleaved little-endian 32-bit float samples.
- Channel order: unchanged from FFmpeg output unless `-map_channel`, `-ac`, or `pan` is used.
- One engine session per user/source stream.
- For MP3/Icecast/BUTT, FFmpeg handles reconnect/codec/container concerns; the engine only sees decoded PCM chunks.
- For multi-channel localisation, preserve native channel count and attach array geometry in the session config.

Scaling pattern:

- Web/API process owns a `SessionManager`.
- Each active user/source gets an `AnalysisSession`.
- Each session owns independent FFT, click, whistle, and localisation state.
- Ingest workers decode streams and push PCM chunks into the matching session.
