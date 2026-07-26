param(
    [int]$Port = 18205,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Net.Http

$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
if (-not (Test-Path -LiteralPath $serviceExe -PathType Leaf)) {
    throw "Service executable not found: $serviceExe"
}

$tempBase = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::GetTempPath())
$root = Join-Path $tempBase (
    "pamguard-sound-recorder-http-" +
    [System.Guid]::NewGuid().ToString("N"))
$projectDir = Join-Path $root "projects"
$recordingRoot = Join-Path $root "recordings"
New-Item -ItemType Directory -Path $projectDir | Out-Null
New-Item -ItemType Directory -Path $recordingRoot | Out-Null
$resolvedRoot = [System.IO.Path]::GetFullPath($root)
$resolvedRecordingRoot =
    [System.IO.Path]::GetFullPath($recordingRoot)
$stdoutPath = Join-Path $resolvedRoot "service.stdout.log"
$stderrPath = Join-Path $resolvedRoot "service.stderr.log"

$environmentNames = @(
    "PAMGUARD_PROJECT_DIR",
    "PAMGUARD_RECORDING_ROOT",
    "PAMGUARD_ACTIVE_PROJECT_ID",
    "PAMGUARD_LEGACY_MODEL_COMPAT",
    "PAMGUARD_MODULE_GRAPH_FILE",
    "PAMGUARD_API_KEY",
    "PAMGUARD_API_KEY_FILE"
)
$oldEnvironment = @{}
foreach ($name in $environmentNames) {
    $oldEnvironment[$name] =
        [Environment]::GetEnvironmentVariable(
            $name,
            [EnvironmentVariableTarget]::Process)
}

$base = "http://127.0.0.1:$Port"
$client = New-Object System.Net.Http.HttpClient
$client.Timeout = [TimeSpan]::FromSeconds(15)
$service = $null
$operatorSurface =
    New-Object System.Collections.Generic.List[string]

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-SmokeHttp {
    param(
        [string]$Method,
        [string]$Target,
        [hashtable]$Headers = @{},
        [string]$Body,
        [byte[]]$Bytes,
        [string]$ContentType = "application/json"
    )
    $request = New-Object System.Net.Http.HttpRequestMessage
    $request.Method =
        New-Object System.Net.Http.HttpMethod(
            $Method.ToUpperInvariant())
    $request.RequestUri = [Uri]::new("$base$Target")
    foreach ($entry in $Headers.GetEnumerator()) {
        [void]$request.Headers.TryAddWithoutValidation(
            [string]$entry.Key,
            [string]$entry.Value)
    }
    if ($null -ne $Bytes) {
        $request.Content =
            New-Object System.Net.Http.ByteArrayContent(,$Bytes)
        $request.Content.Headers.ContentType =
            [System.Net.Http.Headers.MediaTypeHeaderValue]::Parse(
                $ContentType)
    }
    elseif ($PSBoundParameters.ContainsKey("Body")) {
        $request.Content =
            New-Object System.Net.Http.StringContent(
                $Body,
                [System.Text.Encoding]::UTF8,
                $ContentType)
    }
    try {
        $response =
            $client.SendAsync($request).GetAwaiter().GetResult()
        $raw =
            $response.Content.ReadAsStringAsync().
                GetAwaiter().GetResult()
        $parsed = $null
        if (-not [string]::IsNullOrWhiteSpace($raw)) {
            try {
                $parsed = $raw | ConvertFrom-Json
            }
            catch {
                $parsed = $null
            }
        }
        $etag = $null
        if ($null -ne $response.Headers.ETag) {
            $etag = $response.Headers.ETag.ToString()
        }
        if ($Target -like
                "/v1/projects/active/sound-recorders/*") {
            $operatorSurface.Add($Target)
            $operatorSurface.Add($raw)
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

try {
    $env:PAMGUARD_PROJECT_DIR = $projectDir
    $env:PAMGUARD_RECORDING_ROOT = $recordingRoot
    $env:PAMGUARD_ACTIVE_PROJECT_ID = $null
    $env:PAMGUARD_LEGACY_MODEL_COMPAT = $null
    $env:PAMGUARD_MODULE_GRAPH_FILE = $null
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
                "Sound Recorder service exited early: " +
                (Get-Content `
                    -LiteralPath $stderrPath `
                    -Raw `
                    -ErrorAction SilentlyContinue))
        }
        try {
            $health =
                Invoke-SmokeHttp -Method Get -Target "/health"
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

    $blank =
        Invoke-SmokeHttp `
            -Method Get `
            -Target "/v1/projects/active"
    Assert-True `
        ($blank.Status -eq 200 -and
            -not [string]::IsNullOrWhiteSpace($blank.Etag)) `
        "Blank project or ETag was unavailable"

    $mutationBody = @{
        schemaVersion = 1
        validateOnly = $false
        operations = @(
            @{
                op = "addControlledUnit"
                clientRef = "input"
                typeId = "pamguard.acquisition"
                name = "Recorder Input"
                dependencyPolicy = "reject"
            },
            @{
                op = "addControlledUnit"
                clientRef = "recorder"
                typeId = "pamguard.sound-recorder"
                name = "Primary Recorder"
                dependencyPolicy = "reject"
            },
            @{
                op = "setBinding"
                unit = @{ clientRef = "recorder" }
                inputRole = "rawAudio"
                sources = @(
                    @{
                        unit = @{ clientRef = "input" }
                        outputRole = "rawAudio"
                    }
                )
            }
        )
    } | ConvertTo-Json -Depth 32 -Compress
    $added =
        Invoke-SmokeHttp `
            -Method Post `
            -Target "/v1/projects/active/mutations" `
            -Headers @{ "If-Match" = $blank.Etag } `
            -Body $mutationBody
    Assert-True `
        ($added.Status -eq 200 -and
            $added.Json.active.workingRevision -eq 1 -and
            $added.Json.active.projection.status -eq "runnable") `
        "Acquisition + Sound Recorder project was not runnable: $($added.Raw)"

    $projectId =
        [string]$added.Json.active.project.projectId
    $inputId = [string]@(
        $added.Json.createdEntities |
            Where-Object { $_.clientRef -eq "input" }
    )[0].id
    $recorderId = [string]@(
        $added.Json.createdEntities |
            Where-Object { $_.clientRef -eq "recorder" }
    )[0].id
    Assert-True `
        (-not [string]::IsNullOrWhiteSpace($projectId) -and
            -not [string]::IsNullOrWhiteSpace($inputId) -and
            -not [string]::IsNullOrWhiteSpace($recorderId)) `
        "Stable project/unit IDs were not returned"

    $statusPath =
        "/v1/projects/active/sound-recorders/$recorderId/status"
    $transportPath =
        "/v1/projects/active/sound-recorders/$recorderId/transport"
    $initial =
        Invoke-SmokeHttp -Method Get -Target $statusPath
    Assert-True `
        ($initial.Status -eq 200 -and
            $initial.Json.soundRecorderUnitId -ceq $recorderId -and
            $initial.Json.deploymentReady -and
            $initial.Json.runtimePrepared -and
            -not $initial.Json.runtimeRunning -and
            $initial.Json.transport -eq "off" -and
            $null -eq $initial.Json.currentFileName) `
        "Initial stable recorder status was incorrect: $($initial.Raw)"

    $stale =
        Invoke-SmokeHttp `
            -Method Put `
            -Target $transportPath `
            -Body '{"expectedWorkingRevision":0,"transport":"continuous"}'
    Assert-True `
        ($stale.Status -eq 409 -and
            $stale.Json.code -eq "working_revision_conflict") `
        "Stale recorder transport command was not rejected"

    $unsupported =
        Invoke-SmokeHttp `
            -Method Put `
            -Target $transportPath `
            -Body '{"expectedWorkingRevision":1,"transport":"cycle"}'
    Assert-True `
        ($unsupported.Status -eq 501 -and
            $unsupported.Json.code -eq
                "sound_recorder_transport_unsupported") `
        "Cycle transport was not reported as explicitly unsupported"

    $runtimeStarted =
        Invoke-SmokeHttp `
            -Method Post `
            -Target "/module-runtime/control" `
            -Body '{"action":"start"}'
    Assert-True `
        ($runtimeStarted.Status -eq 200 -and
            $runtimeStarted.Json.running) `
        "Project runtime did not start"

    $continuous =
        Invoke-SmokeHttp `
            -Method Put `
            -Target $transportPath `
            -Body '{"expectedWorkingRevision":1,"transport":"continuous"}'
    Assert-True `
        ($continuous.Status -eq 200 -and
            $continuous.Json.transport -eq "continuous" -and
            $continuous.Json.runtimeRunning) `
        "Continuous recorder transport did not start"

    $acquisitions =
        Invoke-SmokeHttp `
            -Method Get `
            -Target "/v1/projects/active/acquisitions"
    $input = @(
        $acquisitions.Json.acquisitions |
            Where-Object { $_.unitId -ceq $inputId }
    )[0]
    $channelCount = [int]$input.channelCount
    Assert-True `
        ($channelCount -gt 0) `
        "Acquisition channel count was unavailable"
    $frameCount = 128
    $pcm =
        New-Object byte[] ($frameCount * $channelCount * 4)
    for ($sample = 0;
         $sample -lt $frameCount * $channelCount;
         $sample++) {
        $value = [single](0.1 * (($sample % 7) - 3))
        [BitConverter]::GetBytes($value).CopyTo(
            $pcm,
            $sample * 4)
    }
    $pcmPath =
        "/v1/projects/active/acquisitions/$inputId/pcm-f32le" +
        "?expectedProjectId=$projectId" +
        "&expectedWorkingRevision=1&startSample=0&timeMs=1000"
    $accepted =
        Invoke-SmokeHttp `
            -Method Post `
            -Target $pcmPath `
            -Bytes $pcm `
            -ContentType "application/octet-stream"
    Assert-True `
        ($accepted.Status -eq 202 -and
            $accepted.Json.inputFrames -eq $frameCount) `
        "Recorder PCM was not accepted: $($accepted.Raw)"

    $off =
        Invoke-SmokeHttp `
            -Method Put `
            -Target $transportPath `
            -Body '{"expectedWorkingRevision":1,"transport":"off"}'
    Assert-True `
        ($off.Status -eq 200 -and
            $off.Json.transport -eq "off" -and
            -not $off.Json.fileOpen -and
            $off.Json.completedFileCount -eq 1 -and
            $null -eq $off.Json.currentFileName) `
        "Recorder did not finalise exactly one WAV: $($off.Raw)"

    $expectedDirectory =
        Join-Path (Join-Path $recordingRoot $projectId) $recorderId
    $wavFiles =
        @(Get-ChildItem `
            -LiteralPath $expectedDirectory `
            -Filter "*.wav" `
            -File `
            -Recurse)
    Assert-True `
        ($wavFiles.Count -eq 1 -and
            $wavFiles[0].Length -gt 44) `
        "Recorder did not create one non-empty WAV in its isolated folder"

    $inspection =
        Invoke-SmokeHttp `
            -Method Get `
            -Target "/v1/projects/active/inspection"
    $eventOutput = @(
        $inspection.Json.projection.publicOutputs |
            Where-Object {
                $_.unitId -ceq $recorderId -and
                $_.outputRole -eq "recordingEvents"
            }
    )[0]
    Assert-True `
        ($null -ne $eventOutput) `
        "Recorder event output was not projected"
    $historyPath =
        "/data-blocks/" +
        [Uri]::EscapeDataString([string]$eventOutput.blockId) +
        "/history"
    $history =
        Invoke-SmokeHttp -Method Get -Target $historyPath
    $eventPath = [string]$history.Json.units[0].payload.path
    $resolvedEventPath =
        [System.IO.Path]::GetFullPath(
            (Join-Path $expectedDirectory $eventPath))
    Assert-True `
        ($history.Status -eq 200 -and
            $history.Json.count -eq 1 -and
            $history.Json.units[0].payload.state -eq "completed" -and
            $history.Json.units[0].payload.frameCount -eq $frameCount -and
            -not [System.IO.Path]::IsPathRooted($eventPath) -and
            $resolvedEventPath.Equals(
                $wavFiles[0].FullName,
                [StringComparison]::OrdinalIgnoreCase)) `
        "Recorder event did not expose one root-relative completed file"

    $surface = [string]::Join("`n", $operatorSurface)
    Assert-True `
        ($surface.IndexOf(
            $resolvedRecordingRoot,
            [StringComparison]::OrdinalIgnoreCase) -lt 0 -and
            $surface -notmatch
                '(?i)(runtimeNodeId|moduleId|/sessions(?:/|"))') `
        "Stable recorder HTTP surface leaked a host path or runtime identity"

    Write-Output (
        "project Sound Recorder HTTP smoke passed: stable transport, " +
        "private storage deployment, WAV finalisation, and relative events")
}
finally {
    if ($null -ne $client) {
        $client.Dispose()
    }
    if ($null -ne $service -and -not $service.HasExited) {
        Stop-Process `
            -Id $service.Id `
            -Force `
            -ErrorAction SilentlyContinue
        $service.WaitForExit()
    }
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $oldEnvironment[$name],
            [EnvironmentVariableTarget]::Process)
    }
    if ($resolvedRoot.StartsWith(
            $tempBase,
            [StringComparison]::OrdinalIgnoreCase) -and
        $resolvedRoot -ne $tempBase) {
        Remove-Item `
            -LiteralPath $resolvedRoot `
            -Recurse `
            -Force `
            -ErrorAction SilentlyContinue
    }
}
