param(
    [int]$Port = 18082,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [int]$Sessions = 8,
    [int]$Chunks = 2,
    [string]$ApiKey = ""
)

$ErrorActionPreference = "Stop"

$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
if (-not (Test-Path $serviceExe)) {
    throw "Service executable not found: $serviceExe"
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ("pamguard-service-load-" + [System.Guid]::NewGuid().ToString("N"))
$sessionDir = Join-Path $root "sessions"
$archiveDir = Join-Path $root "archive"
New-Item -ItemType Directory -Force -Path $sessionDir, $archiveDir | Out-Null

$oldSessionDir = $env:PAMGUARD_SESSION_CONFIG_DIR
$oldArchiveDir = $env:PAMGUARD_RESULT_ARCHIVE_DIR
$oldMaxSessions = $env:PAMGUARD_MAX_SESSIONS
$oldRequireSessionMetadata = $env:PAMGUARD_REQUIRE_SESSION_METADATA
$oldApiKey = $env:PAMGUARD_API_KEY

$process = $null
try {
    $env:PAMGUARD_SESSION_CONFIG_DIR = $sessionDir
    $env:PAMGUARD_RESULT_ARCHIVE_DIR = $archiveDir
    $env:PAMGUARD_MAX_SESSIONS = [string]($Sessions + 2)
    $env:PAMGUARD_REQUIRE_SESSION_METADATA = "1"
    if ($ApiKey) {
        $env:PAMGUARD_API_KEY = $ApiKey
    }
    else {
        Remove-Item Env:\PAMGUARD_API_KEY -ErrorAction SilentlyContinue
    }

    $process = Start-Process -FilePath $serviceExe -ArgumentList "$Port" -PassThru -WindowStyle Hidden
    $base = "http://127.0.0.1:$Port"
    $headers = @{}
    if ($ApiKey) {
        $headers["X-API-Key"] = $ApiKey
    }

    $healthy = $false
    $lastHealth = $null
    for ($i = 0; $i -lt 50; $i++) {
        try {
            $health = Invoke-RestMethod -Method Get -Uri "$base/health"
            $lastHealth = $health
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
        throw "Service did not become healthy"
    }
    if (-not $lastHealth.sessionMetadataRequired) {
        throw "Service load smoke did not enable session metadata enforcement"
    }

    function New-SessionBody([string]$SessionId) {
        return @{
            sessionId = $SessionId
            sourceId = $SessionId
            ownerId = "load-owner"
            tenantId = "load-tenant"
            sampleRateHz = 48000
            channelCount = 2
            fft = @{
                length = 128
                hop = 64
                windowType = "Hann"
                channels = @(0, 1)
            }
            array = @{
                id = "$SessionId-array"
                speedOfSoundMps = 1500.0
                hydrophones = @(
                    @{ channel = 0; xM = 0.0; yM = 0.0; zM = 0.0; sensitivityDb = 0.0 },
                    @{ channel = 1; xM = 1.0; yM = 0.0; zM = 0.0; sensitivityDb = 0.0 }
                )
            }
            click = @{
                enabled = $false
            }
            whistle = @{
                enabled = $false
                regionEnabled = $false
            }
        } | ConvertTo-Json -Depth 10
    }

    function New-PcmBlock([int]$Frames, [int]$Channels, [double]$Phase) {
        $bytes = New-Object byte[] ($Frames * $Channels * 4)
        for ($frame = 0; $frame -lt $Frames; $frame++) {
            for ($channel = 0; $channel -lt $Channels; $channel++) {
                $value = [float]([Math]::Sin($frame * 0.07 + $channel * 0.23 + $Phase) * 0.05)
                [BitConverter]::GetBytes($value).CopyTo($bytes, ($frame * $Channels + $channel) * 4)
            }
        }
        return $bytes
    }

    function Post-PcmBytes([string]$Uri, [byte[]]$Bytes) {
        $request = [System.Net.HttpWebRequest]::Create($Uri)
        $request.Method = "POST"
        $request.ContentType = "application/octet-stream"
        $request.ContentLength = $Bytes.Length
        foreach ($key in $headers.Keys) {
            $request.Headers[$key] = $headers[$key]
        }
        $stream = $request.GetRequestStream()
        try {
            $stream.Write($Bytes, 0, $Bytes.Length)
        }
        finally {
            $stream.Dispose()
        }
        try {
            $response = $request.GetResponse()
            $reader = New-Object System.IO.StreamReader($response.GetResponseStream())
            try {
                return ($reader.ReadToEnd() | ConvertFrom-Json)
            }
            finally {
                $reader.Dispose()
                $response.Dispose()
            }
        }
        catch [System.Net.WebException] {
            if ($_.Exception.Response) {
                $reader = New-Object System.IO.StreamReader($_.Exception.Response.GetResponseStream())
                $body = $reader.ReadToEnd()
                $reader.Dispose()
                throw $body
            }
            throw
        }
    }

    $sessionIds = @()
    for ($i = 0; $i -lt $Sessions; $i++) {
        $sessionId = "load-$i"
        $sessionIds += $sessionId
        $body = New-SessionBody -SessionId $sessionId
        $created = Invoke-RestMethod -Method Post -Uri "$base/sessions" -Headers $headers -ContentType "application/json" -Body $body
        if (-not $created.created) {
            throw "Session $sessionId was not created"
        }
    }

    $listed = Invoke-RestMethod -Method Get -Uri "$base/sessions" -Headers $headers
    if ($listed.count -ne $Sessions) {
        throw "Expected $Sessions listed sessions, got $($listed.count)"
    }

    $frames = 512
    foreach ($sessionId in $sessionIds) {
        for ($chunk = 0; $chunk -lt $Chunks; $chunk++) {
            $startSample = $chunk * $frames
            $pcm = New-PcmBlock -Frames $frames -Channels 2 -Phase ([double]$chunk)
            $result = Post-PcmBytes -Uri "$base/sessions/$sessionId/pcm-f32le?startSample=$startSample" -Bytes $pcm
            if ($result.nextExpectedStartSample -ne ($startSample + $frames)) {
                throw "Session $sessionId chunk $chunk nextExpectedStartSample mismatch"
            }
        }
    }

    foreach ($sessionId in $sessionIds) {
        $status = Invoke-RestMethod -Method Get -Uri "$base/sessions/$sessionId" -Headers $headers
        if ($status.runtime.chunksReceived -ne $Chunks -or $status.runtime.framesReceived -ne ($Chunks * $frames)) {
            throw "Session $sessionId runtime counters mismatch"
        }
    }

    foreach ($sessionId in $sessionIds) {
        $removed = Invoke-RestMethod -Method Delete -Uri "$base/sessions/$sessionId" -Headers $headers
        if (-not $removed.removed) {
            throw "Session $sessionId was not removed"
        }
    }

    Write-Host "Service load smoke passed: sessions=$Sessions chunksPerSession=$Chunks"
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    $env:PAMGUARD_SESSION_CONFIG_DIR = $oldSessionDir
    $env:PAMGUARD_RESULT_ARCHIVE_DIR = $oldArchiveDir
    $env:PAMGUARD_MAX_SESSIONS = $oldMaxSessions
    if ($oldRequireSessionMetadata) {
        $env:PAMGUARD_REQUIRE_SESSION_METADATA = $oldRequireSessionMetadata
    }
    else {
        Remove-Item Env:\PAMGUARD_REQUIRE_SESSION_METADATA -ErrorAction SilentlyContinue
    }
    if ($oldApiKey) {
        $env:PAMGUARD_API_KEY = $oldApiKey
    }
    else {
        Remove-Item Env:\PAMGUARD_API_KEY -ErrorAction SilentlyContinue
    }
    Remove-Item -Recurse -Force -Path $root -ErrorAction SilentlyContinue
}
