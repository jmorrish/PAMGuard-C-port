param(
    [int]$Port = 18191,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build")
)

$ErrorActionPreference = "Stop"
$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
$ingestExe = Join-Path $BuildDir "ffmpeg_stream_ingest.exe"
if (-not (Test-Path $serviceExe)) {
    throw "Service executable not found: $serviceExe"
}
if (-not (Test-Path $ingestExe)) {
    throw "FFmpeg ingest executable not found: $ingestExe"
}

$tempRoot = [System.IO.Path]::GetTempPath()
$root = Join-Path $tempRoot ("pamguard-module-runtime-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $root | Out-Null
$graphFile = Join-Path $root "graph.json"
$pcmFile = Join-Path $root "audio.f32le"
$streamFile = Join-Path $root "stream.ndjson"
$streamErrorFile = Join-Path $root "stream.stderr"
$audioStreamFile = Join-Path $root "audio-stream.f32le"
$audioStreamErrorFile = Join-Path $root "audio-stream.stderr"
$framedAudioStreamFile = Join-Path $root "audio-stream.pga1"
$framedAudioStreamErrorFile = Join-Path $root "audio-stream-pga1.stderr"
$selectedStreamFile = Join-Path $root "selected-stream.ndjson"
$selectedStreamErrorFile = Join-Path $root "selected-stream.stderr"
$oldGraphFile = $env:PAMGUARD_MODULE_GRAPH_FILE
$oldLegacyModelCompat = $env:PAMGUARD_LEGACY_MODEL_COMPAT
$service = $null
$stream = $null
$audioStream = $null
$framedAudioStream = $null

try {
    $env:PAMGUARD_MODULE_GRAPH_FILE = $graphFile
    $env:PAMGUARD_LEGACY_MODEL_COMPAT = "1"
    $service = Start-Process -FilePath $serviceExe -ArgumentList "$Port" -PassThru -WindowStyle Hidden
    $base = "http://127.0.0.1:$Port"
    $healthy = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try {
            $health = Invoke-RestMethod -Method Get -Uri "$base/health"
            if ($health.ok) {
                $healthy = $true
                break
            }
        }
        catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $healthy) {
        throw "Module runtime service did not become healthy"
    }
    $coldStatus = Invoke-RestMethod -Method Get -Uri "$base/module-runtime/status"
    if ($coldStatus.running -or $coldStatus.graphRevision -ne 0) {
        throw "Cold module runtime did not open idle at graph revision 0"
    }

    $graph = @{
        expectedRevision = 0
        schemaVersion = 1
        revision = 0
        modules = @(
            @{ id = "source"; typeId = "pamguard.acquisition"; name = "Input"; enabled = $true; settings = @{ sourceId = "test"; sampleRateHz = 48000; channelCount = 2; subtractDC = $false; dcTimeConstantSeconds = 1 } },
            @{ id = "decimator"; typeId = "pamguard.decimator"; name = "Low band"; enabled = $true; settings = @{ outputSampleRateHz = 12000; filter = @{ type = "butterworth"; band = "lowPass"; order = 6; lowPassFreqHz = 6000; highPassFreqHz = 2000; passBandRippleDb = 2; stopBandRippleDb = 2; chebyGamma = 3; arbitraryFrequenciesHz = @(); arbitraryGainsDb = @() }; interpolation = 0; channelBitmap = 4294967295 } },
            @{ id = "fft-full"; typeId = "pamguard.fft"; name = "Full FFT"; enabled = $true; settings = @{ fftLength = 8; fftHop = 4; channels = @(0, 1) } },
            @{ id = "fft-low"; typeId = "pamguard.fft"; name = "Low FFT"; enabled = $true; settings = @{ fftLength = 8; fftHop = 4; channels = @(0, 1) } },
            @{ id = "levels"; typeId = "pamguard.level-meter"; name = "Levels"; enabled = $true; settings = @{ intervalSeconds = 0.001; channelBitmap = 3 } },
            @{ id = "watch"; typeId = "pamguard.effort-monitor"; name = "Effort"; enabled = $true; settings = @{ defaultCategory = "effort" } },
            @{ id = "storage"; typeId = "pamguard.storage-health"; name = "Storage"; enabled = $true; settings = @{ path = $root; warningFreePercent = 0; intervalSeconds = 30 } }
        )
        connections = @(
            @{ id = "c1"; source = @{ moduleId = "source"; portId = "audio" }; target = @{ moduleId = "fft-full"; portId = "input" } },
            @{ id = "c2"; source = @{ moduleId = "source"; portId = "audio" }; target = @{ moduleId = "decimator"; portId = "input" } },
            @{ id = "c3"; source = @{ moduleId = "decimator"; portId = "output" }; target = @{ moduleId = "fft-low"; portId = "input" } },
            @{ id = "c4"; source = @{ moduleId = "source"; portId = "audio" }; target = @{ moduleId = "levels"; portId = "input" } }
        )
    } | ConvertTo-Json -Depth 12 -Compress
    $applied = Invoke-RestMethod -Method Put -Uri "$base/module-graph" -ContentType "application/json" -Body $graph
    if (-not $applied.applied -or
        $applied.revision -ne 1 -or
        $applied.running -or
        -not (Test-Path $graphFile)) {
        throw "Executable module graph was not applied, persisted, and left idle"
    }
    $idleAfterApply = Invoke-RestMethod -Method Get -Uri "$base/module-runtime/status"
    if ($idleAfterApply.running -or
        @($idleAfterApply.modules |
            Where-Object { $_.state -ne "prepared" }).Count -ne 0) {
        throw "Stopped graph edit implicitly started a module"
    }
    $started = Invoke-RestMethod -Method Post -Uri "$base/module-runtime/control" -ContentType "application/json" -Body '{"action":"start"}'
    if (-not $started.running -or $started.graphRevision -ne 1) {
        throw "Module runtime did not start explicitly"
    }

    $runningGraph = $graph | ConvertFrom-Json
    $runningGraph.expectedRevision = 1
    (@($runningGraph.modules) |
        Where-Object { $_.id -eq "source" }).name =
        "Guarded edit"
    $runningEditStatus = 0
    $runningEditIssue = ""
    try {
        Invoke-WebRequest `
            -Method Put `
            -Uri "$base/module-graph" `
            -ContentType "application/json" `
            -Body ($runningGraph |
                ConvertTo-Json -Depth 12 -Compress) `
            -UseBasicParsing | Out-Null
        throw "Graph endpoint accepted a running mutation without stopRuntime"
    }
    catch [System.Net.WebException] {
        $response = $_.Exception.Response
        $runningEditStatus = [int]$response.StatusCode
        try {
            $errorText = $_.ErrorDetails.Message
            if (-not $errorText) {
                $reader = [System.IO.StreamReader]::new(
                    $response.GetResponseStream())
                try {
                    $errorText = $reader.ReadToEnd()
                }
                finally {
                    $reader.Dispose()
                }
            }
            if ($errorText) {
                $errorBody = $errorText | ConvertFrom-Json
                $runningEditIssue =
                    @($errorBody.issues)[0].code
            }
        }
        finally {
            $response.Dispose()
        }
    }
    if ($runningEditStatus -ne 409 -or
        $runningEditIssue -ne "runtime_running") {
        throw "Running graph mutation returned HTTP $runningEditStatus/$runningEditIssue instead of 409/runtime_running"
    }

    $runningGraph | Add-Member `
        -NotePropertyName stopRuntime `
        -NotePropertyValue $true
    $safeApplied = Invoke-RestMethod `
        -Method Put `
        -Uri "$base/module-graph" `
        -ContentType "application/json" `
        -Body ($runningGraph |
            ConvertTo-Json -Depth 12 -Compress)
    if (-not $safeApplied.applied -or
        $safeApplied.revision -ne 2 -or
        -not $safeApplied.stoppedRuntime -or
        $safeApplied.running) {
        throw "Explicit stopRuntime graph transaction did not stop, apply, and remain idle"
    }

    $stoppedGraph = $runningGraph |
        ConvertTo-Json -Depth 12 |
        ConvertFrom-Json
    $stoppedGraph.PSObject.Properties.Remove("stopRuntime")
    $stoppedGraph.expectedRevision = 2
    (@($stoppedGraph.modules) |
        Where-Object { $_.id -eq "source" }).name =
        "Stopped edit"
    $stoppedApplied = Invoke-RestMethod `
        -Method Put `
        -Uri "$base/module-graph" `
        -ContentType "application/json" `
        -Body ($stoppedGraph |
            ConvertTo-Json -Depth 12 -Compress)
    if (-not $stoppedApplied.applied -or
        $stoppedApplied.revision -ne 3 -or
        $stoppedApplied.running -or
        $stoppedApplied.stoppedRuntime) {
        throw "Stopped graph edit did not preserve idle lifecycle state"
    }
    $staleStatus = 0
    try {
        Invoke-WebRequest -Method Put -Uri "$base/module-graph" -ContentType "application/json" -Body $graph | Out-Null
    }
    catch {
        $staleStatus = [int]$_.Exception.Response.StatusCode
    }
    if ($staleStatus -ne 409) {
        throw "Stale graph update returned HTTP $staleStatus instead of 409"
    }
    $invalidGraph = $graph | ConvertFrom-Json
    $invalidGraph.expectedRevision = 3
    (@($invalidGraph.modules) | Where-Object { $_.id -eq "fft-full" }).settings.fftLength = 3
    $validationGraph = $invalidGraph | ConvertTo-Json -Depth 12 |
        ConvertFrom-Json
    $validationGraph.PSObject.Properties.Remove(
        "expectedRevision")
    $invalidValidation = Invoke-RestMethod `
        -Method Post `
        -Uri "$base/module-graph/validate" `
        -ContentType "application/json" `
        -Body ($validationGraph | ConvertTo-Json -Depth 12 -Compress)
    if ($invalidValidation.valid -or
        -not (@($invalidValidation.issues) |
            Where-Object {
                $_.code -eq "invalid_runtime_settings"
            })) {
        throw "Graph validation did not preflight executable module settings"
    }
    $invalidStatus = 0
    try {
        Invoke-WebRequest -Method Put -Uri "$base/module-graph" -ContentType "application/json" -Body ($invalidGraph | ConvertTo-Json -Depth 12 -Compress) | Out-Null
    }
    catch {
        $invalidStatus = [int]$_.Exception.Response.StatusCode
    }
    if ($invalidStatus -ne 422) {
        throw "Invalid executable graph returned HTTP $invalidStatus instead of 422"
    }
    $graphAfterRejection = Invoke-RestMethod -Method Get -Uri "$base/module-graph"
    if ($graphAfterRejection.revision -ne 3) {
        throw "Rejected graph update changed the authoritative revision"
    }
    $blocks = Invoke-RestMethod -Method Get -Uri "$base/data-blocks"
    if ($blocks.graphRevision -ne 3 -or $blocks.count -ne 7 -or
        -not (@($blocks.dataBlocks) | Where-Object { $_.id -eq "block:fft-low:fft" })) {
        throw "Rejected graph update changed the live runtime or its FFT branches"
    }
    $status = Invoke-RestMethod -Method Get -Uri "$base/module-runtime/status"
    if ($status.running -or $status.count -ne 7 -or
        @($status.modules | Where-Object { $_.state -eq "prepared" }).Count -ne 7) {
        throw "Module lifecycle status did not expose all prepared idle nodes"
    }
    $started = Invoke-RestMethod -Method Post -Uri "$base/module-runtime/control" -ContentType "application/json" -Body '{"action":"start"}'
    if (-not $started.running -or $started.graphRevision -ne 3) {
        throw "Module runtime did not start the edited graph"
    }
    $stopped = Invoke-RestMethod -Method Post -Uri "$base/module-runtime/control" -ContentType "application/json" -Body '{"action":"stop"}'
    if ($stopped.running) {
        throw "Module runtime stop control did not stop the graph"
    }
    $reset = Invoke-RestMethod -Method Post -Uri "$base/module-runtime/control" -ContentType "application/json" -Body '{"action":"reset","restart":true}'
    if (-not $reset.running -or $reset.graphRevision -ne 3) {
        throw "Module runtime reset/restart did not preserve the graph revision"
    }

    Stop-Process -Id $service.Id -Force
    $service.WaitForExit()
    $service = Start-Process `
        -FilePath $serviceExe `
        -ArgumentList "$Port" `
        -PassThru `
        -WindowStyle Hidden
    $healthy = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try {
            $health = Invoke-RestMethod -Method Get -Uri "$base/health"
            if ($health.ok) {
                $healthy = $true
                break
            }
        }
        catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $healthy) {
        throw "Module runtime service did not recover after persistence restart"
    }
    $restoredGraph = Invoke-RestMethod -Method Get -Uri "$base/module-graph"
    $restoredBlocks = Invoke-RestMethod -Method Get -Uri "$base/data-blocks"
    $restoredStatus = Invoke-RestMethod -Method Get -Uri "$base/module-runtime/status"
    if ($restoredGraph.revision -ne 3 -or
        $restoredBlocks.graphRevision -ne 3 -or
        $restoredBlocks.count -ne 7 -or
        -not (@($restoredBlocks.dataBlocks) |
            Where-Object { $_.id -eq "block:fft-low:fft" }) -or
        $restoredStatus.running -or
        @($restoredStatus.modules |
            Where-Object { $_.state -eq "prepared" }).Count -ne 7) {
        throw "Persisted graph did not restore idle with stable module/block identities"
    }
    $restoredStart = Invoke-RestMethod -Method Post -Uri "$base/module-runtime/control" -ContentType "application/json" -Body '{"action":"start"}'
    if (-not $restoredStart.running -or
        $restoredStart.graphRevision -ne 3) {
        throw "Restored graph did not start explicitly"
    }

    $stream = Start-Process -FilePath "curl.exe" -ArgumentList @(
        "--silent",
        "--max-time", "3",
        "$base/data-blocks/block%3Afft-low%3Afft/stream"
    ) -RedirectStandardOutput $streamFile -RedirectStandardError $streamErrorFile -PassThru -WindowStyle Hidden
    $audioStream = Start-Process -FilePath "curl.exe" -ArgumentList @(
        "--silent",
        "--max-time", "3",
        "$base/data-blocks/block%3Asource%3Aaudio/audio-f32le?channels=0%2C1"
    ) -RedirectStandardOutput $audioStreamFile -RedirectStandardError $audioStreamErrorFile -PassThru -WindowStyle Hidden
    $framedAudioStream = Start-Process -FilePath "curl.exe" -ArgumentList @(
        "--silent",
        "--max-time", "3",
        "$base/data-blocks/block%3Asource%3Aaudio/audio-f32le?channels=0%2C1&format=framed"
    ) -RedirectStandardOutput $framedAudioStreamFile -RedirectStandardError $framedAudioStreamErrorFile -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 250

    $bytes = New-Object byte[] (64 * 2 * 4)
    for ($frame = 0; $frame -lt 64; $frame++) {
        for ($channel = 0; $channel -lt 2; $channel++) {
            $value = [single]([Math]::Sin($frame * 0.2 + $channel))
            [BitConverter]::GetBytes($value).CopyTo($bytes, (($frame * 2 + $channel) * 4))
        }
    }
    [System.IO.File]::WriteAllBytes($pcmFile, $bytes)
    $accepted = Invoke-RestMethod -Method Post -Uri "$base/module-runtime/acquisitions/source/pcm-f32le?startSample=0&timeMs=1000" -ContentType "application/octet-stream" -InFile $pcmFile
    if (-not $accepted.accepted -or $accepted.inputFrames -ne 64) {
        throw "Runtime acquisition endpoint did not accept the PCM chunk"
    }
    $operatorEvent = Invoke-RestMethod `
        -Method Post `
        -Uri "$base/module-runtime/operator-inputs/watch/events" `
        -ContentType "application/json" `
        -Body '{"label":"On effort","notes":"runtime smoke","value":1,"timeMs":1100}'
    if (-not $operatorEvent.accepted -or $operatorEvent.graphRevision -ne 3) {
        throw "Operator-input endpoint did not accept the effort observation"
    }
    $stream.WaitForExit()
    $audioStream.WaitForExit()
    $framedAudioStream.WaitForExit()
    $lines = @([System.IO.File]::ReadAllLines($streamFile) | Where-Object { $_.Trim() })
    if ($lines.Count -ne 6) {
        throw "Expected 6 decimated two-channel FFT frames, received $($lines.Count)"
    }
    $audioLength = (Get-Item $audioStreamFile).Length
    if ($audioLength -ne (64 * 2 * 4)) {
        throw "Binary audio stream returned $audioLength bytes instead of 512"
    }
    $framedAudio = [System.IO.File]::ReadAllBytes(
        $framedAudioStreamFile)
    if ($framedAudio.Length -ne (40 + 64 * 2 * 4) -or
        [System.Text.Encoding]::ASCII.GetString(
            $framedAudio,
            0,
            4) -ne "PGA1" -or
        [BitConverter]::ToUInt32($framedAudio, 4) -ne 40 -or
        [BitConverter]::ToUInt32($framedAudio, 8) -ne 2 -or
        [BitConverter]::ToUInt32($framedAudio, 12) -ne 64 -or
        [BitConverter]::ToInt64($framedAudio, 16) -ne 1000 -or
        [BitConverter]::ToUInt64($framedAudio, 24) -ne 0 -or
        [BitConverter]::ToUInt64($framedAudio, 32) -ne 0) {
        throw "Framed audio stream did not preserve PGA1 timing, shape, or drop metadata"
    }
    $first = $lines[0] | ConvertFrom-Json
    if ($first.sourceBlockId -ne "block:fft-low:fft" -or
        $first.typeId -ne "pamguard.fft" -or
        @($first.payload.magnitudeSquared).Count -ne 5) {
        throw "Selected data-block stream returned source=$($first.sourceBlockId) type=$($first.typeId) bins=$(@($first.payload.magnitudeSquared).Count)"
    }
    $history = Invoke-RestMethod -Method Get -Uri "$base/data-blocks/block%3Afft-low%3Afft/history?limit=3"
    if ($history.count -ne 3 -or
        $history.units[0].sourceBlockId -ne "block:fft-low:fft" -or
        $history.stats.published -ne 6 -or
        $history.stats.historySize -ne 6) {
        throw "Bounded data-block history or health counters are incorrect"
    }
    $blocksAfter = Invoke-RestMethod -Method Get -Uri "$base/data-blocks"
    $lowBlock = @($blocksAfter.dataBlocks) |
        Where-Object { $_.id -eq "block:fft-low:fft" } |
        Select-Object -First 1
    if (-not $lowBlock -or $lowBlock.historyCapacity -lt 6 -or
        $lowBlock.stats.published -ne 6 -or
        $null -eq $lowBlock.oldestTimeMs -or
        $null -eq $lowBlock.latestTimeMs -or
        $lowBlock.latestStartSample -lt
            $lowBlock.oldestStartSample) {
        throw "Data-block catalogue did not expose live retention and health"
    }
    $levels = Invoke-RestMethod -Method Get -Uri "$base/data-blocks/block%3Alevels%3Alevels/history?limit=2"
    $effort = Invoke-RestMethod -Method Get -Uri "$base/data-blocks/block%3Awatch%3Aevents/history?limit=2"
    $storage = Invoke-RestMethod -Method Get -Uri "$base/data-blocks/block%3Astorage%3Astatus/history?limit=2"
    if ($levels.count -lt 1 -or
        @($levels.units[0].payload.rmsDbfs).Count -ne 2 -or
        $effort.count -ne 1 -or
        $effort.units[0].payload.category -ne "effort" -or
        $storage.count -lt 1 -or
        -not $storage.units[0].payload.available) {
        throw (
            "Operator support blocks invalid: " +
            "levels=$($levels.count)/channels=$(@($levels.units[0].payload.rmsDbfs).Count) " +
            "effort=$($effort.count)/category=$($effort.units[0].payload.category) " +
            "storage=$($storage.count)/available=$($storage.units[0].payload.available)")
    }
    $selectedStream = Start-Process -FilePath "curl.exe" -ArgumentList @(
        "--silent",
        "--max-time", "1",
        "$base/data-blocks/block%3Asource%3Aaudio/stream?history=1&channels=1&cadenceMs=0"
    ) -RedirectStandardOutput $selectedStreamFile -RedirectStandardError $selectedStreamErrorFile -PassThru -WindowStyle Hidden
    $selectedStream.WaitForExit()
    $selectedLines = @(
        [System.IO.File]::ReadAllLines($selectedStreamFile) |
            Where-Object { $_.Trim() })
    $selected = $selectedLines[0] | ConvertFrom-Json
    if ($selected.payload.channelCount -ne 1 -or
        @($selected.payload.sourceChannels).Count -ne 1 -or
        $selected.payload.sourceChannels[0] -ne 1 -or
        @($selected.payload.interleavedPcm).Count -ne 64) {
        throw "Selected NDJSON stream did not trim the raw-audio payload to channel 1"
    }
    & $ingestExe `
        --engine $base `
        --module source `
        --source "sine=frequency=1000:duration=0.005333333333" `
        --ffmpeg-input-option "-f" `
        --ffmpeg-input-option "lavfi" `
        --sample-rate 48000 `
        --channels 2 `
        --chunk-frames 256 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg ingest bridge did not post to the acquisition module"
    }
    $sourceAfterBridge = Invoke-RestMethod -Method Get -Uri "$base/data-blocks/block%3Asource%3Aaudio/history?limit=2"
    if ($sourceAfterBridge.stats.published -lt 2) {
        throw "Acquisition module did not receive the FFmpeg bridge chunk"
    }

    Write-Host "Module runtime smoke passed: blocks=$($blocks.count) lowFftFrames=$($lines.Count) levels=$($levels.count) effort=$($effort.count) selectedChannelSamples=$(@($selected.payload.interleavedPcm).Count) bridgeChunks=$($sourceAfterBridge.stats.published) audioBytes=$audioLength"
}
finally {
    if ($stream -and -not $stream.HasExited) {
        Stop-Process -Id $stream.Id -Force -ErrorAction SilentlyContinue
    }
    if ($service -and -not $service.HasExited) {
        Stop-Process -Id $service.Id -Force -ErrorAction SilentlyContinue
    }
    if ($audioStream -and -not $audioStream.HasExited) {
        Stop-Process -Id $audioStream.Id -Force -ErrorAction SilentlyContinue
    }
    if ($framedAudioStream -and -not $framedAudioStream.HasExited) {
        Stop-Process -Id $framedAudioStream.Id -Force -ErrorAction SilentlyContinue
    }
    if ($oldGraphFile) {
        $env:PAMGUARD_MODULE_GRAPH_FILE = $oldGraphFile
    }
    else {
        Remove-Item Env:\PAMGUARD_MODULE_GRAPH_FILE -ErrorAction SilentlyContinue
    }
    if ($oldLegacyModelCompat) {
        $env:PAMGUARD_LEGACY_MODEL_COMPAT =
            $oldLegacyModelCompat
    }
    else {
        Remove-Item Env:\PAMGUARD_LEGACY_MODEL_COMPAT `
            -ErrorAction SilentlyContinue
    }
    $resolvedRoot = [System.IO.Path]::GetFullPath($root)
    $resolvedTemp = [System.IO.Path]::GetFullPath($tempRoot)
    if ($resolvedRoot.StartsWith($resolvedTemp, [System.StringComparison]::OrdinalIgnoreCase) -and
        $resolvedRoot -ne $resolvedTemp) {
        Remove-Item -LiteralPath $resolvedRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
