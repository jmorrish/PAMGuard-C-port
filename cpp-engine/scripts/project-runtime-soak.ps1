param(
    [int]$Port = 18207,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [int]$DurationSeconds = 6,
    [int]$FramesPerChunk = 4096,
    [int]$ComparisonChunks = 8
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Net.Http

if ($DurationSeconds -lt 1) {
    throw "DurationSeconds must be at least 1"
}
if ($FramesPerChunk -lt 2048) {
    throw "FramesPerChunk must be at least 2048"
}
if ($ComparisonChunks -lt 2) {
    throw "ComparisonChunks must be at least 2"
}

$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
if (-not (Test-Path -LiteralPath $serviceExe -PathType Leaf)) {
    throw "Service executable not found: $serviceExe"
}
if (-not (Get-Command curl.exe -ErrorAction SilentlyContinue)) {
    throw "curl.exe is required for the concurrent stream clients"
}

$tempBase = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::GetTempPath())
$testRoot = Join-Path $tempBase (
    "pamguard-project-runtime-soak-" +
    [System.Guid]::NewGuid().ToString("N"))
$projectDirectory = Join-Path $testRoot "projects"
New-Item -ItemType Directory -Path $projectDirectory | Out-Null
$resolvedRoot = [System.IO.Path]::GetFullPath($testRoot)
$stdoutPath = Join-Path $resolvedRoot "service.stdout.log"
$stderrPath = Join-Path $resolvedRoot "service.stderr.log"
$slowFftPath = Join-Path $resolvedRoot "slow-fft.ndjson"
$slowClickPath = Join-Path $resolvedRoot "slow-click.ndjson"
$slowAudioPath = Join-Path $resolvedRoot "slow-audio.f32le"

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
$http = New-Object System.Net.Http.HttpClient
$http.Timeout = [TimeSpan]::FromSeconds(15)
$service = $null
$streamClients = @()

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-SoakHttp {
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
            $http.SendAsync($request).GetAwaiter().GetResult()
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

function New-ProjectPcm {
    param(
        [int]$Frames,
        [int]$Channels,
        [double]$SampleRate
    )

    $bytes = New-Object byte[] ($Frames * $Channels * 4)
    for ($frame = 0; $frame -lt $Frames; $frame++) {
        $background =
            0.004 * [Math]::Sin(
                2.0 * [Math]::PI * 6000.0 *
                $frame / $SampleRate)
        $pulse = 0.0
        foreach ($start in @(1024, 3072)) {
            if ($frame -eq $start) {
                $pulse = 0.98
            }
            elseif ($frame -eq $start + 1) {
                $pulse = -0.49
            }
        }
        $value = [single]($background + $pulse)
        for ($channel = 0; $channel -lt $Channels; $channel++) {
            [BitConverter]::GetBytes($value).CopyTo(
                $bytes,
                ($frame * $Channels + $channel) * 4)
        }
    }
    return $bytes
}

function Get-Block {
    param(
        [object]$Response,
        [string]$BlockId
    )
    return @(
        $Response.Json.dataBlocks |
            Where-Object { $_.id -ceq $BlockId }
    )[0]
}

function Assert-BlockBounds {
    param(
        [object]$Response,
        [string]$Context
    )

    Assert-True `
        ($Response.Status -eq 200 -and
            $Response.Json.running) `
        "$Context could not inspect the running project blocks"
    foreach ($block in @($Response.Json.dataBlocks)) {
        Assert-True `
            ($block.stats.observerErrors -eq 0) `
            "$Context block '$($block.id)' reported observer errors"
        Assert-True `
            ($block.stats.historySize -le $block.historyCapacity) `
            "$Context block '$($block.id)' exceeded bounded history"
        Assert-True `
            ($block.stats.queuedUnits -le
                $block.stats.maximumQueuedUnits) `
            "$Context block '$($block.id)' exceeded subscriber queues"
    }
}

function Send-ProjectPcm {
    param(
        [string]$ProjectId,
        [string]$AcquisitionId,
        [uint64]$StartSample,
        [int64]$TimeMs,
        [int]$ExpectedRevision,
        [byte[]]$Pcm,
        [int]$ExpectedFrames
    )

    $target =
        "/v1/projects/active/acquisitions/" +
        "$AcquisitionId/pcm-f32le" +
        "?expectedProjectId=$ProjectId" +
        "&expectedWorkingRevision=$ExpectedRevision" +
        "&startSample=$StartSample&timeMs=$TimeMs"
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $accepted =
        Invoke-SoakHttp `
            -Method Post `
            -Target $target `
            -Bytes $Pcm `
            -ContentType "application/octet-stream"
    $watch.Stop()
    Assert-True `
        ($accepted.Status -eq 202 -and
            $accepted.Json.accepted -and
            $accepted.Json.inputFrames -eq $ExpectedFrames) `
        "Project-authoritative PCM ingest failed: $($accepted.Raw)"
    return $watch.Elapsed.TotalMilliseconds
}

function Get-PhaseUnits {
    param(
        [string]$BlockId,
        [int64]$PhaseStartSample,
        [int64]$PhaseFrameCount,
        [string]$Context
    )

    $phaseEndSample = $PhaseStartSample + $PhaseFrameCount
    $phaseLowerSample = $PhaseStartSample - $FramesPerChunk
    $encodedBlock = [Uri]::EscapeDataString($BlockId)
    $previousCount = -1
    $stablePolls = 0
    $selected = @()
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        $history =
            Invoke-SoakHttp `
                -Method Get `
                -Target "/data-blocks/$encodedBlock/history?limit=4096"
        Assert-True `
            ($history.Status -eq 200) `
            "$Context history was unavailable: $($history.Raw)"
        $selected = @(
            $history.Json.units |
                Where-Object {
                    [int64]$_.startSample -ge $phaseLowerSample -and
                    [int64]$_.startSample -lt $phaseEndSample
                }
        )
        if ($selected.Count -gt 0 -and
            $selected.Count -eq $previousCount) {
            $stablePolls++
            if ($stablePolls -ge 2) {
                return $selected
            }
        }
        else {
            $stablePolls = 0
        }
        $previousCount = $selected.Count
        Start-Sleep -Milliseconds 50
    }
    throw "$Context history did not settle on a non-empty sequence"
}

function Get-ScientificSignature {
    param(
        [object[]]$Units,
        [int64]$PhaseStartSample,
        [int64]$PhaseStartTimeMs,
        [bool]$NormalizeClickPayload,
        [string]$Context
    )

    $ordered = @(
        $Units |
            Sort-Object `
                @{ Expression = { [int64]$_.startSample } },
                @{ Expression = { [uint64]$_.sequence } },
                @{ Expression = { [uint64]$_.channelBitmap } }
    )
    Assert-True `
        ($ordered.Count -gt 0) `
        "$Context contains no scientific units"
    $canonicalUnits = @()
    for ($ordinal = 0; $ordinal -lt $ordered.Count; $ordinal++) {
        $unit = $ordered[$ordinal]
        $payload =
            $unit.payload |
                ConvertTo-Json -Depth 64 -Compress |
                ConvertFrom-Json
        if ($NormalizeClickPayload) {
            $payload.startSample =
                [int64]$payload.startSample -
                [int64]$PhaseStartSample
            $payload.timeMs =
                [int64]$payload.timeMs -
                $PhaseStartTimeMs
        }
        $canonicalUnits += [ordered]@{
            typeId = [string]$unit.typeId
            schemaVersion = [int]$unit.schemaVersion
            # uid, sourceBlockId and sequence are run/transport identities.
            # Hash the ordered scientific metadata and payload instead.
            ordinal = $ordinal
            relativeTimeMs =
                [int64]$unit.timeMs - $PhaseStartTimeMs
            relativeStartSample =
                [int64]$unit.startSample -
                [int64]$PhaseStartSample
            durationSamples = [uint64]$unit.durationSamples
            channelBitmap = [uint64]$unit.channelBitmap
            sequenceBitmap = [uint64]$unit.sequenceBitmap
            clockDomainId = [string]$unit.clockDomainId
            discontinuity = [bool]$unit.discontinuity
            payload = $payload
        }
    }
    $canonicalJson =
        ConvertTo-Json `
            -InputObject @($canonicalUnits) `
            -Depth 64 `
            -Compress
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $digestBytes =
            $sha.ComputeHash(
                [Text.Encoding]::UTF8.GetBytes($canonicalJson))
        $digest =
            ($digestBytes |
                ForEach-Object { $_.ToString("x2") }) -join ""
    }
    finally {
        $sha.Dispose()
    }
    return [pscustomobject]@{
        Count = $ordered.Count
        Digest = $digest
        FirstRelativeStartSample =
            [int64]$ordered[0].startSample -
            [int64]$PhaseStartSample
        LastRelativeStartSample =
            [int64]$ordered[-1].startSample -
            [int64]$PhaseStartSample
    }
}

function Start-StreamClient {
    param(
        [string]$Label,
        [string]$Target,
        [string]$OutputPath,
        [string]$LimitRate
    )

    $arguments = @(
        "--no-buffer",
        "--silent",
        "--show-error",
        "--fail",
        "--max-time",
        [string]($DurationSeconds + 15)
    )
    if (-not [string]::IsNullOrWhiteSpace($LimitRate)) {
        $arguments += @("--limit-rate", $LimitRate)
    }
    $arguments += @("--output", $OutputPath, "$base$Target")
    $process = Start-Process `
        -FilePath "curl.exe" `
        -ArgumentList $arguments `
        -PassThru `
        -WindowStyle Hidden
    return [pscustomobject]@{
        Label = $Label
        Process = $process
    }
}

function Stop-StreamClients {
    foreach ($entry in $streamClients) {
        if ($entry.Process -and -not $entry.Process.HasExited) {
            Stop-Process `
                -Id $entry.Process.Id `
                -Force `
                -ErrorAction SilentlyContinue
        }
    }
    foreach ($entry in $streamClients) {
        if ($entry.Process) {
            [void]$entry.Process.WaitForExit(3000)
        }
    }
}

try {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $null,
            [EnvironmentVariableTarget]::Process)
    }
    $env:PAMGUARD_PROJECT_DIR = $projectDirectory

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
                "Project soak service exited early: " +
                (Get-Content `
                    -LiteralPath $stderrPath `
                    -Raw `
                    -ErrorAction SilentlyContinue))
        }
        try {
            $health =
                Invoke-SoakHttp -Method Get -Target "/health"
            if ($health.Status -eq 200 -and
                $health.Json.ok -and
                $health.Json.authorityMode -eq "project") {
                $healthy = $true
                break
            }
        }
        catch {
        }
        Start-Sleep -Milliseconds 100
    }
    Assert-True $healthy "Project soak service did not become healthy"

    $blank =
        Invoke-SoakHttp `
            -Method Get `
            -Target "/v1/projects/active"
    Assert-True `
        ($blank.Status -eq 200 -and
            -not [string]::IsNullOrWhiteSpace($blank.Etag) -and
            @($blank.Json.project.controlledUnits).Count -eq 0) `
        "A blank project authority was not available"

    $templateBody = @{
        schemaVersion = 1
        validateOnly = $false
        operations = @(
            @{
                op = "addConfigurationTemplate"
                clientRef = "soak"
                templateId = "pamguard.click-monitoring"
            }
        )
    } | ConvertTo-Json -Depth 32 -Compress
    $template =
        Invoke-SoakHttp `
            -Method Post `
            -Target "/v1/projects/active/mutations" `
            -Headers @{ "If-Match" = $blank.Etag } `
            -Body $templateBody
    Assert-True `
        ($template.Status -eq 200 -and
            $template.Json.active.workingRevision -eq 1) `
        "Click-monitoring project template failed: $($template.Raw)"

    $active = $template.Json.active
    $acquisition = @(
        $active.project.controlledUnits |
            Where-Object { $_.typeId -eq "pamguard.acquisition" }
    )[0]
    $fft = @(
        $active.project.controlledUnits |
            Where-Object { $_.typeId -eq "pamguard.fft" }
    )[0]
    $userDisplay = @(
        $active.project.controlledUnits |
            Where-Object { $_.typeId -eq "pamguard.user-display" }
    )[0]
    $clickDetector = @(
        $active.project.controlledUnits |
            Where-Object { $_.typeId -eq "pamguard.click-detector" }
    )[0]
    $soundOutput = @(
        $active.project.controlledUnits |
            Where-Object { $_.typeId -eq "pamguard.sound-output" }
    )[0]
    Assert-True `
        ($null -ne $acquisition -and
            $null -ne $fft -and
            $null -ne $userDisplay -and
            $null -ne $clickDetector -and
            $null -ne $soundOutput -and
            @($active.project.controlledUnits).Count -eq 5) `
        "Template did not create the five project-owned controlled units"

    $displayTabs = @($active.project.displayTabs)
    $spectrogramTab = @(
        $displayTabs |
            Where-Object {
                $_.owner.unitId -ceq $userDisplay.id -and
                $_.owner.role -eq "main"
            }
    )[0]
    $firstSpectrogram = @(
        $spectrogramTab.displays |
            Where-Object {
                $_.providerTypeId -eq
                    "pamguard.spectrogram-display"
            }
    )[0]
    Assert-True `
        ($null -ne $firstSpectrogram) `
        "Template did not create its project-owned Spectrogram display"

    $secondSpectrogram =
        $firstSpectrogram |
            ConvertTo-Json -Depth 64 -Compress |
            ConvertFrom-Json
    $secondSpectrogram.id =
        [System.Guid]::NewGuid().ToString("D").ToLowerInvariant()
    $secondSpectrogram.settings.channelList = @(1)
    $secondSpectrogram.settings.frequencyLimits = @(0, 12000)
    $secondSpectrogram.settings.colourMap = "FIRE"
    $spectrogramTab.displays =
        @($spectrogramTab.displays) + @($secondSpectrogram)
    $spectrogramTab.layout.items =
        @($spectrogramTab.layout.items) +
        @(
            [pscustomobject]@{
                displayId = $secondSpectrogram.id
                column = 0
                row = 8
                width = 12
                height = 8
            }
        )

    $soundSettings =
        $soundOutput.settings |
            ConvertTo-Json -Depth 32 -Compress |
            ConvertFrom-Json
    $soundSettings.channelBitmap = 3
    $configureBody = @{
        schemaVersion = 1
        validateOnly = $false
        operations = @(
            @{
                op = "replaceSettings"
                unit = @{ id = [string]$soundOutput.id }
                settingsVersion = [int]$soundOutput.settingsVersion
                settings = $soundSettings
            },
            @{
                op = "replaceDisplayHierarchy"
                displayTabs = @($displayTabs)
            }
        )
    } | ConvertTo-Json -Depth 64 -Compress
    $configured =
        Invoke-SoakHttp `
            -Method Post `
            -Target "/v1/projects/active/mutations" `
            -Headers @{ "If-Match" = $template.Etag } `
            -Body $configureBody
    Assert-True `
        ($configured.Status -eq 200 -and
            $configured.Json.active.workingRevision -eq 2 -and
            $configured.Json.active.projection.status -eq "runnable") `
        "Project did not become runnable: $($configured.Raw)"
    $configuredSpectrograms = @(
        $configured.Json.active.project.displayTabs |
            ForEach-Object { $_.displays } |
            Where-Object {
                $_.providerTypeId -eq
                    "pamguard.spectrogram-display"
            }
    )
    Assert-True `
        ($configuredSpectrograms.Count -eq 2 -and
            @($configuredSpectrograms.id |
                Select-Object -Unique).Count -eq 2) `
        "Project did not retain two independent Spectrogram instances"

    $inspection =
        Invoke-SoakHttp `
            -Method Get `
            -Target "/v1/projects/active/inspection"
    $audioOutput = @(
        $inspection.Json.projection.publicOutputs |
            Where-Object {
                $_.unitId -ceq $acquisition.id -and
                $_.outputRole -eq "rawAudio"
            }
    )[0]
    $fftOutput = @(
        $inspection.Json.projection.publicOutputs |
            Where-Object {
                $_.unitId -ceq $fft.id -and
                $_.outputRole -eq "fft"
            }
    )[0]
    $clickOutput = @(
        $inspection.Json.projection.publicOutputs |
            Where-Object {
                $_.unitId -ceq $clickDetector.id -and
                $_.outputRole -eq "clicks"
            }
    )[0]
    Assert-True `
        ($inspection.Status -eq 200 -and
            @($inspection.Json.projection.displays).Count -ge 3 -and
            -not [string]::IsNullOrWhiteSpace(
                [string]$audioOutput.blockId) -and
            -not [string]::IsNullOrWhiteSpace(
                [string]$fftOutput.blockId) -and
            -not [string]::IsNullOrWhiteSpace(
                [string]$clickOutput.blockId)) `
        "Project inspection did not resolve authoritative stream blocks"

    $started =
        Invoke-SoakHttp `
            -Method Post `
            -Target "/module-runtime/control" `
            -Body '{"action":"start"}'
    $runtime =
        Invoke-SoakHttp `
            -Method Get `
            -Target "/module-runtime/status"
    $runtimeErrors = @(
        $runtime.Json.modules |
            Where-Object { $_.state -eq "error" }
    )
    Assert-True `
        ($started.Status -eq 200 -and
            $started.Json.running -and
            $runtime.Json.authorityMode -eq "project" -and
            $runtime.Json.projectId -ceq
                $configured.Json.active.project.projectId -and
            $runtime.Json.workingRevision -eq 2 -and
            $runtimeErrors.Count -eq 0) `
        "Project runtime did not start cleanly"

    $blocksBefore =
        Invoke-SoakHttp -Method Get -Target "/data-blocks"
    Assert-BlockBounds `
        -Response $blocksBefore `
        -Context "Before stream subscription"
    $audioBefore = Get-Block $blocksBefore $audioOutput.blockId
    $fftBefore = Get-Block $blocksBefore $fftOutput.blockId
    $clickBefore = Get-Block $blocksBefore $clickOutput.blockId
    Assert-True `
        ($null -ne $audioBefore -and
            $null -ne $fftBefore -and
            $null -ne $clickBefore) `
        "Authoritative project blocks were absent from the running runtime"

    $sampleRate = [double]$audioBefore.sampleRateHz
    $channelCount = 2
    $pcm =
        New-ProjectPcm `
            -Frames $FramesPerChunk `
            -Channels $channelCount `
            -SampleRate $sampleRate

    # Establish a deterministic scientific baseline without presentation
    # subscribers. The runtime is reset before replaying this exact PCM
    # sequence under display/audio pressure.
    [uint64]$comparisonFrameCount =
        [uint64]$ComparisonChunks * [uint64]$FramesPerChunk
    [uint64]$baselineStartSample = 0
    [int64]$baselineStartTimeMs = 1700000000000
    for ($chunkIndex = 0;
         $chunkIndex -lt $ComparisonChunks;
         $chunkIndex++) {
        [uint64]$sample =
            $baselineStartSample +
            [uint64]$chunkIndex * [uint64]$FramesPerChunk
        [int64]$time =
            $baselineStartTimeMs +
            [int64][Math]::Floor(
                [double]($sample - $baselineStartSample) *
                1000.0 / $sampleRate)
        [void](Send-ProjectPcm `
            -ProjectId $configured.Json.active.project.projectId `
            -AcquisitionId $acquisition.id `
            -StartSample $sample `
            -TimeMs $time `
            -ExpectedRevision 2 `
            -Pcm $pcm `
            -ExpectedFrames $FramesPerChunk)
    }
    $baselineFftUnits = @(
        Get-PhaseUnits `
            -BlockId $fftOutput.blockId `
            -PhaseStartSample $baselineStartSample `
            -PhaseFrameCount $comparisonFrameCount `
            -Context "No-pressure FFT baseline"
    )
    $baselineClickUnits = @(
        Get-PhaseUnits `
            -BlockId $clickOutput.blockId `
            -PhaseStartSample $baselineStartSample `
            -PhaseFrameCount $comparisonFrameCount `
            -Context "No-pressure Click baseline"
    )
    $baselineFftSignature =
        Get-ScientificSignature `
            -Units $baselineFftUnits `
            -PhaseStartSample $baselineStartSample `
            -PhaseStartTimeMs $baselineStartTimeMs `
            -NormalizeClickPayload $false `
            -Context "No-pressure FFT baseline"
    $baselineClickSignature =
        Get-ScientificSignature `
            -Units $baselineClickUnits `
            -PhaseStartSample $baselineStartSample `
            -PhaseStartTimeMs $baselineStartTimeMs `
            -NormalizeClickPayload $true `
            -Context "No-pressure Click baseline"

    $reset =
        Invoke-SoakHttp `
            -Method Post `
            -Target "/module-runtime/control" `
            -Body '{"action":"reset","restart":true}'
    Assert-True `
        ($reset.Status -eq 200 -and $reset.Json.running) `
        "Project runtime did not reset for pressured replay"
    $blocksPressureBefore =
        Invoke-SoakHttp -Method Get -Target "/data-blocks"
    Assert-BlockBounds `
        -Response $blocksPressureBefore `
        -Context "After deterministic baseline reset"
    $audioPressureBefore =
        Get-Block $blocksPressureBefore $audioOutput.blockId
    $fftPressureBefore =
        Get-Block $blocksPressureBefore $fftOutput.blockId
    $clickPressureBefore =
        Get-Block $blocksPressureBefore $clickOutput.blockId

    $encodedAudio =
        [Uri]::EscapeDataString([string]$audioOutput.blockId)
    $encodedFft =
        [Uri]::EscapeDataString([string]$fftOutput.blockId)
    $encodedClick =
        [Uri]::EscapeDataString([string]$clickOutput.blockId)
    $streamClients +=
        Start-StreamClient `
            -Label "fast FFT display" `
            -Target "/data-blocks/$encodedFft/stream?channels=0&cadenceMs=0" `
            -OutputPath "NUL"
    $streamClients +=
        Start-StreamClient `
            -Label "slow FFT display" `
            -Target "/data-blocks/$encodedFft/stream?channels=1&cadenceMs=0" `
            -OutputPath $slowFftPath `
            -LimitRate "1k"
    $streamClients +=
        Start-StreamClient `
            -Label "fast Click display" `
            -Target "/data-blocks/$encodedClick/stream?cadenceMs=0" `
            -OutputPath "NUL"
    $streamClients +=
        Start-StreamClient `
            -Label "slow Click display" `
            -Target "/data-blocks/$encodedClick/stream?cadenceMs=0" `
            -OutputPath $slowClickPath `
            -LimitRate "1k"
    $streamClients +=
        Start-StreamClient `
            -Label "fast audio output" `
            -Target "/data-blocks/$encodedAudio/audio-f32le?channels=0" `
            -OutputPath "NUL"
    $streamClients +=
        Start-StreamClient `
            -Label "slow audio output" `
            -Target "/data-blocks/$encodedAudio/audio-f32le?channels=1" `
            -OutputPath $slowAudioPath `
            -LimitRate "4k"

    $subscribersReady = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        foreach ($entry in $streamClients) {
            if ($entry.Process.HasExited) {
                throw (
                    "$($entry.Label) exited before the soak " +
                    "with code $($entry.Process.ExitCode)")
            }
        }
        $subscribed =
            Invoke-SoakHttp -Method Get -Target "/data-blocks"
        $audioSubscribed =
            Get-Block $subscribed $audioOutput.blockId
        $fftSubscribed =
            Get-Block $subscribed $fftOutput.blockId
        $clickSubscribed =
            Get-Block $subscribed $clickOutput.blockId
        if ($audioSubscribed.stats.subscriberCount -ge
                $audioPressureBefore.stats.subscriberCount + 2 -and
            $fftSubscribed.stats.subscriberCount -ge
                $fftPressureBefore.stats.subscriberCount + 2 -and
            $clickSubscribed.stats.subscriberCount -ge
                $clickPressureBefore.stats.subscriberCount + 2) {
            $subscribersReady = $true
            break
        }
        Start-Sleep -Milliseconds 100
    }
    Assert-True `
        $subscribersReady `
        "Fast and slow FFT/Click/audio subscribers did not all attach"

    $service.Refresh()
    $baselineWorkingSet = $service.WorkingSet64
    $maximumWorkingSet = $baselineWorkingSet
    $maximumIngestMs = 0.0
    $maximumQueuedUnits = 0
    $deadline = [DateTime]::UtcNow.AddSeconds($DurationSeconds)
    [uint64]$pressureStartSample = 1000000000
    [int64]$pressureStartTimeMs = 1800000000000
    $chunks = 0

    # Replay the exact baseline prefix with all six presentation subscribers
    # attached. This is the scientific-isolation assertion.
    for ($chunkIndex = 0;
         $chunkIndex -lt $ComparisonChunks;
         $chunkIndex++) {
        [uint64]$sample =
            $pressureStartSample +
            [uint64]$chunkIndex * [uint64]$FramesPerChunk
        [int64]$time =
            $pressureStartTimeMs +
            [int64][Math]::Floor(
                [double]($sample - $pressureStartSample) *
                1000.0 / $sampleRate)
        $ingestMs =
            Send-ProjectPcm `
                -ProjectId $configured.Json.active.project.projectId `
                -AcquisitionId $acquisition.id `
                -StartSample $sample `
                -TimeMs $time `
                -ExpectedRevision 2 `
                -Pcm $pcm `
                -ExpectedFrames $FramesPerChunk
        $maximumIngestMs =
            [Math]::Max($maximumIngestMs, $ingestMs)
        $chunks++
        $service.Refresh()
        $maximumWorkingSet =
            [Math]::Max(
                $maximumWorkingSet,
                $service.WorkingSet64)
    }

    $pressuredFftUnits = @(
        Get-PhaseUnits `
            -BlockId $fftOutput.blockId `
            -PhaseStartSample $pressureStartSample `
            -PhaseFrameCount $comparisonFrameCount `
            -Context "Pressured FFT replay"
    )
    $pressuredClickUnits = @(
        Get-PhaseUnits `
            -BlockId $clickOutput.blockId `
            -PhaseStartSample $pressureStartSample `
            -PhaseFrameCount $comparisonFrameCount `
            -Context "Pressured Click replay"
    )
    $pressuredFftSignature =
        Get-ScientificSignature `
            -Units $pressuredFftUnits `
            -PhaseStartSample $pressureStartSample `
            -PhaseStartTimeMs $pressureStartTimeMs `
            -NormalizeClickPayload $false `
            -Context "Pressured FFT replay"
    $pressuredClickSignature =
        Get-ScientificSignature `
            -Units $pressuredClickUnits `
            -PhaseStartSample $pressureStartSample `
            -PhaseStartTimeMs $pressureStartTimeMs `
            -NormalizeClickPayload $true `
            -Context "Pressured Click replay"
    Assert-True `
        ($pressuredFftSignature.Count -eq
                $baselineFftSignature.Count -and
            $pressuredFftSignature.Digest -ceq
                $baselineFftSignature.Digest -and
            $pressuredClickSignature.Count -eq
                $baselineClickSignature.Count -and
            $pressuredClickSignature.Digest -ceq
                $baselineClickSignature.Digest) `
        ("Presentation pressure changed scientific output: " +
            "FFT baseline=$($baselineFftSignature.Count)/" +
            "$($baselineFftSignature.Digest) pressured=" +
            "$($pressuredFftSignature.Count)/" +
            "$($pressuredFftSignature.Digest); Click baseline=" +
            "$($baselineClickSignature.Count)/" +
            "$($baselineClickSignature.Digest) pressured=" +
            "$($pressuredClickSignature.Count)/" +
            "$($pressuredClickSignature.Digest)")

    $observed =
        Invoke-SoakHttp -Method Get -Target "/data-blocks"
    Assert-BlockBounds `
        -Response $observed `
        -Context "After deterministic pressured replay"
    foreach ($block in @($observed.Json.dataBlocks)) {
        $maximumQueuedUnits =
            [Math]::Max(
                $maximumQueuedUnits,
                [int64]$block.stats.queuedUnits)
    }

    while ([DateTime]::UtcNow -lt $deadline) {
        [uint64]$startSample =
            $pressureStartSample +
            [uint64]$chunks * [uint64]$FramesPerChunk
        [int64]$timeMs =
            $pressureStartTimeMs +
            [int64][Math]::Floor(
                [double]($startSample - $pressureStartSample) *
                1000.0 / $sampleRate)
        $ingestMs =
            Send-ProjectPcm `
                -ProjectId $configured.Json.active.project.projectId `
                -AcquisitionId $acquisition.id `
                -StartSample $startSample `
                -TimeMs $timeMs `
                -ExpectedRevision 2 `
                -Pcm $pcm `
                -ExpectedFrames $FramesPerChunk
        $maximumIngestMs =
            [Math]::Max(
                $maximumIngestMs,
                $ingestMs)
        $chunks++

        $observed =
            Invoke-SoakHttp -Method Get -Target "/data-blocks"
        Assert-BlockBounds `
            -Response $observed `
            -Context "During project soak"
        foreach ($block in @($observed.Json.dataBlocks)) {
            $maximumQueuedUnits =
                [Math]::Max(
                    $maximumQueuedUnits,
                    [int64]$block.stats.queuedUnits)
        }
        $runtime =
            Invoke-SoakHttp `
                -Method Get `
                -Target "/module-runtime/status"
        Assert-True `
            ($runtime.Json.running -and
                $runtime.Json.authorityMode -eq "project" -and
                @(
                    $runtime.Json.modules |
                        Where-Object { $_.state -eq "error" }
                ).Count -eq 0) `
            "Project runtime left its healthy running state during ingest"

        $service.Refresh()
        $maximumWorkingSet =
            [Math]::Max(
                $maximumWorkingSet,
                $service.WorkingSet64)
    }

    $blocksAfter =
        Invoke-SoakHttp -Method Get -Target "/data-blocks"
    Assert-BlockBounds `
        -Response $blocksAfter `
        -Context "After project soak"
    $audioAfter = Get-Block $blocksAfter $audioOutput.blockId
    $fftAfter = Get-Block $blocksAfter $fftOutput.blockId
    $clickAfter = Get-Block $blocksAfter $clickOutput.blockId
    $workingSetGrowth = $maximumWorkingSet - $baselineWorkingSet
    Assert-True `
        ($chunks -ge 2 -and
            $audioAfter.stats.published -gt
                $audioPressureBefore.stats.published -and
            $fftAfter.stats.published -gt
                $fftPressureBefore.stats.published -and
            $clickAfter.stats.published -gt
                $clickPressureBefore.stats.published -and
            $maximumIngestMs -lt 5000 -and
            $workingSetGrowth -lt 268435456) `
        ("Project soak did not advance bounded scientific output: " +
            "chunks=$chunks audio=$($audioAfter.stats.published) " +
            "fft=$($fftAfter.stats.published) " +
            "clicks=$($clickAfter.stats.published) " +
            "maxIngestMs=$maximumIngestMs " +
            "growthBytes=$workingSetGrowth")

    Stop-StreamClients
    $subscribersDrained = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        $draining =
            Invoke-SoakHttp -Method Get -Target "/data-blocks"
        $audioDraining =
            Get-Block $draining $audioOutput.blockId
        $fftDraining =
            Get-Block $draining $fftOutput.blockId
        $clickDraining =
            Get-Block $draining $clickOutput.blockId
        if ($audioDraining.stats.subscriberCount -eq
                $audioPressureBefore.stats.subscriberCount -and
            $fftDraining.stats.subscriberCount -eq
                $fftPressureBefore.stats.subscriberCount -and
            $clickDraining.stats.subscriberCount -eq
                $clickPressureBefore.stats.subscriberCount) {
            $subscribersDrained = $true
            break
        }
        Start-Sleep -Milliseconds 100
    }
    Assert-True `
        $subscribersDrained `
        "External stream subscribers did not drain after disconnect"
    Assert-True `
        ((Get-Item $slowFftPath).Length -gt 0 -and
            (Get-Item $slowClickPath).Length -gt 0 -and
            (Get-Item $slowAudioPath).Length -gt 0) `
        "One or more deliberately slow subscribers received no data"

    $stopped =
        Invoke-SoakHttp `
            -Method Post `
            -Target "/module-runtime/control" `
            -Body '{"action":"stop"}'
    $stoppedRuntime =
        Invoke-SoakHttp `
            -Method Get `
            -Target "/module-runtime/status"
    $stoppedBlocks =
        Invoke-SoakHttp -Method Get -Target "/data-blocks"
    $undrained = @(
        $stoppedBlocks.Json.dataBlocks |
            Where-Object {
                $_.stats.subscriberCount -ne 0 -or
                $_.stats.queuedUnits -ne 0 -or
                $_.stats.observerErrors -ne 0
            }
    )
    Assert-True `
        ($stopped.Status -eq 200 -and
            -not $stopped.Json.running -and
            -not $stoppedRuntime.Json.running -and
            @(
                $stoppedRuntime.Json.modules |
                    Where-Object { $_.state -eq "error" }
            ).Count -eq 0 -and
            $undrained.Count -eq 0) `
        "Project runtime did not stop and drain cleanly"

    Write-Host (
        "Project runtime soak passed: " +
        "duration=${DurationSeconds}s chunks=$chunks " +
        "frames=$([uint64]$chunks * [uint64]$FramesPerChunk) " +
        "streams=$($streamClients.Count) " +
        "fftPublished=$($fftAfter.stats.published) " +
        "clickPublished=$($clickAfter.stats.published) " +
        "scientificParity=exact " +
        "fftParity=$($pressuredFftSignature.Count)/" +
        "$($pressuredFftSignature.Digest.Substring(0, 16)) " +
        "clickParity=$($pressuredClickSignature.Count)/" +
        "$($pressuredClickSignature.Digest.Substring(0, 16)) " +
        "maxQueued=$maximumQueuedUnits " +
        "maxIngestMs=$([Math]::Round($maximumIngestMs, 1)) " +
        "workingSetGrowthMiB=" +
        "$([Math]::Round($workingSetGrowth / 1MB, 1))")
}
finally {
    Stop-StreamClients
    if ($service -and -not $service.HasExited) {
        Stop-Process `
            -Id $service.Id `
            -Force `
            -ErrorAction SilentlyContinue
        [void]$service.WaitForExit(3000)
    }
    $http.Dispose()
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $oldEnvironment[$name],
            [EnvironmentVariableTarget]::Process)
    }
    $resolvedBase = [System.IO.Path]::GetFullPath($tempBase)
    $resolvedCleanup = [System.IO.Path]::GetFullPath($resolvedRoot)
    if ($resolvedCleanup.StartsWith(
            $resolvedBase,
            [StringComparison]::OrdinalIgnoreCase) -and
        $resolvedCleanup -ne $resolvedBase) {
        Remove-Item `
            -LiteralPath $resolvedCleanup `
            -Recurse `
            -Force
    }
}
