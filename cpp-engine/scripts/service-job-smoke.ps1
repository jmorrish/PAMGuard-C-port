param(
    [int]$Port = 18095,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build")
)

<#
Offline job queue smoke: writes a small WAV with click transients, starts the
service with the job queue and archive enabled, submits a job, polls it to
completion, asserts detection counters and archived records, checks replay
determinism (the same job twice produces identical archive record counts and
click totals), and exercises path-traversal rejection.
#>

$ErrorActionPreference = "Stop"

$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
if (-not (Test-Path $serviceExe)) {
    $serviceExe = Join-Path $BuildDir "Release\pamguard_engine_service.exe"
}
if (-not (Test-Path $serviceExe)) {
    throw "Service executable not found under $BuildDir"
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ("pamguard-job-smoke-" + [System.Guid]::NewGuid().ToString("N"))
$audioDir = Join-Path $root "audio"
$archiveDir = Join-Path $root "archive"
$audioArchiveDir = Join-Path $root "audio-archive"
New-Item -ItemType Directory -Force -Path $audioDir, $archiveDir, $audioArchiveDir | Out-Null

# --- write a 3-second 48 kHz mono 16-bit WAV with transients every 4800 samples ---
$sampleRate = 48000
$seconds = 3
$frames = $sampleRate * $seconds
$dataBytes = $frames * 2
$wav = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($wav)
$writer.Write([System.Text.Encoding]::ASCII.GetBytes("RIFF"))
$writer.Write([int](36 + $dataBytes))
$writer.Write([System.Text.Encoding]::ASCII.GetBytes("WAVEfmt "))
$writer.Write([int]16)
$writer.Write([int16]1)          # PCM
$writer.Write([int16]1)          # mono
$writer.Write([int]$sampleRate)
$writer.Write([int]($sampleRate * 2))
$writer.Write([int16]2)
$writer.Write([int16]16)
$writer.Write([System.Text.Encoding]::ASCII.GetBytes("data"))
$writer.Write([int]$dataBytes)
$rng = New-Object System.Random(7)
for ($f = 0; $f -lt $frames; $f++) {
    $v = 0.01 * ($rng.NextDouble() - 0.5)
    if (($f % 4800) -lt 6) {
        $sign = 1.0
        if (($f % 2) -ne 0) { $sign = -1.0 }
        $v += 0.8 * $sign
    }
    $writer.Write([int16][math]::Round($v * 32000))
}
$writer.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $audioDir "smoke.wav"), $wav.ToArray())

$oldJobDir = $env:PAMGUARD_JOB_AUDIO_DIR
$oldArchive = $env:PAMGUARD_RESULT_ARCHIVE_DIR
$process = $null
try {
    $env:PAMGUARD_JOB_AUDIO_DIR = $audioDir
    $env:PAMGUARD_RESULT_ARCHIVE_DIR = $archiveDir
    $env:PAMGUARD_AUDIO_ARCHIVE_DIR = $audioArchiveDir
    Remove-Item Env:\PAMGUARD_API_KEY -ErrorAction SilentlyContinue
    Remove-Item Env:\PAMGUARD_API_KEY_FILE -ErrorAction SilentlyContinue
    Remove-Item Env:\PAMGUARD_REQUIRE_SESSION_METADATA -ErrorAction SilentlyContinue
    Remove-Item Env:\PAMGUARD_SESSION_CONFIG_DIR -ErrorAction SilentlyContinue

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
    if (-not $health.jobQueueEnabled) { throw "Health did not report the job queue enabled" }

    $session = @{
        click = @{
            enabled = $true
            thresholdDb = 10.0; shortFilter = 0.1; longFilter = 0.00001
            preSample = 10; postSample = 12; minSep = 8; maxLength = 128; minTriggerChannels = 1
        }
        fft = @{ length = 512; hop = 256; windowType = "Hann"; channels = @(0) }
    }

    function Submit-Job([string]$JobId) {
        $body = @{ jobId = $JobId; wavFile = "smoke.wav"; session = $session } | ConvertTo-Json -Depth 10
        return Invoke-RestMethod -Method Post -Uri "$base/jobs" -ContentType "application/json" -Body $body
    }
    function Wait-Job2([string]$JobId) {
        for ($i = 0; $i -lt 200; $i++) {
            $job = Invoke-RestMethod -Method Get -Uri "$base/jobs/$JobId"
            if ($job.state -in @("completed", "failed", "cancelled")) { return $job }
            Start-Sleep -Milliseconds 100
        }
        throw "Job $JobId did not finish in time"
    }

    $created = Submit-Job "run-a"
    if ($created.state -ne "queued") { throw "Job was not queued" }
    $jobA = Wait-Job2 "run-a"
    if ($jobA.state -ne "completed") { throw "Job run-a did not complete: $($jobA.state) $($jobA.error)" }
    if ($jobA.processedFrames -ne $frames) { throw "Job did not process the whole file ($($jobA.processedFrames)/$frames)" }
    if ($jobA.clicks -lt 10) { throw "Job detected too few clicks ($($jobA.clicks)) - transients should fire the detector" }

    # Archived records are queryable through the ordinary archive endpoints
    # under the job's session id.
    $archive = Invoke-RestMethod -Method Get -Uri "$base/sessions/job-run-a/archive?limit=10"
    if ($archive.count -lt 3) { throw "Job archive should hold one record per chunk plus flush, got $($archive.count)" }
    $clickEvents = Invoke-RestMethod -Method Get -Uri "$base/sessions/job-run-a/archive/detections?type=click&limit=500"
    if ($clickEvents.count -lt 10) { throw "Job archive detections should include the clicks" }

    # Replay determinism: same file, same config, same results.
    $null = Submit-Job "run-b"
    $jobB = Wait-Job2 "run-b"
    if ($jobB.state -ne "completed") { throw "Job run-b did not complete" }
    if ($jobB.clicks -ne $jobA.clicks -or $jobB.chunks -ne $jobA.chunks) {
        throw "Replay was not deterministic: run-a clicks=$($jobA.clicks) chunks=$($jobA.chunks), run-b clicks=$($jobB.clicks) chunks=$($jobB.chunks)"
    }

    # Path traversal must be rejected at submission time.
    $escaped = $false
    try {
        $body = @{ jobId = "evil"; wavFile = "..\..\outside.wav"; session = $session } | ConvertTo-Json -Depth 10
        $null = Invoke-RestMethod -Method Post -Uri "$base/jobs" -ContentType "application/json" -Body $body
    }
    catch { $escaped = $true }
    if (-not $escaped) { throw "Path traversal in wavFile was not rejected" }

    # Listing and cleanup.
    $list = Invoke-RestMethod -Method Get -Uri "$base/jobs"
    if ($list.count -lt 2) { throw "Job list did not include both runs" }
    $removed = Invoke-RestMethod -Method Delete -Uri "$base/jobs/run-b"
    if (-not $removed.removed) { throw "Completed job delete did not remove the record" }

    # --- audio archive + replay: post live PCM, then replay it as a job ---
    if (-not $health.audioArchiveEnabled) { throw "Health did not report the audio archive enabled" }
    $liveSession = @{
        sessionId = "live-rec"
        sampleRateHz = $sampleRate
        channelCount = 1
        fft = @{ length = 512; hop = 256; windowType = "Hann"; channels = @(0) }
        click = $session.click
    } | ConvertTo-Json -Depth 10
    $null = Invoke-RestMethod -Method Post -Uri "$base/sessions" -ContentType "application/json" -Body $liveSession

    # Feed the same audio as the WAV, as raw f32le, in two chunks with a
    # deliberate one-chunk gap so the index's continuity flag has something
    # to report.
    $chunkFrames = $sampleRate
    $f32 = New-Object byte[] ($chunkFrames * 4)
    $rng2 = New-Object System.Random(7)
    $liveClicks = 0
    function Post-Chunk([long]$StartSample) {
        for ($f = 0; $f -lt $chunkFrames; $f++) {
            $g = $StartSample + $f
            $v = 0.01 * ($rng2.NextDouble() - 0.5)
            if (($g % 4800) -lt 6) {
                $sign = 1.0
                if (($g % 2) -ne 0) { $sign = -1.0 }
                $v += 0.8 * $sign
            }
            [BitConverter]::GetBytes([float]$v).CopyTo($f32, $f * 4)
        }
        $request = [System.Net.HttpWebRequest]::Create("$base/sessions/live-rec/pcm-f32le?startSample=$StartSample")
        $request.Method = "POST"
        $request.ContentType = "application/octet-stream"
        $request.ContentLength = $f32.Length
        $stream = $request.GetRequestStream()
        $stream.Write($f32, 0, $f32.Length)
        $stream.Dispose()
        $response = $request.GetResponse()
        $readerX = New-Object System.IO.StreamReader($response.GetResponseStream())
        $bodyX = $readerX.ReadToEnd() | ConvertFrom-Json
        $readerX.Dispose(); $response.Dispose()
        return $bodyX
    }
    $r1 = Post-Chunk 0
    $r2 = Post-Chunk (2 * $chunkFrames)   # gap: chunk at 1x skipped
    $liveClicks = @($r1.clicks).Count + @($r2.clicks).Count
    if ($liveClicks -lt 10) { throw "Live recording session detected too few clicks ($liveClicks)" }

    $audioIndex = Invoke-RestMethod -Method Get -Uri "$base/sessions/live-rec/audio/index"
    if ($audioIndex.count -ne 2 -or $audioIndex.totalFrames -ne (2 * $chunkFrames)) {
        throw "Audio index should hold the two posted chunks"
    }
    if ($audioIndex.contiguous) { throw "Audio index should report the deliberate gap as non-contiguous" }

    # Replay the archived audio through a job with the same detector config;
    # the original chunk boundaries, start samples, and gap are preserved, so
    # the click count matches the live run exactly.
    $replayBody = @{ jobId = "replay-1"; audioSession = "live-rec"; session = $session } | ConvertTo-Json -Depth 10
    $null = Invoke-RestMethod -Method Post -Uri "$base/jobs" -ContentType "application/json" -Body $replayBody
    $replay = Wait-Job2 "replay-1"
    if ($replay.state -ne "completed") { throw "Replay job did not complete: $($replay.state) $($replay.error)" }
    if ($replay.clicks -ne $liveClicks) {
        throw "Replay clicks ($($replay.clicks)) did not match the live run ($liveClicks)"
    }
    if ($replay.chunks -ne 2) { throw "Replay should preserve the original two chunk boundaries" }

    Write-Host "Job smoke passed: clicks=$($jobA.clicks) chunks=$($jobA.chunks) archiveRecords=$($archive.count) deterministic=true replayClicks=$($replay.clicks)"
    exit 0
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    $env:PAMGUARD_JOB_AUDIO_DIR = $oldJobDir
    $env:PAMGUARD_RESULT_ARCHIVE_DIR = $oldArchive
    Remove-Item Env:\PAMGUARD_AUDIO_ARCHIVE_DIR -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}
