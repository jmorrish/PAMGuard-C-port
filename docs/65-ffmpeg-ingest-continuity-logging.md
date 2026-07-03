# FFmpeg ingest continuity logging

`ffmpeg_stream_ingest` now parses the engine response from each PCM POST and includes timeline continuity in its chunk logs.

Example:

```text
posted chunk 12 startSample=45056 continuity=contiguous delta=0 nextExpected=49152
```

Fields:

- `continuity`: engine-reported `first`, `contiguous`, `gap`, `overlap`, `unknown`, or `unparsed`.
- `delta`: posted `startSample` minus the engine's expected start sample.
- `nextExpected`: the engine's expected start sample after accepting the chunk.

This makes supervised restarts and multi-source deployments easier to diagnose. A healthy continuous stream should report `first` once and `contiguous` afterwards. `gap` or `overlap` means the session timeline no longer matches the posted PCM sequence.
