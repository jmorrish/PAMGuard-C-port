param(
    [int]$Port = 18198,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build")
)

$ErrorActionPreference = "Stop"
$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
$deadChildExe = Join-Path $env:WINDIR "System32\where.exe"
if (-not (Test-Path $serviceExe)) {
    throw "Service executable not found: $serviceExe"
}
if (-not (Test-Path $deadChildExe)) {
    throw "Short-lived capture fixture executable not found: $deadChildExe"
}

$tempBase = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::GetTempPath())
$root = Join-Path $tempBase (
    "pamguard-capture-http-" +
    [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $root | Out-Null
$resolvedRoot = [System.IO.Path]::GetFullPath($root)
$graphFile = Join-Path $resolvedRoot "graph.json"

$oldGraphFile = $env:PAMGUARD_MODULE_GRAPH_FILE
$oldLegacyModelCompat = $env:PAMGUARD_LEGACY_MODEL_COMPAT
$oldCaptureEnabled = $env:PAMGUARD_CAPTURE_ENABLED
$oldIngestExe = $env:PAMGUARD_INGEST_EXE
$oldApiKey = $env:PAMGUARD_API_KEY
$oldApiKeyFile = $env:PAMGUARD_API_KEY_FILE
$service = $null

function Get-HttpFailureStatus {
    param([scriptblock]$Request)
    try {
        & $Request
        return 200
    }
    catch {
        if ($null -eq $_.Exception.Response) {
            throw
        }
        return [int]$_.Exception.Response.StatusCode
    }
}

try {
    $env:PAMGUARD_MODULE_GRAPH_FILE = $graphFile
    $env:PAMGUARD_LEGACY_MODEL_COMPAT = "1"
    $env:PAMGUARD_CAPTURE_ENABLED = "1"
    $env:PAMGUARD_INGEST_EXE = $deadChildExe
    $env:PAMGUARD_API_KEY = $null
    $env:PAMGUARD_API_KEY_FILE = $null

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
        throw "Capture HTTP smoke service did not become healthy"
    }

    $cold = Invoke-RestMethod "$base/capture/status"
    if (-not $cold.captureEnabled -or
        $cold.currentGraphRevision -ne 0 -or
        @($cold.captures).Count -ne 0) {
        throw "Cold capture status did not expose an empty revision-zero registry"
    }

    $untrustedStatus = Get-HttpFailureStatus {
        Invoke-WebRequest `
            -Method Post `
            -Uri "$base/capture/start" `
            -ContentType "application/json" `
            -Body '{"moduleId":"source","url":"file:///tmp/audio.wav"}' `
            -UseBasicParsing | Out-Null
    }
    if ($untrustedStatus -ne 400) {
        throw "Non-http(s) capture URL returned HTTP $untrustedStatus instead of 400"
    }

    $graph = @{
        expectedRevision = 0
        schemaVersion = 1
        revision = 0
        modules = @(
            @{
                id = "source"
                typeId = "pamguard.acquisition"
                name = "Input"
                enabled = $true
                settings = @{
                    sourceId = "capture-smoke"
                    sampleRateHz = 48000
                    channelCount = 2
                    subtractDC = $false
                    dcTimeConstantSeconds = 1
                }
            }
        )
        connections = @()
    } | ConvertTo-Json -Depth 8 -Compress
    $applied = Invoke-RestMethod `
        -Method Put `
        -Uri "$base/module-graph" `
        -ContentType "application/json" `
        -Body $graph
    if (-not $applied.applied -or $applied.revision -ne 1) {
        throw "Capture smoke acquisition graph was not applied"
    }
    $runtime = Invoke-RestMethod `
        -Method Post `
        -Uri "$base/module-runtime/control" `
        -ContentType "application/json" `
        -Body '{"action":"start"}'
    if (-not $runtime.running -or $runtime.graphRevision -ne 1) {
        throw "Capture smoke acquisition graph did not start"
    }

    $staleStartStatus = Get-HttpFailureStatus {
        Invoke-WebRequest `
            -Method Post `
            -Uri "$base/capture/start" `
            -ContentType "application/json" `
            -Body (
                '{"moduleId":"source",' +
                '"expectedGraphRevision":0,' +
                '"url":"https://fixture.invalid/live"}'
            ) `
            -UseBasicParsing | Out-Null
    }
    if ($staleStartStatus -ne 409) {
        throw (
            "Stale module capture start returned HTTP " +
            "$staleStartStatus instead of 409")
    }

    $started = Invoke-RestMethod `
        -Method Post `
        -Uri "$base/capture/start" `
        -ContentType "application/json" `
        -Body (
            '{"moduleId":"source",' +
            '"expectedGraphRevision":1,' +
            '"url":"https://fixture.invalid/live"}'
        )
    if (-not $started.started -or
        $started.moduleId -ne "source" -or
        $null -ne $started.sessionId -or
        $started.kind -ne "url" -or
        $started.graphRevision -ne 1 -or
        $started.pid -le 0) {
        throw "Capture start response lost its raw module ID or graph revision"
    }

    $reaped = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        Start-Sleep -Milliseconds 50
        $status = Invoke-RestMethod "$base/capture/status"
        if ($status.currentGraphRevision -ne 1) {
            throw "Capture status lost the current graph revision"
        }
        if (@($status.captures).Count -eq 0) {
            $reaped = $true
            break
        }
    }
    if (-not $reaped) {
        throw "Capture status retained the exited child process"
    }

    $missingStopStatus = Get-HttpFailureStatus {
        Invoke-WebRequest `
            -Method Post `
            -Uri "$base/capture/stop" `
            -ContentType "application/json" `
            -Body (
                '{"moduleId":"source",' +
                '"expectedGraphRevision":1}'
            ) `
            -UseBasicParsing | Out-Null
    }
    if ($missingStopStatus -ne 404) {
        throw "Reaped capture remained registered for stop"
    }

    Write-Output "capture service HTTP smoke passed"
}
finally {
    if ($null -ne $service -and -not $service.HasExited) {
        Stop-Process `
            -Id $service.Id `
            -Force `
            -ErrorAction SilentlyContinue
        $service.WaitForExit()
    }
    $env:PAMGUARD_MODULE_GRAPH_FILE = $oldGraphFile
    $env:PAMGUARD_LEGACY_MODEL_COMPAT =
        $oldLegacyModelCompat
    $env:PAMGUARD_CAPTURE_ENABLED = $oldCaptureEnabled
    $env:PAMGUARD_INGEST_EXE = $oldIngestExe
    $env:PAMGUARD_API_KEY = $oldApiKey
    $env:PAMGUARD_API_KEY_FILE = $oldApiKeyFile

    if ($resolvedRoot.StartsWith(
            $tempBase,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        $resolvedRoot -ne $tempBase) {
        Remove-Item `
            -LiteralPath $resolvedRoot `
            -Recurse `
            -Force `
            -ErrorAction SilentlyContinue
    }
}
