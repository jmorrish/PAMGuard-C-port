param(
    [int]$Port = 18206,
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
$testRoot = Join-Path $tempBase (
    "pamguard-tracked-click-http-" +
    [System.Guid]::NewGuid().ToString("N"))
$projectDirectory = Join-Path $testRoot "projects"
New-Item -ItemType Directory -Path $projectDirectory | Out-Null
$resolvedRoot = [System.IO.Path]::GetFullPath($testRoot)
$stdoutPath = Join-Path $resolvedRoot "service.stdout.log"
$stderrPath = Join-Path $resolvedRoot "service.stderr.log"

$environmentNames = @(
    "PAMGUARD_PROJECT_DIR",
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
    $response = $null
    try {
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

function Invariant-Number {
    param([double]$Value)
    return $Value.ToString(
        "R",
        [Globalization.CultureInfo]::InvariantCulture)
}

function Angle-Difference {
    param(
        [double]$Left,
        [double]$Right
    )
    $difference = $Left - $Right
    while ($difference -gt [Math]::PI) {
        $difference -= 2.0 * [Math]::PI
    }
    while ($difference -lt -[Math]::PI) {
        $difference += 2.0 * [Math]::PI
    }
    return [Math]::Abs($difference)
}

function New-ClickPcm {
    param(
        [int]$Frames,
        [int]$Channels,
        [double]$SampleRate
    )
    $bytes = New-Object byte[] ($Frames * $Channels * 4)
    foreach ($frame in 0..($Frames - 1)) {
        $background =
            0.004 * [Math]::Sin(
                2.0 * [Math]::PI * 6000.0 *
                $frame / $SampleRate)
        $pulse = 0.0
        foreach ($start in @(1024, 3072, 5120)) {
            if ($frame -eq $start) {
                $pulse = 0.98
            }
            elseif ($frame -eq $start + 1) {
                $pulse = -0.49
            }
        }
        $value = [single]($background + $pulse)
        foreach ($channel in 0..($Channels - 1)) {
            [BitConverter]::GetBytes($value).CopyTo(
                $bytes,
                ($frame * $Channels + $channel) * 4)
        }
    }
    return $bytes
}

try {
    $env:PAMGUARD_PROJECT_DIR = $projectDirectory
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
                "Tracked-click service exited early: " +
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
                name = "Moving Array Input"
                dependencyPolicy = "reject"
            },
            @{
                op = "addControlledUnit"
                clientRef = "clicks"
                typeId = "pamguard.click-detector"
                name = "Moving Array Click Detector"
                dependencyPolicy = "reject"
            },
            @{
                op = "setBinding"
                unit = @{ clientRef = "clicks" }
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
        "Moving-array Click Detector project was not runnable: $($added.Raw)"

    $projectId =
        [string]$added.Json.active.project.projectId
    $inputId = [string]@(
        $added.Json.createdEntities |
            Where-Object { $_.clientRef -eq "input" }
    )[0].id
    $clickDetectorId = [string]@(
        $added.Json.createdEntities |
            Where-Object { $_.clientRef -eq "clicks" }
    )[0].id
    Assert-True `
        (-not [string]::IsNullOrWhiteSpace($inputId) -and
            -not [string]::IsNullOrWhiteSpace($clickDetectorId)) `
        "Stable Acquisition/Click Detector IDs were not returned"

    $inspection =
        Invoke-SmokeHttp `
            -Method Get `
            -Target "/v1/projects/active/inspection"
    $clickOutput = @(
        $inspection.Json.projection.publicOutputs |
            Where-Object {
                $_.unitId -ceq $clickDetectorId -and
                $_.outputRole -eq "clicks"
            }
    )[0]
    Assert-True `
        ($inspection.Status -eq 200 -and
            -not [string]::IsNullOrWhiteSpace(
                [string]$clickOutput.blockId)) `
        "Click Detector retained-click output was not projected"

    $runtimeStarted =
        Invoke-SmokeHttp `
            -Method Post `
            -Target "/module-runtime/control" `
            -Body '{"action":"start"}'
    Assert-True `
        ($runtimeStarted.Status -eq 200 -and
            $runtimeStarted.Json.running) `
        "Moving-array project runtime did not start"

    $acquisitions =
        Invoke-SmokeHttp `
            -Method Get `
            -Target "/v1/projects/active/acquisitions"
    $input = @(
        $acquisitions.Json.acquisitions |
            Where-Object { $_.unitId -ceq $inputId }
    )[0]
    $sampleRate = [double]$input.sampleRateHz
    $channelCount = [int]$input.channelCount
    Assert-True `
        ($sampleRate -eq 48000.0 -and $channelCount -eq 2) `
        "Pinned default Acquisition geometry was unavailable"

    $targetEast = 750.0
    $targetNorth = 60.0
    $northPositions = @(-240.0, -120.0, 0.0, 120.0, 240.0)
    $poses = @()
    for ($index = 0; $index -lt $northPositions.Count; $index++) {
        $north = [double]$northPositions[$index]
        $bearing = [Math]::Atan2(
            $targetEast,
            $targetNorth - $north)
        $poses += [pscustomobject]@{
            East = 0.0
            North = $north
            Height = -8.0 - 0.5 * $index
            Bearing = $bearing
            HeadingDegrees =
                ($bearing - [Math]::PI / 2.0) *
                180.0 / [Math]::PI
            TimeMs = [int64](1700000000000 + 250 * $index)
        }
    }

    $frameCount = 6144
    $pcm = New-ClickPcm `
        -Frames $frameCount `
        -Channels $channelCount `
        -SampleRate $sampleRate
    for ($index = 0; $index -lt $poses.Count; $index++) {
        $pose = $poses[$index]
        $startSample = [int64]($index * $frameCount)
        $pcmPath =
            "/v1/projects/active/acquisitions/$inputId/pcm-f32le" +
            "?expectedProjectId=$projectId" +
            "&expectedWorkingRevision=1" +
            "&startSample=$startSample" +
            "&timeMs=$($pose.TimeMs)" +
            "&headingDegrees=$(Invariant-Number $pose.HeadingDegrees)" +
            "&pitchDegrees=0&rollDegrees=0" +
            "&originEastMetres=$(Invariant-Number $pose.East)" +
            "&originNorthMetres=$(Invariant-Number $pose.North)" +
            "&originHeightMetres=$(Invariant-Number $pose.Height)"
        $accepted =
            Invoke-SmokeHttp `
                -Method Post `
                -Target $pcmPath `
                -Bytes $pcm `
                -ContentType "application/octet-stream"
        Assert-True `
            ($accepted.Status -eq 202 -and
                $accepted.Json.inputFrames -eq $frameCount -and
                $accepted.Json.orientationAccepted -and
                $accepted.Json.navigationSampleAccepted) `
            "Posed PCM chunk $index was not accepted: $($accepted.Raw)"
    }

    $historyPath =
        "/data-blocks/" +
        [Uri]::EscapeDataString([string]$clickOutput.blockId) +
        "/history?limit=256"
    $selectedClicks = @()
    $lastHistory = $null
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        $lastHistory =
            Invoke-SmokeHttp -Method Get -Target $historyPath
        $selectedClicks = @()
        if ($lastHistory.Status -eq 200) {
            foreach ($pose in $poses) {
                $candidate = @(
                    $lastHistory.Json.units |
                        Where-Object {
                            @($_.payload.navigationOriginMetres).
                                    Count -eq 3 -and
                            [Math]::Abs(
                                [double]$_.payload.
                                    navigationOriginMetres[1] -
                                $pose.North) -lt 1.0e-9 -and
                            @($_.payload.
                                earthBearingAmbiguitiesRadians).
                                    Count -eq 2
                        } |
                        Sort-Object startSample |
                        Select-Object -First 1
                )
                if ($candidate.Count -eq 1) {
                    $selectedClicks += $candidate[0]
                }
            }
        }
        if ($selectedClicks.Count -eq $poses.Count) {
            break
        }
        Start-Sleep -Milliseconds 100
    }
    Assert-True `
        ($selectedClicks.Count -eq $poses.Count) `
        ("Did not retain one localised click for every platform pose: " +
            $lastHistory.Raw)

    for ($index = 0; $index -lt $selectedClicks.Count; $index++) {
        $click = $selectedClicks[$index]
        $pose = $poses[$index]
        $headingRadians =
            $pose.HeadingDegrees * [Math]::PI / 180.0
        $ambiguities =
            @($click.payload.earthBearingAmbiguitiesRadians)
        $aimedBearing = @(
            $ambiguities |
                Where-Object {
                    (Angle-Difference `
                        -Left ([double]$_) `
                        -Right $pose.Bearing) -lt 0.03
                }
        )
        Assert-True `
            (@($click.payload.orientation).Count -eq 1 -and
                [Math]::Abs(
                    [double]$click.payload.orientation.headingDegrees -
                    $pose.HeadingDegrees) -lt 1.0e-9 -and
                [string]$click.payload.navigationReferenceId -ceq
                    $inputId -and
                $ambiguities.Count -eq 2 -and
                $aimedBearing.Count -eq 1) `
            "Retained click $index lost its trigger-onset pose/bearing"
    }

    $locators = @(
        $selectedClicks |
            ForEach-Object {
                @{
                    uid = [uint64]$_.uid
                    startSample = [int64]$_.startSample
                    channelBitmap = [uint32]$_.channelBitmap
                }
            }
    )
    $assignmentBody = @{
        clicks = $locators
        eventId = $null
    } | ConvertTo-Json -Depth 16 -Compress
    $trackedBase =
        "/v1/projects/active/click-detectors/$clickDetectorId"
    $assigned =
        Invoke-SmokeHttp `
            -Method Post `
            -Target "$trackedBase/tracked-events:assign" `
            -Body $assignmentBody
    Assert-True `
        ($assigned.Status -eq 201 -and
            $assigned.Json.eventId -eq 1 -and
            $assigned.Json.clickCount -eq $poses.Count -and
            $assigned.Json.localisation.status -eq "available" -and
            $assigned.Json.localisation.available -and
            $assigned.Json.localisation.code -eq
                "least_squares_available") `
        "Tracked event did not expose complete moving-array inputs: $($assigned.Raw)"
    foreach ($eventClick in @($assigned.Json.clicks)) {
        Assert-True `
            (@($eventClick.originMetres).Count -eq 3 -and
                $null -ne $eventClick.headingRadians -and
                @($eventClick.earthBearingAmbiguitiesRadians).
                    Count -eq 2 -and
                [string]$eventClick.navigationReferenceId -ceq
                    $inputId) `
            "Tracked-event readback omitted a pose/bearing input"
    }

    $localised =
        Invoke-SmokeHttp `
            -Method Post `
            -Target "$trackedBase/tracked-events/1:localise" `
            -Body "{}"
    $ambiguityResults = @($localised.Json.ambiguities)
    $acceptedResults = @(
        $ambiguityResults |
            Where-Object { $_.accepted }
    )
    $targetResults = @(
        $acceptedResults |
            Where-Object {
                @($_.fit.positionMetres).Count -eq 3 -and
                [Math]::Abs(
                    [double]$_.fit.positionMetres[0] -
                    $targetEast) -lt 50.0 -and
                [Math]::Abs(
                    [double]$_.fit.positionMetres[1] -
                    $targetNorth) -lt 50.0
            }
    )
    Assert-True `
        ($localised.Status -eq 200 -and
            $localised.Json.status -eq "executed" -and
            $localised.Json.executed -and
            $localised.Json.accepted -and
            $localised.Json.code -eq "least_squares_executed" -and
            $localised.Json.assessment.code -eq
                "least_squares_available" -and
            $ambiguityResults.Count -eq 2 -and
            $acceptedResults.Count -ge 1 -and
            $targetResults.Count -eq 1) `
        "Moving-array Least Squares result was incomplete: $($localised.Raw)"

    $targetResult = $targetResults[0]
    Assert-True `
        ($targetResult.fit.status -eq "success" -and
            $targetResult.fit.succeeded -and
            @($targetResult.fit.selectedObservationIndices).Count -eq
                $poses.Count -and
            $null -ne $targetResult.beamSampleTimeMs -and
            [double]$targetResult.beamDistanceMetres -gt 0.0 -and
            $targetResult.filterAssessment.passesRunawayGuard -and
            $targetResult.filterAssessment.passesConfiguredLimits -and
            $targetResult.filterAssessment.accepted) `
        "Successful target fit omitted beam-track/filter evidence"

    $deleted =
        Invoke-SmokeHttp `
            -Method Delete `
            -Target "$trackedBase/tracked-events/1"
    Assert-True `
        ($deleted.Status -eq 200 -and $deleted.Json.deleted) `
        "Tracked-event cleanup failed"

    $runtimeStopped =
        Invoke-SmokeHttp `
            -Method Post `
            -Target "/module-runtime/control" `
            -Body '{"action":"stop"}'
    Assert-True `
        ($runtimeStopped.Status -eq 200 -and
            -not $runtimeStopped.Json.running) `
        "Moving-array project runtime did not stop"

    Write-Output (
        "project tracked-click localisation HTTP smoke passed: five " +
        "trigger-onset poses, ordered earth bearings, Least Squares fit, " +
        "navigation beam sample, and PAMGuard range/height filters")
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
