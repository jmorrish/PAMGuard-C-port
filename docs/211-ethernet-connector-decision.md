# Direct Ethernet Connector: A Decision Record

Date: 2026-07-22

## The deliverable

WP3 (`docs/05`) lists a "direct Ethernet connector interface" among the ingest deliverables. This records why no bespoke connector is being built, in the docs/200 style: with evidence, and with the conditions that would reverse the decision.

## What already covers it

Two network-audio surfaces exist, and between them they cover every case the programme has named:

1. **The PCM POST endpoint** *is* a direct network audio interface: any device or bridge that can open a TCP connection can stream raw f32le at the engine with sample-accurate continuity tracking, per-chunk attitude, and the audio archive behind it. This is the engine's native wire format.
2. **The FFmpeg bridge** passes its source URL and input options straight to FFmpeg (`ffmpeg_stream_ingest --source-url ... --ffmpeg-input-option ...`), and FFmpeg's protocol layer speaks `udp://` (including multicast), `rtp://`, `rtsp://`, `tcp://`, and `srt://` natively. A hydrophone frontend that multicasts RTP, an AoIP stream, or a UDP PCM pipe already flows through the existing bridge with an input URL — reconnect/backoff, realtime pacing, and session bootstrap included.

## What a bespoke connector would be for

A raw-socket listener would only add value for a **proprietary framing that no FFmpeg demuxer handles** — a specific instrument's UDP packet layout with embedded timestamps or channel maps. No such device protocol is specified anywhere in this programme's documents, and building a connector for an unspecified protocol means inventing the protocol, which is the docs/200 anti-pattern: code validated against nothing.

## What would reverse this

A named instrument with a documented packet format and a capture of its traffic. At that point the connector is a well-scoped bridge (packets → PCM POSTs, reusing the ingest supervisor), and this document is where it starts.
