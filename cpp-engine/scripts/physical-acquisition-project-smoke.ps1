param(
    [Parameter(Mandatory = $true)]
    [string]$DeviceName,
    [int]$Port = 18209,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [string]$FfmpegPath = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Net.Http

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

$resolvedBuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$serviceExe = Join-Path $resolvedBuildDir "pamguard_engine_service.exe"
$ingestExe = Join-Path $resolvedBuildDir "ffmpeg_stream_ingest.exe"
foreach ($requiredFile in @($serviceExe, $ingestExe)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required executable not found: $requiredFile"
    }
}

if ([string]::IsNullOrWhiteSpace($FfmpegPath)) {
    $ffmpegCommand = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if ($null -eq $ffmpegCommand) {
        throw "FFmpeg was not found on PATH; pass -FfmpegPath explicitly"
    }
    $FfmpegPath = $ffmpegCommand.Source
}
$resolvedFfmpegPath = [System.IO.Path]::GetFullPath($FfmpegPath)
if (-not (Test-Path -LiteralPath $resolvedFfmpegPath -PathType Leaf)) {
    throw "FFmpeg executable not found: $resolvedFfmpegPath"
}
if ([string]::IsNullOrWhiteSpace($DeviceName)) {
    throw "DeviceName must be the exact enumerated DirectShow audio-device name"
}

$tempBase = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::GetTempPath())
$testRoot = Join-Path $tempBase (
    "pamguard-physical-acquisition-" +
    [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$resolvedRoot = [System.IO.Path]::GetFullPath($testRoot)
$projectDir = Join-Path $resolvedRoot "projects"
New-Item -ItemType Directory -Path $projectDir | Out-Null
$stdoutPath = Join-Path $resolvedRoot "service.stdout.log"
$stderrPath = Join-Path $resolvedRoot "service.stderr.log"

$environmentNames = @(
    "PAMGUARD_PROJECT_DIR",
    "PAMGUARD_ACTIVE_PROJECT_ID",
    "PAMGUARD_LEGACY_MODEL_COMPAT",
    "PAMGUARD_MODULE_GRAPH_FILE",
    "PAMGUARD_CAPTURE_ENABLED",
    "PAMGUARD_INGEST_EXE",
    "PAMGUARD_FFMPEG_PATH",
    "PAMGUARD_API_KEY",
    "PAMGUARD_API_KEY_FILE"
)
$oldEnvironment = @{}
foreach ($name in $environmentNames) {
    $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable(
        $name,
        [EnvironmentVariableTarget]::Process)
}

$baseUrl = "http://127.0.0.1:$Port"
$client = [System.Net.Http.HttpClient]::new()
$client.Timeout = [TimeSpan]::FromSeconds(15)
$service = $null
$captureProcessId = 0
$captureStarted = $false
$runtimeStarted = $false

function Invoke-PhysicalSmokeHttp {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Method,
        [Parameter(Mandatory = $true)]
        [string]$Target,
        [hashtable]$Headers = @{},
        [string]$Body
    )

    $request = [System.Net.Http.HttpRequestMessage]::new(
        [System.Net.Http.HttpMethod]::new($Method.ToUpperInvariant()),
        [Uri]::new("$baseUrl$Target"))
    foreach ($entry in $Headers.GetEnumerator()) {
        [void]$request.Headers.TryAddWithoutValidation(
            [string]$entry.Key,
            [string]$entry.Value)
    }
    if ($PSBoundParameters.ContainsKey("Body")) {
        $request.Content = [System.Net.Http.StringContent]::new(
            $Body,
            [System.Text.Encoding]::UTF8,
            "application/json")
    }

    $response = $null
    try {
        $response = $client.SendAsync($request).GetAwaiter().GetResult()
        $raw = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        $parsed = $null
        if (-not [string]::IsNullOrWhiteSpace($raw)) {
            try {
                $parsed = $raw | ConvertFrom-Json
            }
            catch {
                $parsed = $null
            }
        }
        $etag = if ($null -ne $response.Headers.ETag) {
            $response.Headers.ETag.ToString()
        }
        else {
            $null
        }
        return [pscustomobject]@{
            Status = [int]$response.StatusCode
            Raw = $raw
            Json = $parsed
            Etag = $etag
        }
    }
    finally {
        if ($null -ne $response) {
            $response.Dispose()
        }
        $request.Dispose()
    }
}

function Wait-ProcessGone {
    param([int]$ProcessId)
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($null -eq (
                Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) {
            return $true
        }
        Start-Sleep -Milliseconds 50
    }
    return $false
}

try {
    $env:PAMGUARD_PROJECT_DIR = $projectDir
    $env:PAMGUARD_ACTIVE_PROJECT_ID = $null
    $env:PAMGUARD_LEGACY_MODEL_COMPAT = $null
    $env:PAMGUARD_MODULE_GRAPH_FILE = $null
    $env:PAMGUARD_CAPTURE_ENABLED = "1"
    $env:PAMGUARD_INGEST_EXE = $ingestExe
    $env:PAMGUARD_FFMPEG_PATH = $resolvedFfmpegPath
    $env:PAMGUARD_API_KEY = $null
    $env:PAMGUARD_API_KEY_FILE = $null

    $service = Start-Process `
        -FilePath $serviceExe `
        -ArgumentList "$Port" `
        -PassThru `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath

    $healthy = $false
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($service.HasExited) {
            throw (
                "Service exited during startup: " +
                (Get-Content -LiteralPath $stderrPath -Raw))
        }
        try {
            $health = Invoke-PhysicalSmokeHttp -Method Get -Target "/health"
            if ($health.Status -eq 200 -and $health.Json.ok) {
                $healthy = $true
                break
            }
        }
        catch {
        }
        Start-Sleep -Milliseconds 100
    }
    Assert-True $healthy "Service did not become healthy"

    $devices = Invoke-PhysicalSmokeHttp `
        -Method Get `
        -Target "/capture/devices"
    Assert-True `
        ($devices.Status -eq 200) `
        "Audio-device enumeration failed: $($devices.Raw)"
    $exactDevice = @(
        $devices.Json.devices |
            Where-Object {
                $_.type -eq "audio" -and
                $_.name -ceq $DeviceName
            }
    )
    Assert-True `
        ($exactDevice.Count -eq 1) `
        (
            "The exact audio device was not enumerated: '$DeviceName'. " +
            "Available audio devices: " +
            (@(
                $devices.Json.devices |
                    Where-Object { $_.type -eq "audio" } |
                    ForEach-Object { $_.name }
            ) -join "; "))

    $blank = Invoke-PhysicalSmokeHttp `
        -Method Get `
        -Target "/v1/projects/active"
    Assert-True `
        ($blank.Status -eq 200 -and
            -not [string]::IsNullOrWhiteSpace($blank.Etag)) `
        "Blank active project or ETag was unavailable"

    $mutationBody = @{
        schemaVersion = 1
        validateOnly = $false
        operations = @(
            @{
                op = "addControlledUnit"
                clientRef = "physicalInput"
                typeId = "pamguard.acquisition"
                name = "Physical input"
                dependencyPolicy = "reject"
            }
        )
    } | ConvertTo-Json -Depth 16 -Compress
    $added = Invoke-PhysicalSmokeHttp `
        -Method Post `
        -Target "/v1/projects/active/mutations" `
        -Headers @{ "If-Match" = $blank.Etag } `
        -Body $mutationBody
    Assert-True `
        ($added.Status -eq 200 -and
            $added.Json.active.workingRevision -eq 1) `
        "Physical Acquisition could not be added: $($added.Raw)"
    $unitId = [string]@(
        $added.Json.createdEntities |
            Where-Object { $_.clientRef -eq "physicalInput" }
    )[0].id
    Assert-True `
        (-not [string]::IsNullOrWhiteSpace($unitId)) `
        "Physical Acquisition ID was not returned"

    $bindingPath =
        "/v1/projects/active/acquisitions/$unitId/host-binding"
    $binding = Invoke-PhysicalSmokeHttp `
        -Method Put `
        -Target $bindingPath `
        -Body (@{
            expectedWorkingRevision = 1
            expectedBindingRevision = 0
            source = @{
                kind = "device"
                deviceName = $DeviceName
            }
        } | ConvertTo-Json -Depth 8 -Compress)
    Assert-True `
        ($binding.Status -eq 201 -and
            $binding.Json.hostBinding.source.deviceName -ceq $DeviceName) `
        "Exact host-device binding failed: $($binding.Raw)"

    $runtime = Invoke-PhysicalSmokeHttp `
        -Method Post `
        -Target "/module-runtime/control" `
        -Body '{"action":"start"}'
    Assert-True `
        ($runtime.Status -eq 200 -and $runtime.Json.running) `
        "Project runtime did not start: $($runtime.Raw)"
    $runtimeStarted = $true

    $inspection = Invoke-PhysicalSmokeHttp `
        -Method Get `
        -Target "/v1/projects/active/inspection"
    $output = @(
        $inspection.Json.projection.publicOutputs |
            Where-Object {
                $_.unitId -ceq $unitId -and
                $_.outputRole -eq "rawAudio"
            }
    )[0]
    Assert-True `
        ($null -ne $output -and
            -not [string]::IsNullOrWhiteSpace([string]$output.blockId)) `
        "Physical Acquisition raw-audio block was not projected"

    $capturePath =
        "/v1/projects/active/acquisitions/$unitId/capture:start"
    $capture = Invoke-PhysicalSmokeHttp `
        -Method Post `
        -Target $capturePath `
        -Body '{"expectedWorkingRevision":1}'
    Assert-True `
        ($capture.Status -eq 200 -and
            $capture.Json.started -and
            $capture.Json.processId -gt 0) `
        "Project-owned physical capture did not start: $($capture.Raw)"
    $captureProcessId = [int]$capture.Json.processId
    $captureStarted = $true

    $historyPath =
        "/data-blocks/" +
        [Uri]::EscapeDataString([string]$output.blockId) +
        "/history"
    $history = $null
    for ($attempt = 0; $attempt -lt 150; $attempt++) {
        $history = Invoke-PhysicalSmokeHttp `
            -Method Get `
            -Target $historyPath
        if ($history.Status -eq 200 -and $history.Json.count -gt 0) {
            break
        }
        $captureStatus = Invoke-PhysicalSmokeHttp `
            -Method Get `
            -Target (
                "/v1/projects/active/acquisitions/$unitId/" +
                "capture-status")
        Assert-True `
            ($captureStatus.Status -eq 200 -and
                $captureStatus.Json.running) `
            "Physical capture child exited before publishing audio"
        Start-Sleep -Milliseconds 100
    }
    Assert-True `
        ($null -ne $history -and $history.Json.count -gt 0) `
        "No physical audio reached the Acquisition raw-audio block"

    $stoppedCapture = Invoke-PhysicalSmokeHttp `
        -Method Post `
        -Target (
            "/v1/projects/active/acquisitions/$unitId/capture:stop") `
        -Body '{"expectedWorkingRevision":1}'
    Assert-True `
        ($stoppedCapture.Status -eq 200 -and
            $stoppedCapture.Json.stopped -and
            (Wait-ProcessGone -ProcessId $captureProcessId)) `
        "Physical capture did not stop and reap its ingest process"
    $captureStarted = $false

    $stoppedRuntime = Invoke-PhysicalSmokeHttp `
        -Method Post `
        -Target "/module-runtime/control" `
        -Body '{"action":"stop"}'
    Assert-True `
        ($stoppedRuntime.Status -eq 200 -and
            -not $stoppedRuntime.Json.running) `
        "Project runtime did not stop cleanly"
    $runtimeStarted = $false

    Write-Output (
        "Physical Acquisition project smoke passed: device='$DeviceName'; " +
        "block=$($output.blockId); retainedChunks=$($history.Json.count); " +
        "capture process $captureProcessId reaped")
}
finally {
    if ($null -ne $service -and -not $service.HasExited) {
        if ($captureStarted) {
            try {
                [void](Invoke-PhysicalSmokeHttp `
                    -Method Post `
                    -Target (
                        "/v1/projects/active/acquisitions/$unitId/" +
                        "capture:stop") `
                    -Body '{"expectedWorkingRevision":1}')
            }
            catch {
            }
        }
        if ($runtimeStarted) {
            try {
                [void](Invoke-PhysicalSmokeHttp `
                    -Method Post `
                    -Target "/module-runtime/control" `
                    -Body '{"action":"stop"}')
            }
            catch {
            }
        }
        Stop-Process -Id $service.Id -Force -ErrorAction SilentlyContinue
        [void]$service.WaitForExit(5000)
    }
    $client.Dispose()
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $oldEnvironment[$name],
            [EnvironmentVariableTarget]::Process)
    }
    if ($resolvedRoot.StartsWith(
            $tempBase,
            [StringComparison]::OrdinalIgnoreCase) -and
        $resolvedRoot -ne $tempBase -and
        (Test-Path -LiteralPath $resolvedRoot)) {
        Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
    }
}
