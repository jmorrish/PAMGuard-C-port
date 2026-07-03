# FFmpeg Input Options

Date: 2026-07-01

## Purpose

Live sources are not all polite HTTP MP3 streams. Icecast/BUTT/direct Ethernet/UDP/RTSP deployments sometimes need FFmpeg input-side flags such as low-buffering, protocol timeout, transport selection, or probe tuning.

## Bridge option

`ffmpeg_stream_ingest` now accepts repeated input option tokens before `-i`:

```powershell
.\build\ffmpeg_stream_ingest.exe `
  --source "udp://239.0.0.1:1234" `
  --session "harbour-array" `
  --ffmpeg-input-option "-fflags" `
  --ffmpeg-input-option "nobuffer"
```

The bridge quotes each token separately when building the FFmpeg command.

## Supervisor manifest

Source manifests can pass the same tokens as an array:

```json
{
  "id": "harbour-array",
  "source": "udp://239.0.0.1:1234",
  "ffmpegInputOptions": ["-fflags", "nobuffer"]
}
```

Use this for input-side FFmpeg options only. Channel mapping and filtering should still use `audioFilter`.

## Validation

`ops/ingest_supervisor_command_smoke.py` now asserts that manifest `ffmpegInputOptions` expand into repeated `--ffmpeg-input-option` bridge arguments. Registered CTest help checks cover both `--api-key-env` and `--ffmpeg-input-option`.
