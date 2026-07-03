# C++ HTTP service

The C++ backend service wraps `SessionManager` directly.

Build target:

```powershell
cd C:\python\PAMGuard\pamguard-enterprise-port\cpp-engine
.\scripts\build-msvc.ps1
.\build\pamguard_engine_service.exe 8080
```

Health:

```powershell
Invoke-RestMethod http://localhost:8080/health
```

Create a session:

```powershell
$body = @{
  sessionId = "demo"
  sourceId = "icecast-demo"
  sampleRateHz = 48000
  channelCount = 2
  fft = @{
    length = 1024
    hop = 512
    channels = @(0, 1)
  }
  click = @{
    enabled = $true
    localisation = $true
    thresholdDb = 10
  }
  whistle = @{
    enabled = $true
    regionEnabled = $true
    minPixels = 20
    minLength = 10
    connectType = 8
    keepShapeStubs = $false
    fragmentationMethod = 3
    maxCrossLength = 5
  }
} | ConvertTo-Json -Depth 8

Invoke-RestMethod -Method Post -Uri http://localhost:8080/sessions -Body $body -ContentType "application/json"
```

Push decoded PCM:

```powershell
ffmpeg -i "<stream-or-file>" -f f32le -acodec pcm_f32le - |
  Invoke-WebRequest -Method Post -Uri "http://localhost:8080/sessions/demo/pcm-f32le?startSample=0" -ContentType "application/octet-stream" -InFile -
```

Notes:

- The service expects interleaved little-endian `float32` PCM.
- One session should represent one user/source stream.
- `startSample` must be monotonic for stateful detectors and waveform capture.
- Production ingest should use a worker that chunks FFmpeg stdout and posts sequential PCM blocks with correct `startSample`.
