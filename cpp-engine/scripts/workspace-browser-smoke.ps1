param(
    [int]$Port = 18193,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build")
)

$ErrorActionPreference = "Stop"
$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
$browserExe =
    "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path $serviceExe)) {
    throw "Service executable not found: $serviceExe"
}
if (-not (Test-Path $browserExe)) {
    throw "Google Chrome not found: $browserExe"
}

$tempBase = [System.IO.Path]::GetTempPath()
$testRoot = Join-Path $tempBase (
    "pamguard-workspace-browser-" +
    [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$oldGraph = $env:PAMGUARD_MODULE_GRAPH_FILE
$oldLegacyModelCompat = $env:PAMGUARD_LEGACY_MODEL_COMPAT
$oldUi = $env:PAMGUARD_WEB_UI_FILE
$oldWorkspace = $env:PAMGUARD_WORKSPACE_FILE
$service = $null
$workspaceRoot = (
    Resolve-Path (Join-Path $PSScriptRoot "..\..")
).Path
$profile = Join-Path $workspaceRoot (
    ".workspace-browser-profile-" +
    [System.Guid]::NewGuid().ToString("N"))

try {
    $env:PAMGUARD_MODULE_GRAPH_FILE =
        Join-Path $testRoot "module-graph.json"
    $env:PAMGUARD_LEGACY_MODEL_COMPAT = "1"
    $env:PAMGUARD_WEB_UI_FILE = (
        Resolve-Path (Join-Path $PSScriptRoot "..\..\web-ui\legacy-compat.html")
    ).Path
    $env:PAMGUARD_WORKSPACE_FILE =
        Join-Path $testRoot "workspaces.json"
    $service = Start-Process `
        -FilePath $serviceExe `
        -ArgumentList "$Port" `
        -PassThru `
        -WindowStyle Hidden
    $base = "http://127.0.0.1:$Port"
    $healthy = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try {
            $health = Invoke-RestMethod -Method Get -Uri "$base/health"
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
        throw "Workspace browser service did not become healthy"
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
                    sourceId = "browser"
                    sampleRateHz = 48000
                    channelCount = 2
                    subtractDC = $false
                    dcTimeConstantSeconds = 1
                }
            },
            @{
                id = "fft-one"
                typeId = "pamguard.fft"
                name = "Full FFT"
                enabled = $true
                settings = @{
                    fftLength = 64
                    fftHop = 32
                    channels = @(0, 1)
                }
            },
            @{
                id = "decimator"
                typeId = "pamguard.decimator"
                name = "Low-band route"
                enabled = $true
                settings = @{
                    outputSampleRateHz = 12000
                    filter = @{
                        type = "butterworth"
                        band = "lowPass"
                        order = 6
                        lowPassFreqHz = 6000
                        highPassFreqHz = 2000
                        passBandRippleDb = 2
                        stopBandRippleDb = 2
                        chebyGamma = 3
                        arbitraryFrequenciesHz = @()
                        arbitraryGainsDb = @()
                    }
                    interpolation = 0
                    channelBitmap = 4294967295
                }
            },
            @{
                id = "fft-two"
                typeId = "pamguard.fft"
                name = "Low-band FFT"
                enabled = $true
                settings = @{
                    fftLength = 128
                    fftHop = 64
                    channels = @(0, 1)
                }
            },
            @{
                id = "clicks"
                typeId = "pamguard.click-detector"
                name = "Clicks"
                enabled = $true
                settings = @{
                    channelBitmap = 3
                    triggerBitmap = 3
                    minTriggerChannels = 1
                    thresholdDb = 10
                    longFilter = 0.00001
                    shortFilter = 0.1
                    preSample = 10
                    postSample = 12
                    minSep = 8
                    maxLength = 128
                }
            },
            @{
                id = "levels"
                typeId = "pamguard.level-meter"
                name = "Levels"
                enabled = $true
                settings = @{
                    intervalSeconds = 0.25
                    channelBitmap = 3
                }
            }
        )
        connections = @(
            @{
                id = "fft-input"
                source = @{ moduleId = "source"; portId = "audio" }
                target = @{ moduleId = "fft-one"; portId = "input" }
            },
            @{
                id = "click-input"
                source = @{ moduleId = "source"; portId = "audio" }
                target = @{ moduleId = "clicks"; portId = "input" }
            },
            @{
                id = "decimator-input"
                source = @{ moduleId = "source"; portId = "audio" }
                target = @{ moduleId = "decimator"; portId = "input" }
            },
            @{
                id = "fft-two-input"
                source = @{ moduleId = "decimator"; portId = "output" }
                target = @{ moduleId = "fft-two"; portId = "input" }
            },
            @{
                id = "levels-input"
                source = @{ moduleId = "source"; portId = "audio" }
                target = @{ moduleId = "levels"; portId = "input" }
            }
        )
    } | ConvertTo-Json -Depth 12 -Compress
    Invoke-RestMethod `
        -Method Put `
        -Uri "$base/module-graph" `
        -ContentType "application/json" `
        -Body $graph | Out-Null
    $layout = @{
        schemaVersion = 1
        name = "Watch"
        synchronizedTime = $true
        arrangement = "tabs"
        audio = @{
            sourceBlockId = "block:source:audio"
            channels = "0"
            gain = 1
        }
        displays = @(
            @{
                id = "spectrogram-1"
                type = "spectrogram"
                name = "Full band"
                sourceBlockId = "block:fft-one:fft"
                channel = 0
            },
            @{
                id = "spectrogram-2"
                type = "spectrogram"
                name = "Decimated low band"
                sourceBlockId = "block:fft-two:fft"
                channel = 1
                minimumFrequencyHz = 0
                maximumFrequencyHz = 6000
                syncGroup = "watch"
            },
            @{
                id = "events-3"
                type = "events"
                name = "Clicks"
                sourceBlockId = "block:clicks:clicks"
                channel = 0
            },
            @{
                id = "waveform-lost"
                type = "waveform"
                name = "Disconnected source"
                sourceBlockId = "block:removed:audio"
                channel = 0
            },
            @{
                id = "levels-5"
                type = "level"
                name = "Typed levels"
                sourceBlockId = "block:levels:levels"
                channel = 0
            },
            @{
                id = "status-6"
                type = "status"
                name = "Module status"
            },
            @{
                id = "datamap-7"
                type = "datamap"
                name = "Data map"
                syncGroup = "watch"
            }
        )
    } | ConvertTo-Json -Depth 10 -Compress
    $invalidLayout = $layout | ConvertFrom-Json
    $invalidLayout.displays[1].id =
        $invalidLayout.displays[0].id
    $invalidWorkspaceStatus = 0
    try {
        Invoke-WebRequest `
            -Method Put `
            -Uri "$base/workspaces/invalid" `
            -ContentType "application/json" `
            -Body ($invalidLayout |
                ConvertTo-Json -Depth 10 -Compress) |
            Out-Null
    }
    catch {
        $invalidWorkspaceStatus =
            [int]$_.Exception.Response.StatusCode
    }
    if ($invalidWorkspaceStatus -ne 400) {
        throw "Duplicate workspace display IDs returned HTTP $invalidWorkspaceStatus instead of 400"
    }
    $saved = Invoke-RestMethod `
        -Method Put `
        -Uri "$base/workspaces/watch" `
        -ContentType "application/json" `
        -Body $layout
    Stop-Process -Id $service.Id -Force
    $service.WaitForExit()
    $service = Start-Process `
        -FilePath $serviceExe `
        -ArgumentList "$Port" `
        -PassThru `
        -WindowStyle Hidden
    $healthy = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try {
            $health = Invoke-RestMethod -Method Get -Uri "$base/health"
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
        throw "Workspace service did not recover after persistence restart"
    }
    $listed = Invoke-RestMethod -Method Get -Uri "$base/workspaces"
    $loaded = Invoke-RestMethod -Method Get -Uri "$base/workspaces/watch"
    if (-not $saved.saved -or -not $saved.persistent -or
        $listed.count -ne 1 -or
        $loaded.name -ne "Watch" -or
        @($loaded.displays).Count -ne 7 -or
        -not (Test-Path $env:PAMGUARD_WORKSPACE_FILE)) {
        throw "Workspace persistence API did not survive restart with stable layout identities"
    }
    $runtime = Invoke-RestMethod `
        -Method Post `
        -Uri "$base/module-runtime/control" `
        -ContentType "application/json" `
        -Body '{"action":"start"}'
    if (-not $runtime.running) {
        throw "Persisted workspace fixture graph did not start explicitly"
    }

    $pcmPath = Join-Path $testRoot "workspace-audio.f32le"
    $pcm = New-Object byte[] (12000 * 2 * 4)
    for ($frame = 0; $frame -lt 12000; $frame++) {
        for ($channel = 0; $channel -lt 2; $channel++) {
            $sample = [single](0.1 * [Math]::Sin(
                2 * [Math]::PI * (400 + 200 * $channel) *
                $frame / 48000))
            [BitConverter]::GetBytes($sample).CopyTo(
                $pcm,
                (($frame * 2 + $channel) * 4))
        }
    }
    [System.IO.File]::WriteAllBytes($pcmPath, $pcm)
    $ingested = Invoke-RestMethod `
        -Method Post `
        -Uri "$base/module-runtime/acquisitions/source/pcm-f32le?startSample=0&timeMs=1000" `
        -ContentType "application/octet-stream" `
        -InFile $pcmPath
    if (-not $ingested.accepted -or $ingested.inputFrames -ne 12000) {
        throw "Workspace fixture audio was not accepted by the graph acquisition"
    }

    New-Item -ItemType Directory -Path $profile | Out-Null
    $userDataArgument = "--user-data-dir=$profile"
    $domPath = Join-Path $testRoot "workspace-dom.html"
    $browserErrorPath = Join-Path $testRoot "browser.stderr"
    $browser = Start-Process `
        -FilePath $browserExe `
        -ArgumentList @(
            "--headless=new",
            "--disable-gpu",
            "--no-first-run",
            $userDataArgument,
            "--virtual-time-budget=4000",
            "--dump-dom",
            "$base/") `
        -RedirectStandardOutput $domPath `
        -RedirectStandardError $browserErrorPath `
        -PassThru `
        -WindowStyle Hidden
    $browser.WaitForExit()
    for ($attempt = 0; $attempt -lt 30; $attempt++) {
        if ((Test-Path $domPath) -and
            (Get-Item $domPath).Length -gt 0) {
            break
        }
        Start-Sleep -Milliseconds 100
    }
    $joined = if (Test-Path $domPath) {
        Get-Content $domPath -Raw
    }
    else {
        ""
    }
    $checks = @(
        'value="spectrogram"',
        'value="events"',
        'value="waveform"',
        'value="level"',
        'value="timeplot"',
        'value="status"',
        'value="datamap"',
        'id="workspaceArrangement"',
        'id="operatorTabs"',
        'id="graphModuleSearch"',
        'id="graphApply"',
        'id="graphCanvas"',
        'id="graphSettingsDialog"',
        'id="graphOperatorInput"',
        'id="graphOperatorSubmit"',
        'id="captureTarget"',
        'value="module:source"',
        'data-type-id="pamguard.acquisition"',
        'data-type-id="pamguard.fft"',
        'data-type-id="pamguard.effort-monitor"',
        'data-type-id="pamguard.clip-generator"',
        'graph-palette-item',
        'graph-node',
        'graph-connection',
        'block:fft-one:fft',
        'block:fft-two:fft',
        'block:clicks:clicks',
        'block:levels:levels',
        'Missing source · block:removed:audio',
        'Error: stream unavailable (404)',
        'typed level',
        'Open this module in the graph editor',
        'Navigate synchronized displays to',
        'value="watch"',
        'operator-display'
    )
    foreach ($check in $checks) {
        if (-not $joined.Contains($check)) {
            Write-Host $joined.Substring(
                0,
                [Math]::Min(1200, $joined.Length))
            throw "Browser DOM missing $check"
        }
    }
    Write-Host (
        "Workspace browser smoke passed: visual graph editor, 7 providers, " +
        "two independent spectrograms, click display, source-loss state, " +
        "split/tabbed workspace controls, and restart-persisted layout")
}
finally {
    if ($service -and -not $service.HasExited) {
        Stop-Process -Id $service.Id -Force
    }
    if ($null -ne $oldGraph) {
        $env:PAMGUARD_MODULE_GRAPH_FILE = $oldGraph
    }
    else {
        Remove-Item Env:\PAMGUARD_MODULE_GRAPH_FILE `
            -ErrorAction SilentlyContinue
    }
    if ($null -ne $oldLegacyModelCompat) {
        $env:PAMGUARD_LEGACY_MODEL_COMPAT =
            $oldLegacyModelCompat
    }
    else {
        Remove-Item Env:\PAMGUARD_LEGACY_MODEL_COMPAT `
            -ErrorAction SilentlyContinue
    }
    if ($null -ne $oldUi) {
        $env:PAMGUARD_WEB_UI_FILE = $oldUi
    }
    else {
        Remove-Item Env:\PAMGUARD_WEB_UI_FILE `
            -ErrorAction SilentlyContinue
    }
    if ($null -ne $oldWorkspace) {
        $env:PAMGUARD_WORKSPACE_FILE = $oldWorkspace
    }
    else {
        Remove-Item Env:\PAMGUARD_WORKSPACE_FILE `
            -ErrorAction SilentlyContinue
    }
    $resolvedRoot = [System.IO.Path]::GetFullPath($testRoot)
    $resolvedBase = [System.IO.Path]::GetFullPath($tempBase)
    if ($resolvedRoot.StartsWith(
            $resolvedBase,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        $resolvedRoot -ne $resolvedBase) {
        Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
    }
    $resolvedProfile = [System.IO.Path]::GetFullPath($profile)
    $resolvedWorkspace = [System.IO.Path]::GetFullPath($workspaceRoot)
    if ($resolvedProfile.StartsWith(
            $resolvedWorkspace,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        $resolvedProfile -ne $resolvedWorkspace) {
        Get-CimInstance Win32_Process |
            Where-Object {
                $_.CommandLine -and
                $_.CommandLine.IndexOf(
                    $resolvedProfile,
                    [System.StringComparison]::OrdinalIgnoreCase) -ge 0
            } |
            ForEach-Object {
                Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
            }
        Start-Sleep -Milliseconds 100
        if (Test-Path -LiteralPath $resolvedProfile) {
            Remove-Item -LiteralPath $resolvedProfile -Recurse -Force
        }
    }
}
