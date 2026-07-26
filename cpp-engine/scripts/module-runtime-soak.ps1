param(
    [int]$Port = 18194,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [int]$DurationSeconds = 5,
    [int]$FramesPerChunk = 2048
)

$ErrorActionPreference = "Stop"
if ($DurationSeconds -lt 1) {
    throw "DurationSeconds must be at least 1"
}
if ($FramesPerChunk -lt 256) {
    throw "FramesPerChunk must be at least 256"
}

$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
if (-not (Test-Path $serviceExe)) {
    throw "Service executable not found: $serviceExe"
}

$tempBase = [System.IO.Path]::GetTempPath()
$root = Join-Path $tempBase (
    "pamguard-module-soak-" +
    [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $root | Out-Null
$graphFile = Join-Path $root "graph.json"
$pcmFile = Join-Path $root "chunk.f32le"
$slowFftFile = Join-Path $root "slow-fft.ndjson"
$slowTriggerFile = Join-Path $root "slow-trigger.ndjson"
$slowAudioFile = Join-Path $root "slow-audio.f32le"
$oldGraphFile = $env:PAMGUARD_MODULE_GRAPH_FILE
$oldLegacyModelCompat = $env:PAMGUARD_LEGACY_MODEL_COMPAT
$service = $null
$clients = @()

try {
    $bytes = New-Object byte[] ($FramesPerChunk * 2 * 4)
    for ($frame = 0; $frame -lt $FramesPerChunk; $frame++) {
        for ($channel = 0; $channel -lt 2; $channel++) {
            $value = [single](
                0.1 * [Math]::Sin(
                    $frame * 0.07 + $channel * 0.31))
            [BitConverter]::GetBytes($value).CopyTo(
                $bytes,
                (($frame * 2 + $channel) * 4))
        }
    }
    [System.IO.File]::WriteAllBytes($pcmFile, $bytes)

    $env:PAMGUARD_MODULE_GRAPH_FILE = $graphFile
    $env:PAMGUARD_LEGACY_MODEL_COMPAT = "1"
    $service = Start-Process `
        -FilePath $serviceExe `
        -ArgumentList "$Port" `
        -PassThru `
        -WindowStyle Hidden
    $base = "http://127.0.0.1:$Port"
    $healthy = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try {
            if ((Invoke-RestMethod "$base/health").ok) {
                $healthy = $true
                break
            }
        }
        catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $healthy) {
        throw "Soak service did not become healthy"
    }

    $graph = @{
        expectedRevision = 0
        schemaVersion = 1
        revision = 0
        modules = @(
            @{ id = "source"; typeId = "pamguard.acquisition"; name = "Input"; enabled = $true; settings = @{ sourceId = "soak"; sampleRateHz = 48000; channelCount = 2; subtractDC = $false; dcTimeConstantSeconds = 1 } },
            @{ id = "fft-a"; typeId = "pamguard.fft"; name = "FFT A"; enabled = $true; settings = @{ fftLength = 256; fftHop = 64; windowType = "Blackman-Harris"; channels = @(0, 1); clickRemoval = $true; clickThreshold = 5.0; clickPower = 6 } },
            @{ id = "decimator"; typeId = "pamguard.decimator"; name = "Decimator"; enabled = $true; settings = @{ outputSampleRateHz = 12000; filter = @{ type = "butterworth"; band = "lowPass"; order = 6; lowPassFreqHz = 6000; highPassFreqHz = 2000; passBandRippleDb = 2; stopBandRippleDb = 2; chebyGamma = 3; arbitraryFrequenciesHz = @(); arbitraryGainsDb = @() }; interpolation = 0; channelBitmap = 4294967295 } },
            @{ id = "fft-b"; typeId = "pamguard.fft"; name = "FFT B"; enabled = $true; settings = @{ fftLength = 128; fftHop = 32; windowType = "Hann"; channels = @(0, 1); clickRemoval = $false; clickThreshold = 5.0; clickPower = 6 } },
            @{ id = "noise"; typeId = "pamguard.spectrogram-noise"; name = "Noise reduction"; enabled = $true; settings = @{ medianFilter = $true; medianFilterLength = 5; averageSubtraction = $true; updateConstant = 0.02; kernelSmoothing = $false; threshold = $false; thresholdDb = 8.0; finalOutput = 2 } },
            @{ id = "clicks"; typeId = "pamguard.click-detector"; name = "Click detector"; enabled = $true; settings = @{ channelBitmap = 3; triggerBitmap = 3; minTriggerChannels = 1; thresholdDb = 10.0; longFilter = 0.00001; longFilter2 = 0.000001; shortFilter = 0.1; preSample = 40; postSample = 40; minSep = 100; maxLength = 1024; sampleNoise = $true; noiseSampleIntervalSeconds = 5.0; storeBackground = $true; backgroundIntervalMilliseconds = 5000; publishTriggerFunction = $true; preFilter = @{ type = "butterworth"; band = "highPass"; order = 4; lowPassFreqHz = 20000; highPassFreqHz = 500; passBandRippleDb = 2.0 }; triggerFilter = @{ type = "butterworth"; band = "highPass"; order = 2; lowPassFreqHz = 20000; highPassFreqHz = 2000; passBandRippleDb = 2.0 } } },
            @{ id = "levels"; typeId = "pamguard.level-meter"; name = "Levels"; enabled = $true; settings = @{ intervalSeconds = 0.1; channelBitmap = 3 } }
        )
        connections = @(
            @{ id = "c1"; source = @{ moduleId = "source"; portId = "audio" }; target = @{ moduleId = "fft-a"; portId = "input" } },
            @{ id = "c2"; source = @{ moduleId = "source"; portId = "audio" }; target = @{ moduleId = "decimator"; portId = "input" } },
            @{ id = "c3"; source = @{ moduleId = "decimator"; portId = "output" }; target = @{ moduleId = "fft-b"; portId = "input" } },
            @{ id = "c4"; source = @{ moduleId = "fft-b"; portId = "fft" }; target = @{ moduleId = "noise"; portId = "input" } },
            @{ id = "c5"; source = @{ moduleId = "source"; portId = "audio" }; target = @{ moduleId = "clicks"; portId = "input" } },
            @{ id = "c6"; source = @{ moduleId = "source"; portId = "audio" }; target = @{ moduleId = "levels"; portId = "input" } }
        )
    } | ConvertTo-Json -Depth 12 -Compress
    $applied = Invoke-RestMethod `
        -Method Put `
        -Uri "$base/module-graph" `
        -ContentType "application/json" `
        -Body $graph
    if (-not $applied.applied) {
        throw "Soak graph was not applied"
    }
    $started = Invoke-RestMethod `
        -Method Post `
        -Uri "$base/module-runtime/control" `
        -ContentType "application/json" `
        -Body '{"action":"start"}'
    if (-not $started.running -or
        $started.graphRevision -ne $applied.revision) {
        throw "Soak module runtime did not start at the applied graph revision"
    }

    $clients += Start-Process -FilePath "curl.exe" -ArgumentList @(
        "--silent",
        "--max-time", ($DurationSeconds + 3),
        "--output", "NUL",
        "$base/data-blocks/block%3Afft-a%3Afft/stream?channels=0&cadenceMs=0"
    ) -PassThru -WindowStyle Hidden
    $clients += Start-Process -FilePath "curl.exe" -ArgumentList @(
        "--silent",
        "--max-time", ($DurationSeconds + 3),
        "--limit-rate", "1k",
        "--output", $slowFftFile,
        "$base/data-blocks/block%3Anoise%3Aoutput/stream?channels=1&cadenceMs=0"
    ) -PassThru -WindowStyle Hidden
    $clients += Start-Process -FilePath "curl.exe" -ArgumentList @(
        "--silent",
        "--max-time", ($DurationSeconds + 3),
        "--limit-rate", "4k",
        "--output", $slowAudioFile,
        "$base/data-blocks/block%3Asource%3Aaudio/audio-f32le?channels=0"
    ) -PassThru -WindowStyle Hidden
    $clients += Start-Process -FilePath "curl.exe" -ArgumentList @(
        "--silent",
        "--max-time", ($DurationSeconds + 3),
        "--limit-rate", "2k",
        "--output", $slowTriggerFile,
        "$base/data-blocks/block%3Aclicks%3Atrigger/stream?channels=0&cadenceMs=0"
    ) -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 200

    $service.Refresh()
    $baselineWorkingSet = $service.WorkingSet64
    $maximumWorkingSet = $baselineWorkingSet
    $deadline = [DateTime]::UtcNow.AddSeconds($DurationSeconds)
    [uint64]$startSample = 0
    $chunks = 0
    $maximumIngestMs = 0.0
    while ([DateTime]::UtcNow -lt $deadline) {
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $accepted = Invoke-RestMethod `
            -Method Post `
            -Uri "$base/module-runtime/acquisitions/source/pcm-f32le?startSample=$startSample" `
            -ContentType "application/octet-stream" `
            -InFile $pcmFile
        $watch.Stop()
        if (-not $accepted.accepted -or
            $accepted.inputFrames -ne $FramesPerChunk) {
            throw "Soak ingest rejected a PCM chunk"
        }
        $maximumIngestMs = [Math]::Max(
            $maximumIngestMs,
            $watch.Elapsed.TotalMilliseconds)
        $startSample += $FramesPerChunk
        $chunks++
        $service.Refresh()
        $maximumWorkingSet = [Math]::Max(
            $maximumWorkingSet,
            $service.WorkingSet64)
    }

    foreach ($client in $clients) {
        if (-not $client.HasExited) {
            Stop-Process -Id $client.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Milliseconds 200

    $runtime = Invoke-RestMethod "$base/module-runtime/status"
    $blocks = Invoke-RestMethod "$base/data-blocks"
    $badBlock = @($blocks.dataBlocks) | Where-Object {
        $_.stats.observerErrors -ne 0 -or
        $_.stats.historySize -gt $_.historyCapacity -or
        $_.stats.queuedUnits -gt $_.stats.maximumQueuedUnits
    } | Select-Object -First 1
    $workingSetGrowth = $maximumWorkingSet - $baselineWorkingSet
    if ($chunks -lt 2 -or
        -not $runtime.running -or
        $runtime.count -ne 7 -or
        $badBlock -or
        $workingSetGrowth -gt 268435456 -or
        $maximumIngestMs -gt 5000) {
        throw (
            "Soak invariants failed: chunks=$chunks running=$($runtime.running) " +
            "modules=$($runtime.count) badBlock=$($badBlock.id) " +
            "growthBytes=$workingSetGrowth maxIngestMs=$maximumIngestMs")
    }
    Write-Host (
        "Module runtime soak passed: duration=${DurationSeconds}s " +
        "chunks=$chunks frames=$startSample clients=$($clients.Count) " +
        "maxIngestMs=$([Math]::Round($maximumIngestMs, 1)) " +
        "workingSetGrowthMiB=$([Math]::Round($workingSetGrowth / 1MB, 1))")
}
finally {
    foreach ($client in $clients) {
        if ($client -and -not $client.HasExited) {
            Stop-Process -Id $client.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($service -and -not $service.HasExited) {
        Stop-Process -Id $service.Id -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $oldGraphFile) {
        $env:PAMGUARD_MODULE_GRAPH_FILE = $oldGraphFile
    }
    else {
        Remove-Item Env:\PAMGUARD_MODULE_GRAPH_FILE `
            -ErrorAction SilentlyContinue
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
    $resolvedBase = [System.IO.Path]::GetFullPath($tempBase)
    if ($resolvedRoot.StartsWith(
            $resolvedBase,
            [StringComparison]::OrdinalIgnoreCase) -and
        $resolvedRoot -ne $resolvedBase) {
        Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
    }
}
