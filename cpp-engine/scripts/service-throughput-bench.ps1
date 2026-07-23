param(
    [int]$Port = 18091,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [int]$Sessions = 25,
    [int]$SecondsOfAudio = 30,
    [int]$SampleRateHz = 48000,
    [int]$Channels = 2,
    [double]$ChunkSeconds = 1.0,
    [switch]$Detectors,
    [switch]$Archive,   # enable result + audio archiving during the run
    [switch]$Monitors,  # enable noiseBand + ltsa + ishmael + sgramCorr monitoring load
    [switch]$SoakMinutes0  # placeholder switch retained for compatibility
)

<#
Throughput benchmark for the engine service: N concurrent sessions each fed
SecondsOfAudio of synthetic audio in ChunkSeconds chunks as fast as the
service will take them, measuring aggregate audio-seconds processed per wall
second (the realtime factor) and per-chunk latency percentiles.

The question it answers is the original acceptance criterion's: can the
service sustain the target session count at realtime? A realtime factor >= 1.0
means yes for this machine, mix, and config; the factor says by how much.

-Detectors enables click detection + localisation and the whistle chain, which
is the expensive, honest configuration; without it the benchmark measures
little more than HTTP and FFT.

This is a measurement tool, not a CI gate: numbers belong in a dated doc, not
in an assertion that fails on a slow laptop.
#>

$ErrorActionPreference = "Stop"

$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
if (-not (Test-Path $serviceExe)) {
    $serviceExe = Join-Path $BuildDir "Release\pamguard_engine_service.exe"
}
if (-not (Test-Path $serviceExe)) {
    throw "Service executable not found under $BuildDir"
}

$oldMaxSessions = $env:PAMGUARD_MAX_SESSIONS
$oldSessionDir = $env:PAMGUARD_SESSION_CONFIG_DIR
$oldArchiveDir = $env:PAMGUARD_RESULT_ARCHIVE_DIR
$oldAudioArchiveDir = $env:PAMGUARD_AUDIO_ARCHIVE_DIR
$archiveRoot = $null
$process = $null
try {
    Remove-Item Env:\PAMGUARD_SESSION_CONFIG_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\PAMGUARD_RESULT_ARCHIVE_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\PAMGUARD_AUDIO_ARCHIVE_DIR -ErrorAction SilentlyContinue
    Remove-Item Env:\PAMGUARD_API_KEY -ErrorAction SilentlyContinue
    Remove-Item Env:\PAMGUARD_API_KEY_FILE -ErrorAction SilentlyContinue
    Remove-Item Env:\PAMGUARD_REQUIRE_SESSION_METADATA -ErrorAction SilentlyContinue
    $env:PAMGUARD_MAX_SESSIONS = "$($Sessions + 2)"
    if ($Archive) {
        $archiveRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("pamguard-bench-archive-" + [System.Guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Force -Path (Join-Path $archiveRoot "results") | Out-Null
        New-Item -ItemType Directory -Force -Path (Join-Path $archiveRoot "audio") | Out-Null
        $env:PAMGUARD_RESULT_ARCHIVE_DIR = Join-Path $archiveRoot "results"
        $env:PAMGUARD_AUDIO_ARCHIVE_DIR = Join-Path $archiveRoot "audio"
    }

    $process = Start-Process -FilePath $serviceExe -ArgumentList "$Port" -PassThru -WindowStyle Hidden
    $base = "http://127.0.0.1:$Port"

    $healthy = $false
    for ($i = 0; $i -lt 100; $i++) {
        try {
            $health = Invoke-RestMethod -Method Get -Uri "$base/health"
            if ($health.ok) { $healthy = $true; break }
        }
        catch { Start-Sleep -Milliseconds 100 }
    }
    if (-not $healthy) { throw "Service did not become healthy" }

    # --- sessions ---
    for ($s = 0; $s -lt $Sessions; $s++) {
        $config = @{
            sessionId = "bench-$s"
            sourceId = "bench-src-$s"
            sampleRateHz = $SampleRateHz
            channelCount = $Channels
            fft = @{ length = 512; hop = 256; windowType = "Hann"; channels = @(0..($Channels - 1)) }
        }
        if ($Detectors) {
            $config.array = @{
                id = "bench-array"
                speedOfSoundMps = 1500.0
                hydrophones = @(0..($Channels - 1) | ForEach-Object { @{ channel = $_; xM = [double]$_; yM = 0.0; zM = 0.0 } })
            }
            $config.click = @{
                enabled = $true; localisation = $true
                thresholdDb = 10.0; shortFilter = 0.1; longFilter = 0.00001
                preSample = 10; postSample = 12; minSep = 8; maxLength = 128; minTriggerChannels = 1
                train = @{ enabled = $true }
            }
            $config.whistle = @{
                enabled = $true; regionEnabled = $true
                noise = @{ medianFilter = $true; medianFilterLength = 61; threshold = $true; thresholdDb = 8.0; finalOutput = 2 }
            }
        }
        if ($Monitors) {
            $config.noiseBand = @{ enabled = $true; bandType = "thirdOctave"; minFrequencyHz = 100.0; maxFrequencyHz = 0.0; outputIntervalSeconds = 5.0 }
            $config.ltsa = @{ enabled = $true; intervalSeconds = 10 }
            $config.ishmael = @{ enabled = $true; f0 = 500.0; f1 = 8000.0; thresh = 2.0; minTimeSeconds = 0.05; refractoryTimeSeconds = 0.2 }
            $config.sgramCorr = @{ enabled = $true; segments = @(,@(0.0, 4000.0, 0.2, 8000.0)); spread = 500.0; thresh = 0.5; minTimeSeconds = 0.05; refractoryTimeSeconds = 0.2 }
        }
        $body = $config | ConvertTo-Json -Depth 10
        $created = Invoke-RestMethod -Method Post -Uri "$base/sessions" -ContentType "application/json" -Body $body
        if (-not $created.created) { throw "Session bench-$s was not created" }
    }

    # --- synthetic chunk: broadband noise + a tone + periodic transients ---
    $chunkFrames = [int]($SampleRateHz * $ChunkSeconds)
    $bytes = New-Object byte[] ($chunkFrames * $Channels * 4)
    $rng = New-Object System.Random(12345)
    for ($f = 0; $f -lt $chunkFrames; $f++) {
        for ($c = 0; $c -lt $Channels; $c++) {
            $v = 0.02 * ($rng.NextDouble() - 0.5) + 0.05 * [math]::Sin(2 * [math]::PI * 6000.0 * $f / $SampleRateHz)
            if (($f % 4800) -lt 6) { $v += 0.8 }
            [BitConverter]::GetBytes([float]$v).CopyTo($bytes, ($f * $Channels + $c) * 4)
        }
    }

    $chunksPerSession = [int][math]::Ceiling($SecondsOfAudio / $ChunkSeconds)
    $totalChunks = $chunksPerSession * $Sessions
    $latencies = New-Object System.Collections.Generic.List[double]

    Add-Type -AssemblyName System.Net.Http
    $client = New-Object System.Net.Http.HttpClient
    $client.Timeout = [TimeSpan]::FromMinutes(5)

    Write-Host "Streaming $chunksPerSession x $($ChunkSeconds)s chunks into $Sessions sessions (detectors: $([bool]$Detectors))..."
    $wall = [System.Diagnostics.Stopwatch]::StartNew()
    for ($k = 0; $k -lt $chunksPerSession; $k++) {
        $startSample = [int64]$k * $chunkFrames
        for ($s = 0; $s -lt $Sessions; $s++) {
            $content = New-Object System.Net.Http.ByteArrayContent(,$bytes)
            $sw = [System.Diagnostics.Stopwatch]::StartNew()
            $response = $client.PostAsync("$base/sessions/bench-$s/pcm-f32le?startSample=$startSample", $content).Result
            $sw.Stop()
            if (-not $response.IsSuccessStatusCode) {
                throw "Chunk POST failed for bench-$s at ${startSample}: $($response.StatusCode)"
            }
            $null = $response.Content.ReadAsStringAsync().Result
            $response.Dispose()
            $latencies.Add($sw.Elapsed.TotalMilliseconds)
        }
    }
    $wall.Stop()

    $audioSeconds = $Sessions * $chunksPerSession * $ChunkSeconds
    $wallSeconds = $wall.Elapsed.TotalSeconds
    $realtimeFactor = $audioSeconds / $wallSeconds
    $sorted = $latencies | Sort-Object
    $p50 = $sorted[[int]($sorted.Count * 0.50)]
    $p95 = $sorted[[int]([math]::Min($sorted.Count * 0.95, $sorted.Count - 1))]
    $p99 = $sorted[[int]([math]::Min($sorted.Count * 0.99, $sorted.Count - 1))]
    $pcmMb = $totalChunks * $bytes.Length / 1e6

    Write-Host ""
    Write-Host ("Sessions:            {0}" -f $Sessions)
    Write-Host ("Audio per session:   {0}s in {1}s chunks ({2} chunks)" -f $SecondsOfAudio, $ChunkSeconds, $chunksPerSession)
    Write-Host ("Total audio:         {0}s across {1} chunk POSTs ({2:N1} MB PCM)" -f $audioSeconds, $totalChunks, $pcmMb)
    Write-Host ("Wall time:           {0:N2}s" -f $wallSeconds)
    Write-Host ("Realtime factor:     {0:N2}x  (>= 1.00 sustains {1} live sessions on this machine)" -f $realtimeFactor, $Sessions)
    Write-Host ("Chunk latency ms:    p50={0:N1}  p95={1:N1}  p99={2:N1}  max={3:N1}" -f $p50, $p95, $p99, ($sorted[-1]))
    if ($Archive) {
        $resultBytes = (Get-ChildItem -Recurse -File (Join-Path $archiveRoot "results") | Measure-Object -Sum Length).Sum
        $audioBytes = (Get-ChildItem -Recurse -File (Join-Path $archiveRoot "audio") | Measure-Object -Sum Length).Sum
        Write-Host ("Archive written:     results {0:N1} MB, audio {1:N1} MB" -f ($resultBytes / 1e6), ($audioBytes / 1e6))
    }

    for ($s = 0; $s -lt $Sessions; $s++) {
        $null = Invoke-RestMethod -Method Delete -Uri "$base/sessions/bench-$s"
    }
    if ($realtimeFactor -lt 1.0) {
        Write-Warning "Realtime factor below 1.0: this configuration does NOT sustain $Sessions live sessions here."
        exit 2
    }
    exit 0
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    if ($archiveRoot -and (Test-Path $archiveRoot)) {
        Remove-Item -Recurse -Force $archiveRoot -ErrorAction SilentlyContinue
    }
    $env:PAMGUARD_MAX_SESSIONS = $oldMaxSessions
    $env:PAMGUARD_SESSION_CONFIG_DIR = $oldSessionDir
    $env:PAMGUARD_RESULT_ARCHIVE_DIR = $oldArchiveDir
    $env:PAMGUARD_AUDIO_ARCHIVE_DIR = $oldAudioArchiveDir
}
