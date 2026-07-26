param(
    [int]$Port = 18204,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [Parameter(Mandatory = $true)]
    [string]$IngestExe,
    [Parameter(Mandatory = $true)]
    [string]$ChildExe
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Net.Http

$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
if (-not (Test-Path -LiteralPath $serviceExe -PathType Leaf)) {
    throw "Service executable not found: $serviceExe"
}
if (-not (Test-Path -LiteralPath $IngestExe -PathType Leaf)) {
    throw "FFmpeg ingest bridge not found: $IngestExe"
}
if (-not (Test-Path -LiteralPath $ChildExe -PathType Leaf)) {
    throw "Capture lifecycle child not found: $ChildExe"
}

$tempBase = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::GetTempPath())
$root = Join-Path $tempBase (
    "pamguard-project-acquisition-" +
    [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $root | Out-Null
$resolvedRoot = [System.IO.Path]::GetFullPath($root)
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

$base = "http://127.0.0.1:$Port"
$client = New-Object System.Net.Http.HttpClient
$client.Timeout = [TimeSpan]::FromSeconds(15)
$service = $null
$operatorTranscript = New-Object System.Collections.Generic.List[string]

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
    $request.Method = New-Object System.Net.Http.HttpMethod(
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
        $request.Content = New-Object System.Net.Http.StringContent(
            $Body,
            [System.Text.Encoding]::UTF8,
            $ContentType)
    }

    try {
        $response = $client.SendAsync($request).
            GetAwaiter().GetResult()
        $raw = $response.Content.ReadAsStringAsync().
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

function Invoke-ProjectMutation {
    param(
        [string]$Etag,
        [object[]]$Operations
    )
    $body = @{
        schemaVersion = 1
        validateOnly = $false
        operations = $Operations
    } | ConvertTo-Json -Depth 64 -Compress
    return Invoke-SmokeHttp `
        -Method Post `
        -Target "/v1/projects/active/mutations" `
        -Headers @{ "If-Match" = $Etag } `
        -Body $body
}

function Add-OperatorTranscript {
    param(
        [string]$Target,
        [object]$Response
    )
    $operatorTranscript.Add($Target)
    $operatorTranscript.Add([string]$Response.Raw)
}

function Wait-ProcessGone {
    param([int]$ProcessId)
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($null -eq (
                Get-Process `
                    -Id $ProcessId `
                    -ErrorAction SilentlyContinue)) {
            return $true
        }
        Start-Sleep -Milliseconds 50
    }
    return $false
}

function Wait-NonZeroRawUnitAfter {
    param(
        [string]$Target,
        [uint64]$MinimumUid
    )
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        $history =
            Invoke-SmokeHttp -Method Get -Target $Target
        if ($history.Status -eq 200) {
            foreach ($unit in @($history.Json.units)) {
                if ([uint64]$unit.uid -le $MinimumUid) {
                    continue
                }
                foreach ($sample in @(
                        $unit.payload.interleavedPcm)) {
                    if ([Math]::Abs([double]$sample) -gt 0.000001) {
                        return $history
                    }
                }
            }
        }
        Start-Sleep -Milliseconds 50
    }
    return $null
}

function Invoke-RejectedIngestChild {
    param(
        [string]$ProjectId,
        [string]$AcquisitionUnitId,
        [uint64]$WorkingRevision,
        [int]$SampleRate,
        [int]$ChannelCount,
        [string]$Name
    )
    $stdout = Join-Path $resolvedRoot "$Name.stdout.log"
    $stderr = Join-Path $resolvedRoot "$Name.stderr.log"
    $arguments = @(
        "--source",
        "https://fixture.invalid/rejected-child",
        "--project-id",
        $ProjectId,
        "--acquisition-unit-id",
        $AcquisitionUnitId,
        "--working-revision",
        [string]$WorkingRevision,
        "--engine",
        $base,
        "--sample-rate",
        [string]$SampleRate,
        "--channels",
        [string]$ChannelCount,
        "--chunk-frames",
        "16",
        "--ffmpeg",
        ('"' + [System.IO.Path]::GetFullPath($ChildExe) + '"')
    )
    $process = Start-Process `
        -FilePath $IngestExe `
        -ArgumentList $arguments `
        -PassThru `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr
    if (-not $process.WaitForExit(5000)) {
        Stop-Process `
            -Id $process.Id `
            -Force `
            -ErrorAction SilentlyContinue
        throw "Rejected ingest child '$Name' did not exit"
    }
    $process.Refresh()
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Error = Get-Content `
            -LiteralPath $stderr `
            -Raw `
            -ErrorAction SilentlyContinue
    }
}

try {
    $env:PAMGUARD_PROJECT_DIR = $projectDir
    $env:PAMGUARD_ACTIVE_PROJECT_ID = $null
    $env:PAMGUARD_LEGACY_MODEL_COMPAT = $null
    $env:PAMGUARD_MODULE_GRAPH_FILE = $null
    $env:PAMGUARD_CAPTURE_ENABLED = "1"
    $env:PAMGUARD_INGEST_EXE =
        [System.IO.Path]::GetFullPath($IngestExe)
    $env:PAMGUARD_FFMPEG_PATH =
        [System.IO.Path]::GetFullPath($ChildExe)
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
                "Project Acquisition service exited early: " +
                (Get-Content `
                    -LiteralPath $stderrPath `
                    -Raw `
                    -ErrorAction SilentlyContinue))
        }
        try {
            $health = Invoke-SmokeHttp -Method Get -Target "/health"
            if ($health.Status -eq 200 -and $health.Json.ok) {
                $healthy = $true
                break
            }
        }
        catch {
        }
        Start-Sleep -Milliseconds 100
    }
    Assert-True `
        $healthy `
        "Project Acquisition service did not become healthy"

    $blank = Invoke-SmokeHttp `
        -Method Get `
        -Target "/v1/projects/active"
    Assert-True `
        ($blank.Status -eq 200 -and
            -not [string]::IsNullOrWhiteSpace($blank.Etag)) `
        "Blank active project or its ETag was unavailable"

    $added = Invoke-ProjectMutation `
        -Etag $blank.Etag `
        -Operations @(
            @{
                op = "addControlledUnit"
                clientRef = "inputA"
                typeId = "pamguard.acquisition"
                name = "Input A"
                dependencyPolicy = "reject"
            },
            @{
                op = "addControlledUnit"
                clientRef = "inputB"
                typeId = "pamguard.acquisition"
                name = "Input B"
                dependencyPolicy = "reject"
            }
        )
    Assert-True `
        ($added.Status -eq 200 -and
            $added.Json.active.workingRevision -eq 1) `
        "Two independent Acquisition instances were not created"
    $etag = $added.Etag
    $workingRevision = 1
    $projectId =
        [string]$added.Json.active.project.projectId
    $inputA = [string]@(
        $added.Json.createdEntities |
            Where-Object { $_.clientRef -eq "inputA" }
    )[0].id
    $inputB = [string]@(
        $added.Json.createdEntities |
            Where-Object { $_.clientRef -eq "inputB" }
    )[0].id

    $listPath = "/v1/projects/active/acquisitions"
    $listed = Invoke-SmokeHttp -Method Get -Target $listPath
    Add-OperatorTranscript $listPath $listed
    Assert-True `
        ($listed.Status -eq 200 -and
            $listed.Json.workingRevision -eq $workingRevision -and
            @($listed.Json.acquisitions).Count -eq 2 -and
            @(
                $listed.Json.acquisitions |
                    Where-Object {
                        $_.unitId -ceq $inputA -or
                        $_.unitId -ceq $inputB
                    }
            ).Count -eq 2 -and
            @(
                $listed.Json.acquisitions |
                    Where-Object {
                        $_.configurationStatus -eq
                            "needsConfiguration"
                    }
            ).Count -eq 2) `
        (
            "Active Acquisition listing did not preserve independent " +
            "unit IDs and unbound NeedsConfiguration state")

    $bindingAPath =
        "/v1/projects/active/acquisitions/$inputA/host-binding"
    $bindingBPath =
        "/v1/projects/active/acquisitions/$inputB/host-binding"
    $staleBinding = Invoke-SmokeHttp `
        -Method Put `
        -Target $bindingAPath `
        -Body (@{
            expectedWorkingRevision = 0
            expectedBindingRevision = 0
            source = @{
                kind = "url"
                url = "https://fixture.invalid/input-a"
            }
        } | ConvertTo-Json -Depth 8 -Compress)
    Add-OperatorTranscript $bindingAPath $staleBinding
    Assert-True `
        ($staleBinding.Status -eq 409 -and
            $staleBinding.Json.code -eq
                "working_revision_conflict" -and
            $staleBinding.Json.currentWorkingRevision -eq 1) `
        "Stale host-binding write was not rejected"

    $bindingA = Invoke-SmokeHttp `
        -Method Put `
        -Target $bindingAPath `
        -Body (@{
            expectedWorkingRevision = $workingRevision
            expectedBindingRevision = 0
            source = @{
                kind = "url"
                url = "https://fixture.invalid/input-a"
            }
        } | ConvertTo-Json -Depth 8 -Compress)
    $bindingB = Invoke-SmokeHttp `
        -Method Put `
        -Target $bindingBPath `
        -Body (@{
            expectedWorkingRevision = $workingRevision
            expectedBindingRevision = 0
            source = @{
                kind = "url"
                url = "https://fixture.invalid/input-b"
            }
        } | ConvertTo-Json -Depth 8 -Compress)
    Add-OperatorTranscript $bindingAPath $bindingA
    Add-OperatorTranscript $bindingBPath $bindingB
    Assert-True `
        ($bindingA.Status -eq 201 -and
            $bindingB.Status -eq 201 -and
            $bindingA.Json.hostBinding.acquisitionUnitId -ceq
                $inputA -and
            $bindingB.Json.hostBinding.acquisitionUnitId -ceq
                $inputB -and
            $bindingA.Json.hostBinding.bindingRevision -eq 1 -and
            $bindingB.Json.hostBinding.bindingRevision -eq 1) `
        "Independent host bindings were not created"
    $configuredList = Invoke-SmokeHttp `
        -Method Get `
        -Target $listPath
    Assert-True `
        (@(
            $configuredList.Json.acquisitions |
                Where-Object {
                    $_.configurationStatus -eq "configured"
                }
        ).Count -eq 2) `
        "Host-bound Acquisitions did not become configured"

    $deletedAPath = $bindingAPath
    $deletedA = Invoke-SmokeHttp `
        -Method Delete `
        -Target $deletedAPath `
        -Body (@{
            expectedWorkingRevision = $workingRevision
            expectedBindingRevision = 1
        } | ConvertTo-Json -Compress)
    $stillB = Invoke-SmokeHttp `
        -Method Get `
        -Target $bindingBPath
    Add-OperatorTranscript $deletedAPath $deletedA
    Add-OperatorTranscript $bindingBPath $stillB
    Assert-True `
        ($deletedA.Status -eq 200 -and
            $deletedA.Json.deleted -and
            $stillB.Status -eq 200 -and
            $stillB.Json.acquisitionUnitId -ceq $inputB) `
        (
            "Deleting one host binding affected another Acquisition " +
            "instance (delete=$($deletedA.Status) " +
            "$($deletedA.Raw); other=$($stillB.Status) " +
            "$($stillB.Raw))")

    $bindingA = Invoke-SmokeHttp `
        -Method Put `
        -Target $bindingAPath `
        -Body (@{
            expectedWorkingRevision = $workingRevision
            expectedBindingRevision = 0
            source = @{
                kind = "url"
                url = "https://fixture.invalid/input-a"
            }
        } | ConvertTo-Json -Depth 8 -Compress)
    Add-OperatorTranscript $bindingAPath $bindingA
    Assert-True `
        ($bindingA.Status -eq 201 -and
            $bindingA.Json.hostBinding.bindingRevision -eq 2) `
        "Binding recreation did not prevent revision ABA"

    $startedRuntime = Invoke-SmokeHttp `
        -Method Post `
        -Target "/module-runtime/control" `
        -Body '{"action":"start"}'
    Assert-True `
        ($startedRuntime.Status -eq 200 -and
            $startedRuntime.Json.running) `
        "Project runtime did not start"

    $inspection = Invoke-SmokeHttp `
        -Method Get `
        -Target "/v1/projects/active/inspection"
    $outputA = @(
        $inspection.Json.projection.publicOutputs |
            Where-Object {
                $_.unitId -ceq $inputA -and
                $_.outputRole -eq "rawAudio"
            }
    )[0]
    $outputB = @(
        $inspection.Json.projection.publicOutputs |
            Where-Object {
                $_.unitId -ceq $inputB -and
                $_.outputRole -eq "rawAudio"
            }
    )[0]
    Assert-True `
        ($null -ne $outputA -and $null -ne $outputB) `
        "Acquisition public raw-audio outputs were not projected"

    $listedA = @(
        $listed.Json.acquisitions |
            Where-Object { $_.unitId -ceq $inputA }
    )[0]
    $sampleRate =
        [int][Math]::Round([double]$listedA.sampleRateHz)
    $channelCount = [int]$listedA.channelCount
    Assert-True `
        ($sampleRate -gt 0 -and $channelCount -gt 0) `
        "Acquisition audio shape was unavailable"
    $frameCount = 16
    $pcm = New-Object byte[] (
        $frameCount * $channelCount * 4)
    for ($sample = 0;
         $sample -lt $frameCount * $channelCount;
         $sample++) {
        $value = [single](0.05 * (($sample % 5) - 2))
        [BitConverter]::GetBytes($value).CopyTo(
            $pcm,
            $sample * 4)
    }

    $wrongProjectId =
        [System.Guid]::NewGuid().ToString()
    $wrongProjectPcmPath =
        "/v1/projects/active/acquisitions/$inputA/pcm-f32le" +
        "?expectedProjectId=$wrongProjectId" +
        "&expectedWorkingRevision=$workingRevision&startSample=0"
    $wrongProjectPcm = Invoke-SmokeHttp `
        -Method Post `
        -Target $wrongProjectPcmPath `
        -Bytes $pcm `
        -ContentType "application/octet-stream"
    Add-OperatorTranscript `
        $wrongProjectPcmPath `
        $wrongProjectPcm
    Assert-True `
        ($wrongProjectPcm.Status -eq 409 -and
            $wrongProjectPcm.Json.code -eq
                "active_project_mismatch") `
        "Wrong-project PCM ingest was not rejected"

    $stalePcmPath =
        "/v1/projects/active/acquisitions/$inputA/pcm-f32le" +
        "?expectedProjectId=$projectId" +
        "&expectedWorkingRevision=0&startSample=0"
    $stalePcm = Invoke-SmokeHttp `
        -Method Post `
        -Target $stalePcmPath `
        -Bytes $pcm `
        -ContentType "application/octet-stream"
    Add-OperatorTranscript $stalePcmPath $stalePcm
    Assert-True `
        ($stalePcm.Status -eq 409 -and
            $stalePcm.Json.code -eq
                "working_revision_conflict") `
        "Stale supervised PCM ingest was not rejected"

    $pcmPath =
        "/v1/projects/active/acquisitions/$inputA/pcm-f32le" +
        "?expectedProjectId=$projectId" +
        "&expectedWorkingRevision=$workingRevision&startSample=0"
    $accepted = Invoke-SmokeHttp `
        -Method Post `
        -Target $pcmPath `
        -Bytes $pcm `
        -ContentType "application/octet-stream"
    Add-OperatorTranscript $pcmPath $accepted
    Assert-True `
        ($accepted.Status -eq 202 -and
            $accepted.Json.accepted -and
            $accepted.Json.acquisitionUnitId -ceq $inputA -and
            $accepted.Json.inputFrames -eq $frameCount) `
        "Stable Acquisition PCM ingress did not accept the chunk"

    $blockAPath =
        "/data-blocks/" +
        [Uri]::EscapeDataString([string]$outputA.blockId) +
        "/history"
    $blockBPath =
        "/data-blocks/" +
        [Uri]::EscapeDataString([string]$outputB.blockId) +
        "/history"
    $historyA = Invoke-SmokeHttp `
        -Method Get `
        -Target $blockAPath
    $historyB = Invoke-SmokeHttp `
        -Method Get `
        -Target $blockBPath
    Assert-True `
        ($historyA.Status -eq 200 -and
            $historyA.Json.count -eq 1 -and
            $historyB.Status -eq 200 -and
            $historyB.Json.count -eq 0) `
        "PCM did not reach only the selected Acquisition raw-audio block"

    $stoppedRuntime = Invoke-SmokeHttp `
        -Method Post `
        -Target "/module-runtime/control" `
        -Body '{"action":"stop"}'
    Assert-True `
        ($stoppedRuntime.Status -eq 200 -and
            -not $stoppedRuntime.Json.running) `
        "Project runtime did not stop before revision mutation"

    $renamed = Invoke-ProjectMutation `
        -Etag $etag `
        -Operations @(
            @{
                op = "renameControlledUnit"
                unit = @{ id = $inputB }
                name = "Input B revised"
            }
        )
    Assert-True `
        ($renamed.Status -eq 200 -and
            $renamed.Json.active.workingRevision -eq 2) `
        "Project revision mutation failed"
    $etag = $renamed.Etag
    $workingRevision = 2

    $reconciled = Invoke-SmokeHttp `
        -Method Get `
        -Target $listPath
    Add-OperatorTranscript $listPath $reconciled
    $carriedA = Invoke-SmokeHttp `
        -Method Get `
        -Target $bindingAPath
    $carriedB = Invoke-SmokeHttp `
        -Method Get `
        -Target $bindingBPath
    Add-OperatorTranscript $bindingAPath $carriedA
    Add-OperatorTranscript $bindingBPath $carriedB
    Assert-True `
        (@(
            $reconciled.Json.acquisitions |
                Where-Object {
                    $null -ne $_.hostBindingRevision
                }
        ).Count -eq 2 -and
            @(
                $reconciled.Json.acquisitions |
                    Where-Object {
                        $_.configurationStatus -eq "configured"
                    }
            ).Count -eq 2 -and
            $carriedA.Status -eq 200 -and
            $carriedA.Json.workingRevision -eq 2 -and
            $carriedA.Json.bindingRevision -eq 2 -and
            $carriedA.Json.source.url -ceq
                "https://fixture.invalid/input-a" -and
            $carriedB.Status -eq 200 -and
            $carriedB.Json.workingRevision -eq 2 -and
            $carriedB.Json.bindingRevision -eq 1 -and
            $carriedB.Json.source.url -ceq
                "https://fixture.invalid/input-b") `
        (
            "Stable Acquisition host bindings were not carried across an " +
            "unrelated project working-revision change")

    $startedRuntime = Invoke-SmokeHttp `
        -Method Post `
        -Target "/module-runtime/control" `
        -Body '{"action":"start"}'
    Assert-True `
        ($startedRuntime.Status -eq 200 -and
            $startedRuntime.Json.running) `
        "Revised project runtime did not start"

    $wrongProjectChild =
        Invoke-RejectedIngestChild `
            -ProjectId ([System.Guid]::NewGuid().ToString()) `
            -AcquisitionUnitId $inputA `
            -WorkingRevision $workingRevision `
            -SampleRate $sampleRate `
            -ChannelCount $channelCount `
            -Name "wrong-project-child"
    Assert-True `
        ($wrongProjectChild.ExitCode -ne 0 -and
            $wrongProjectChild.Error -match
                "project ID is not the active project") `
        "Capture child did not fail closed on a stale project identity"

    $staleRevisionChild =
        Invoke-RejectedIngestChild `
            -ProjectId $projectId `
            -AcquisitionUnitId $inputA `
            -WorkingRevision 1 `
            -SampleRate $sampleRate `
            -ChannelCount $channelCount `
            -Name "stale-revision-child"
    Assert-True `
        ($staleRevisionChild.ExitCode -ne 0 -and
            $staleRevisionChild.Error -match
                "working revision is stale") `
        "Capture child did not fail closed on a stale working revision"

    $revisedInspection = Invoke-SmokeHttp `
        -Method Get `
        -Target "/v1/projects/active/inspection"
    $outputA = @(
        $revisedInspection.Json.projection.publicOutputs |
            Where-Object {
                $_.unitId -ceq $inputA -and
                $_.outputRole -eq "rawAudio"
            }
    )[0]
    Assert-True `
        ($revisedInspection.Status -eq 200 -and
            $null -ne $outputA) `
        "Revised Acquisition raw-audio output was unavailable"
    $blockAPath =
        "/data-blocks/" +
        [Uri]::EscapeDataString([string]$outputA.blockId) +
        "/history"
    $historyBeforeCapture = Invoke-SmokeHttp `
        -Method Get `
        -Target $blockAPath
    [uint64]$uidBeforeCapture = 0
    foreach ($unit in @($historyBeforeCapture.Json.units)) {
        $uidBeforeCapture = [Math]::Max(
            $uidBeforeCapture,
            [uint64]$unit.uid)
    }

    $startPath =
        "/v1/projects/active/acquisitions/$inputA/capture:start"
    $capture = Invoke-SmokeHttp `
        -Method Post `
        -Target $startPath `
        -Body (@{
            expectedWorkingRevision = $workingRevision
        } | ConvertTo-Json -Compress)
    Add-OperatorTranscript $startPath $capture
    Assert-True `
        ($capture.Status -eq 200 -and
            $capture.Json.started -and
            $capture.Json.acquisitionUnitId -ceq $inputA -and
            $capture.Json.processId -gt 0) `
        "Project-owned capture child did not start"
    $captureProcessId = [int]$capture.Json.processId
    $captureHistory =
        Wait-NonZeroRawUnitAfter `
            -Target $blockAPath `
            -MinimumUid $uidBeforeCapture
    Assert-True `
        ($null -ne $captureHistory) `
        (
            "Real capture ingest child did not publish new non-zero " +
            "project raw audio")

    $statusAPath =
        "/v1/projects/active/acquisitions/$inputA/capture-status"
    $statusBPath =
        "/v1/projects/active/acquisitions/$inputB/capture-status"
    $statusA = Invoke-SmokeHttp `
        -Method Get `
        -Target $statusAPath
    $statusB = Invoke-SmokeHttp `
        -Method Get `
        -Target $statusBPath
    Add-OperatorTranscript $statusAPath $statusA
    Add-OperatorTranscript $statusBPath $statusB
    Assert-True `
        ($statusA.Status -eq 200 -and
            $statusA.Json.running -and
            $statusB.Status -eq 200 -and
            -not $statusB.Json.running) `
        "Capture status was not independent by Acquisition unit"

    $readyWithCapture =
        Invoke-SmokeHttp -Method Get -Target "/ready"
    Assert-True `
        ($readyWithCapture.Status -eq 200 -and
            $readyWithCapture.Json.ready -and
            $readyWithCapture.Json.authorityMode -eq "project" -and
            $readyWithCapture.Json.acquisitionCapture.requiredCount -eq
                1 -and
            $readyWithCapture.Json.acquisitionCapture.healthyCount -eq
                1 -and
            @($readyWithCapture.Json.issues).Count -eq 0 -and
            $null -eq $readyWithCapture.Json.PSObject.
                Properties["sessions"] -and
            $null -eq $readyWithCapture.Json.PSObject.
                Properties["capacityAvailable"]) `
        (
            "Project readiness was not derived from its live Acquisition " +
            "capture or still exposed legacy session capacity: " +
            $readyWithCapture.Raw)

    Stop-Process `
        -Id $captureProcessId `
        -Force `
        -ErrorAction Stop
    Assert-True `
        (Wait-ProcessGone -ProcessId $captureProcessId) `
        "Injected required-capture child death was not observed"
    $deadReadiness = $null
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        $candidate =
            Invoke-SmokeHttp -Method Get -Target "/ready"
        $deadIssue = @(
            $candidate.Json.issues |
                Where-Object {
                    $_.code -eq "required_capture_dead" -and
                    $_.unitId -ceq $inputA -and
                    $_.action -eq "restart-capture"
                }
        )
        if ($candidate.Status -eq 503 -and
            -not $candidate.Json.ready -and
            $deadIssue.Count -eq 1) {
            $deadReadiness = $candidate
            break
        }
        Start-Sleep -Milliseconds 25
    }
    Assert-True `
        ($null -ne $deadReadiness) `
        (
            "Dead required capture did not produce a latched, actionable " +
            "503 readiness issue")

    $capture = Invoke-SmokeHttp `
        -Method Post `
        -Target $startPath `
        -Body (@{
            expectedWorkingRevision = $workingRevision
        } | ConvertTo-Json -Compress)
    Assert-True `
        ($capture.Status -eq 200 -and
            $capture.Json.started -and
            $capture.Json.processId -gt 0) `
        "Required capture could not restart after injected child death"
    $captureProcessId = [int]$capture.Json.processId
    $recoveredReadiness =
        Invoke-SmokeHttp -Method Get -Target "/ready"
    Assert-True `
        ($recoveredReadiness.Status -eq 200 -and
            $recoveredReadiness.Json.ready -and
            $recoveredReadiness.Json.acquisitionCapture.requiredCount -eq
                1 -and
            $recoveredReadiness.Json.acquisitionCapture.healthyCount -eq
                1) `
        (
            "Readiness did not recover after the required capture was " +
            "restarted: " + $recoveredReadiness.Raw)

    $staleStopPath =
        "/v1/projects/active/acquisitions/$inputA/capture:stop"
    $staleStop = Invoke-SmokeHttp `
        -Method Post `
        -Target $staleStopPath `
        -Body '{"expectedWorkingRevision":1}'
    Add-OperatorTranscript $staleStopPath $staleStop
    Assert-True `
        ($staleStop.Status -eq 409 -and
            $staleStop.Json.code -eq
                "working_revision_conflict") `
        "Stale capture stop was not rejected"

    $stoppedCapture = Invoke-SmokeHttp `
        -Method Post `
        -Target $staleStopPath `
        -Body (@{
            expectedWorkingRevision = $workingRevision
        } | ConvertTo-Json -Compress)
    Add-OperatorTranscript $staleStopPath $stoppedCapture
    Assert-True `
        ($stoppedCapture.Status -eq 200 -and
            $stoppedCapture.Json.stopped -and
            $stoppedCapture.Json.acquisitionUnitId -ceq
                $inputA -and
            (Wait-ProcessGone -ProcessId $captureProcessId)) `
        "Stable capture stop did not quiesce and reap its child"
    $readyAfterIntentionalStop =
        Invoke-SmokeHttp -Method Get -Target "/ready"
    Assert-True `
        ($readyAfterIntentionalStop.Status -eq 200 -and
            $readyAfterIntentionalStop.Json.ready -and
            $readyAfterIntentionalStop.Json.acquisitionCapture.
                requiredCount -eq 0) `
        (
            "Explicit capture stop did not clear the readiness " +
            "requirement: " + $readyAfterIntentionalStop.Raw)

    $capture = Invoke-SmokeHttp `
        -Method Post `
        -Target $startPath `
        -Body (@{
            expectedWorkingRevision = $workingRevision
        } | ConvertTo-Json -Compress)
    Add-OperatorTranscript $startPath $capture
    Assert-True `
        ($capture.Status -eq 200 -and
            $capture.Json.started -and
            $capture.Json.processId -gt 0) `
        "Capture could not restart after explicit stop"
    $captureProcessId = [int]$capture.Json.processId

    $switched = Invoke-SmokeHttp `
        -Method Post `
        -Target "/v1/projects/active/new" `
        -Headers @{ "If-Match" = $etag } `
        -Body (@{
            schemaVersion = 1
            name = "Switched project"
            description = "capture lifecycle smoke"
            discardDirty = $true
        } | ConvertTo-Json -Compress)
    Assert-True `
        ($switched.Status -eq 200 -and
            $switched.Json.project.projectId -cne
                $added.Json.active.project.projectId) `
        "Active project switch failed"
    Assert-True `
        (Wait-ProcessGone -ProcessId $captureProcessId) `
        "Project switch left the old Acquisition capture child running"

    $afterSwitch = Invoke-SmokeHttp `
        -Method Get `
        -Target $listPath
    Add-OperatorTranscript $listPath $afterSwitch
    Assert-True `
        ($afterSwitch.Status -eq 200 -and
            @($afterSwitch.Json.acquisitions).Count -eq 0) `
        "Old Acquisition instances remained active after project switch"

    $oldStatus = Invoke-SmokeHttp `
        -Method Get `
        -Target $statusAPath
    Add-OperatorTranscript $statusAPath $oldStatus
    Assert-True `
        ($oldStatus.Status -eq 404 -and
            $oldStatus.Json.code -eq
                "acquisition_not_found") `
        "Old Acquisition target remained addressable after project switch"

    $operatorSurface = [string]::Join(
        "`n",
        $operatorTranscript)
    Assert-True `
        ($operatorSurface -notmatch
            '(?i)(/sessions(?:/|")|moduleId|runtimeNodeId)') `
        "Stable Acquisition requests or responses leaked a session/runtime node ID"

    Write-Output (
        "project Acquisition HTTP smoke passed: stable unit IDs, " +
        "binding CAS/revision reconciliation, real non-zero child PCM, " +
        "stale child fencing, dynamic readiness failure/recovery, " +
        "independent capture, and child cleanup")
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
