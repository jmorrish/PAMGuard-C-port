param(
    [int]$Port = 18201,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build")
)

$ErrorActionPreference = "Stop"

$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
if (-not (Test-Path -LiteralPath $serviceExe)) {
    $serviceExe = Join-Path $BuildDir "Release\pamguard_engine_service.exe"
}
if (-not (Test-Path -LiteralPath $serviceExe)) {
    throw "Service executable not found under $BuildDir"
}

$tempBase = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::GetTempPath()
)
$testRoot = Join-Path $tempBase (
    "pamguard-project-service-" +
    [System.Guid]::NewGuid().ToString("N")
)
$projectDir = Join-Path $testRoot "projects"
New-Item -ItemType Directory -Path $projectDir | Out-Null
$resolvedTestRoot = [System.IO.Path]::GetFullPath($testRoot)
$resolvedProjectDir = [System.IO.Path]::GetFullPath($projectDir)

$environmentNames = @(
    "PAMGUARD_ACTIVE_PROJECT_ID",
    "PAMGUARD_API_KEY",
    "PAMGUARD_API_KEY_FILE",
    "PAMGUARD_CAPTURE_ENABLED",
    "PAMGUARD_CORS_ORIGIN",
    "PAMGUARD_LEGACY_MODEL_COMPAT",
    "PAMGUARD_MODULE_GRAPH_FILE",
    "PAMGUARD_PROJECT_DIR",
    "PAMGUARD_SESSION_CONFIG_DIR",
    "PAMGUARD_WEB_ASSET_DIR",
    "PAMGUARD_WEB_UI_FILE",
    "PAMGUARD_WORKSPACE_FILE"
)
$oldEnvironment = @{}
foreach ($name in $environmentNames) {
    $oldEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
    [Environment]::SetEnvironmentVariable($name, $null, "Process")
}

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Context
    )

    if (-not $Condition) {
        throw $Context
    }
}

function Assert-Status {
    param(
        [Parameter(Mandatory = $true)]$Response,
        [Parameter(Mandatory = $true)][int]$Expected,
        [Parameter(Mandatory = $true)][string]$Context
    )

    if ($Response.Status -ne $Expected) {
        throw (
            "$Context returned HTTP $($Response.Status), expected " +
            "$Expected. Body: $($Response.Body)"
        )
    }
}

function Assert-JsonResponse {
    param(
        [Parameter(Mandatory = $true)]$Response,
        [Parameter(Mandatory = $true)][string]$Context
    )

    if ($null -eq $Response.Json) {
        throw "$Context did not return a JSON document. Body: $($Response.Body)"
    }
}

function Assert-StrongProjectEtag {
    param(
        [Parameter(Mandatory = $true)][string]$Etag,
        [Parameter(Mandatory = $true)][string]$Context
    )

    if ($Etag -cnotmatch '^"pgp1-[A-Za-z0-9_-]{43}"$') {
        throw "$Context returned an invalid strong project ETag: $Etag"
    }
}

function Get-ResponseEtag {
    param(
        [Parameter(Mandatory = $true)]$Response,
        [Parameter(Mandatory = $true)][string]$Context
    )

    if (-not $Response.Headers.ContainsKey("ETag")) {
        throw "$Context omitted the ETag response header"
    }
    $etag = [string]$Response.Headers["ETag"]
    Assert-StrongProjectEtag -Etag $etag -Context $Context
    return $etag
}

function ConvertTo-CompactJson {
    param([Parameter(Mandatory = $true)]$Value)

    return ($Value | ConvertTo-Json -Depth 64 -Compress)
}

function Invoke-LocalHttp {
    param(
        [Parameter(Mandatory = $true)][string]$Method,
        [Parameter(Mandatory = $true)][string]$Target,
        [AllowNull()][string]$Body = $null,
        [hashtable]$Headers = @{}
    )

    if (-not $Target.StartsWith("/") -or
        $Target.Contains("`r") -or
        $Target.Contains("`n")) {
        throw "Invalid local HTTP target: $Target"
    }

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect("127.0.0.1", $Port)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 15000
        $stream.WriteTimeout = 15000

        $encodedBody = if ($null -eq $Body) {
            [byte[]]::new(0)
        }
        else {
            [System.Text.Encoding]::UTF8.GetBytes($Body)
        }

        $request = [System.Text.StringBuilder]::new()
        [void]$request.Append(
            "$($Method.ToUpperInvariant()) $Target HTTP/1.1`r`n"
        )
        [void]$request.Append("Host: 127.0.0.1:$Port`r`n")
        [void]$request.Append("Connection: close`r`n")
        [void]$request.Append("Accept: application/json`r`n")
        if ($null -ne $Body) {
            [void]$request.Append(
                "Content-Type: application/json; charset=utf-8`r`n"
            )
        }
        foreach ($name in $Headers.Keys) {
            $value = [string]$Headers[$name]
            if ($name.Contains("`r") -or $name.Contains("`n") -or
                $value.Contains("`r") -or $value.Contains("`n")) {
                throw "Invalid HTTP request header"
            }
            [void]$request.Append("$name`: $value`r`n")
        }
        [void]$request.Append(
            "Content-Length: $($encodedBody.Length)`r`n`r`n"
        )

        $encodedHeaders = [System.Text.Encoding]::ASCII.GetBytes(
            $request.ToString()
        )
        $stream.Write($encodedHeaders, 0, $encodedHeaders.Length)
        if ($encodedBody.Length -gt 0) {
            $stream.Write($encodedBody, 0, $encodedBody.Length)
        }
        $stream.Flush()

        $encodedResponse = [System.IO.MemoryStream]::new()
        try {
            $buffer = [byte[]]::new(8192)
            while (($count = $stream.Read(
                        $buffer,
                        0,
                        $buffer.Length
                    )) -gt 0) {
                $encodedResponse.Write($buffer, 0, $count)
            }
            $responseText = [System.Text.Encoding]::UTF8.GetString(
                $encodedResponse.ToArray()
            )
        }
        finally {
            $encodedResponse.Dispose()
        }

        $headerEnd = $responseText.IndexOf("`r`n`r`n")
        if ($headerEnd -lt 0) {
            throw "Malformed HTTP response for $Method $Target"
        }
        $headerLines = $responseText.Substring(0, $headerEnd).Split(
            @("`r`n"),
            [System.StringSplitOptions]::None
        )
        $statusMatch = [regex]::Match(
            $headerLines[0],
            '^HTTP/\d\.\d\s+(\d{3})(?:\s|$)'
        )
        if (-not $statusMatch.Success) {
            throw (
                "Malformed HTTP status for $Method $Target`: " +
                $headerLines[0]
            )
        }

        $responseHeaders = @{}
        foreach ($line in $headerLines | Select-Object -Skip 1) {
            $separator = $line.IndexOf(":")
            if ($separator -le 0) {
                continue
            }
            $name = $line.Substring(0, $separator).Trim()
            $value = $line.Substring($separator + 1).Trim()
            if ($responseHeaders.ContainsKey($name)) {
                $responseHeaders[$name] = (
                    [string]$responseHeaders[$name] + ", " + $value
                )
            }
            else {
                $responseHeaders[$name] = $value
            }
        }

        $responseBody = $responseText.Substring($headerEnd + 4)
        $json = $null
        if (-not [string]::IsNullOrWhiteSpace($responseBody)) {
            try {
                $json = $responseBody | ConvertFrom-Json
            }
            catch {
                $contentType = if (
                    $responseHeaders.ContainsKey("Content-Type")
                ) {
                    [string]$responseHeaders["Content-Type"]
                }
                else {
                    ""
                }
                if ($contentType -like "application/json*") {
                    throw (
                        "Invalid JSON response for $Method $Target`: " +
                        $responseBody
                    )
                }
            }
        }

        return [pscustomobject]@{
            Status = [int]$statusMatch.Groups[1].Value
            Headers = $responseHeaders
            Body = $responseBody
            Json = $json
        }
    }
    finally {
        $client.Dispose()
    }
}

function Assert-PortAvailable {
    $listener = [System.Net.Sockets.TcpListener]::new(
        [System.Net.IPAddress]::Loopback,
        $Port
    )
    try {
        $listener.Start()
    }
    catch {
        throw "TCP port $Port is already in use"
    }
    finally {
        $listener.Stop()
    }
}

function Start-TestService {
    param([Parameter(Mandatory = $true)][string]$Label)

    $stdout = Join-Path $testRoot "$Label.stdout.log"
    $stderr = Join-Path $testRoot "$Label.stderr.log"
    $process = Start-Process `
        -FilePath $serviceExe `
        -ArgumentList "$Port" `
        -PassThru `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr

    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($process.HasExited) {
            $detail = if (Test-Path -LiteralPath $stderr) {
                [System.IO.File]::ReadAllText($stderr)
            }
            else {
                ""
            }
            throw (
                "Service exited during $Label startup with code " +
                "$($process.ExitCode): $detail"
            )
        }
        try {
            $health = Invoke-LocalHttp -Method Get -Target "/health"
            if ($health.Status -eq 200 -and $health.Json.ok) {
                return [pscustomobject]@{
                    Process = $process
                    Stdout = $stdout
                    Stderr = $stderr
                }
            }
        }
        catch {
            Start-Sleep -Milliseconds 100
        }
    }

    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    Wait-Process -Id $process.Id -ErrorAction SilentlyContinue
    $detail = if (Test-Path -LiteralPath $stderr) {
        [System.IO.File]::ReadAllText($stderr)
    }
    else {
        ""
    }
    throw "Service did not become healthy for $Label. $detail"
}

function Stop-TestService {
    param($Handle)

    if ($null -ne $Handle -and
        $null -ne $Handle.Process -and
        -not $Handle.Process.HasExited) {
        Stop-Process -Id $Handle.Process.Id -Force
        Wait-Process `
            -Id $Handle.Process.Id `
            -ErrorAction SilentlyContinue
    }
}

function Get-ServiceLogText {
    param($Handle)

    if ($null -eq $Handle) {
        return ""
    }
    $parts = @()
    foreach ($path in @($Handle.Stdout, $Handle.Stderr)) {
        if ($path -and (Test-Path -LiteralPath $path)) {
            $text = [System.IO.File]::ReadAllText($path)
            if (-not [string]::IsNullOrWhiteSpace($text)) {
                $parts += $text.Trim()
            }
        }
    }
    return ($parts -join "`n")
}

function New-MutationBody {
    param(
        [Parameter(Mandatory = $true)][bool]$ValidateOnly,
        [Parameter(Mandatory = $true)][array]$Operations
    )

    return ConvertTo-CompactJson @{
        schemaVersion = 1
        validateOnly = $ValidateOnly
        operations = @($Operations)
    }
}

function Invoke-ProjectMutation {
    param(
        [Parameter(Mandatory = $true)][string]$Etag,
        [Parameter(Mandatory = $true)][bool]$ValidateOnly,
        [Parameter(Mandatory = $true)][array]$Operations
    )

    return Invoke-LocalHttp `
        -Method Post `
        -Target "/v1/projects/active/mutations" `
        -Headers @{ "If-Match" = $Etag } `
        -Body (New-MutationBody `
            -ValidateOnly $ValidateOnly `
            -Operations $Operations)
}

function Assert-StringSet {
    param(
        [Parameter(Mandatory = $true)][array]$Actual,
        [Parameter(Mandatory = $true)][array]$Expected,
        [Parameter(Mandatory = $true)][string]$Context
    )

    $difference = @(
        Compare-Object `
            -ReferenceObject @($Expected | Sort-Object) `
            -DifferenceObject @($Actual | Sort-Object)
    )
    if ($difference.Count -ne 0) {
        throw (
            "$Context differed. Actual: " +
            (@($Actual | Sort-Object) -join ", ")
        )
    }
}

function Assert-ClickMonitoringTemplateResult {
    param(
        [Parameter(Mandatory = $true)]$Response,
        [Parameter(Mandatory = $true)][string]$ClientRef,
        [Parameter(Mandatory = $true)][string]$Context
    )

    Assert-Status $Response 200 $Context
    Assert-JsonResponse $Response $Context
    $createdByRef = @{}
    foreach ($entity in @($Response.Json.createdEntities)) {
        $createdByRef[[string]$entity.clientRef] =
            [string]$entity.id
    }
    $expectedRefs = @(
        "$ClientRef`:acquisition",
        "$ClientRef`:fft",
        "$ClientRef`:userDisplay",
        "$ClientRef`:clickDetector",
        "$ClientRef`:soundOutput"
    )
    Assert-StringSet `
        -Actual @($createdByRef.Keys) `
        -Expected $expectedRefs `
        -Context "$Context created entities"

    $ids = @{
        Acquisition = $createdByRef["$ClientRef`:acquisition"]
        Fft = $createdByRef["$ClientRef`:fft"]
        UserDisplay = $createdByRef["$ClientRef`:userDisplay"]
        ClickDetector = $createdByRef["$ClientRef`:clickDetector"]
        SoundOutput = $createdByRef["$ClientRef`:soundOutput"]
    }
    $units = @($Response.Json.active.project.controlledUnits)
    $acquisition = @(
        $units |
            Where-Object { $_.id -ceq $ids.Acquisition }
    )[0]
    $fft = @(
        $units |
            Where-Object { $_.id -ceq $ids.Fft }
    )[0]
    $userDisplay = @(
        $units |
            Where-Object { $_.id -ceq $ids.UserDisplay }
    )[0]
    $clickDetector = @(
        $units |
            Where-Object { $_.id -ceq $ids.ClickDetector }
    )[0]
    $soundOutput = @(
        $units |
            Where-Object { $_.id -ceq $ids.SoundOutput }
    )[0]
    $fftBinding = @(
        $fft.bindings |
            Where-Object { $_.inputRole -eq "rawAudio" }
    )[0]
    $clickBinding = @(
        $clickDetector.bindings |
            Where-Object { $_.inputRole -eq "rawAudio" }
    )[0]
    $soundBinding = @(
        $soundOutput.bindings |
            Where-Object { $_.inputRole -eq "audio" }
    )[0]
    $tabs = @($Response.Json.active.project.displayTabs)
    $spectrogramTab = @(
        $tabs |
            Where-Object {
                $_.owner.unitId -ceq $ids.UserDisplay -and
                $_.owner.role -eq "main"
            }
    )[0]
    $clickTab = @(
        $tabs |
            Where-Object {
                $_.owner.unitId -ceq $ids.ClickDetector -and
                $_.owner.role -eq "clickDisplay"
            }
    )[0]
    $spectrogram = @($spectrogramTab.displays)[0]
    $clickDisplay = @($clickTab.displays)[0]

    Assert-True `
        ($Response.Json.changed -and
            @($createdByRef.Values | Select-Object -Unique).Count -eq 5 -and
            $units.Count -eq 5 -and
            $acquisition.typeId -eq "pamguard.acquisition" -and
            $fft.typeId -eq "pamguard.fft" -and
            $userDisplay.typeId -eq "pamguard.user-display" -and
            $clickDetector.typeId -eq "pamguard.click-detector" -and
            $soundOutput.typeId -eq "pamguard.sound-output" -and
            $soundOutput.settings.channelBitmap -eq 0 -and
            $acquisition.settings.PSObject.Properties.Name -notcontains
                "sourceId" -and
            $Response.Json.active.projection.status -eq
                "needs-configuration") `
        "$Context did not create five independent default units"
    Assert-True `
        (@($fftBinding.sources).Count -eq 1 -and
            $fftBinding.sources[0].unitId -ceq $ids.Acquisition -and
            $fftBinding.sources[0].outputRole -eq "rawAudio" -and
            @($clickBinding.sources).Count -eq 1 -and
            $clickBinding.sources[0].unitId -ceq $ids.Acquisition -and
            $clickBinding.sources[0].outputRole -eq "rawAudio" -and
            @($soundBinding.sources).Count -eq 1 -and
            $soundBinding.sources[0].unitId -ceq $ids.Acquisition -and
            $soundBinding.sources[0].outputRole -eq "rawAudio") `
        "$Context did not bind all processing branches to Acquisition"
    Assert-True `
        ($tabs.Count -eq 2 -and
            @($spectrogramTab.displays).Count -eq 1 -and
            $spectrogram.providerTypeId -eq
                "pamguard.spectrogram-display" -and
            $spectrogram.owner.unitId -ceq $ids.UserDisplay -and
            $spectrogram.owner.role -eq "provider" -and
            $spectrogram.source.unitId -ceq $ids.Fft -and
            $spectrogram.source.outputRole -eq "fft" -and
            @($clickTab.displays).Count -eq 1 -and
            $clickDisplay.providerTypeId -eq
                "pamguard.click-display" -and
            $clickDisplay.owner.unitId -ceq $ids.ClickDetector -and
            $clickDisplay.owner.role -eq "clickDisplay" -and
            $clickDisplay.source.unitId -ceq $ids.ClickDetector -and
            $clickDisplay.source.outputRole -eq "clicks") `
        "$Context display ownership or source binding changed"
    return $ids
}

$service = $null
$firstService = $null
$secondService = $null
$corsOrigin = "https://operator-smoke.invalid"

try {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $null, "Process")
    }
    $env:PAMGUARD_PROJECT_DIR = $resolvedProjectDir
    $env:PAMGUARD_CORS_ORIGIN = $corsOrigin
    Assert-PortAvailable

    $service = Start-TestService -Label "cold"
    $firstService = $service
    $baseHeaders = @{ "Origin" = $corsOrigin }

    $catalogue = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/controlled-unit-types" `
        -Headers $baseHeaders
    Assert-Status $catalogue 200 "Controlled-unit catalogue"
    Assert-JsonResponse $catalogue "Controlled-unit catalogue"
    Assert-True `
        ($catalogue.Json.schemaVersion -eq 1) `
        "Controlled-unit catalogue schema version changed"
    Assert-True `
        ($catalogue.Json.descriptorSet.id -eq "pamguard-2.02.18e" -and
            $catalogue.Json.descriptorSet.version -eq 1 -and
            $catalogue.Json.descriptorSet.authorityCommit -eq
                "dca55c81ef6f1498a8a3b926c69e7182afb915ee") `
        "Controlled-unit catalogue lost its Java authority pin"
    Assert-StringSet `
        -Actual @(
            @($catalogue.Json.controlledUnitTypes) |
                ForEach-Object { $_.typeId }
        ) `
        -Expected @(
            "pamguard.acquisition",
            "pamguard.amplifier",
            "pamguard.click-detector",
            "pamguard.decimator",
            "pamguard.fft",
            "pamguard.fft-noise-monitor",
            "pamguard.filter",
            "pamguard.ishmael-energy-sum",
            "pamguard.ishmael-match-filter",
            "pamguard.ishmael-sgram-corr",
            "pamguard.level-meter",
            "pamguard.ltsa",
            "pamguard.matched-template-classifier",
            "pamguard.mht-click-train",
            "pamguard.noise-band-monitor",
            "pamguard.patch-panel",
            "pamguard.sound-recorder",
            "pamguard.clip-generator",
            "pamguard.sound-output",
            "pamguard.user-input",
            "pamguard.aural-listening",
            "pamguard.alarm-event-counter",
            "pamguard.effort-monitor",
            "pamguard.user-display",
            "pamguard.whistles-moans"
        ) `
        -Context "Controlled-unit catalogue"
    $experimentalUtilities = [ordered]@{
        "pamguard.user-input" = "User input"
        "pamguard.aural-listening" = "Aural Listening Form"
        "pamguard.alarm-event-counter" = "Alarm"
        "pamguard.effort-monitor" = "Scroll Effort"
    }
    foreach ($entry in $experimentalUtilities.GetEnumerator()) {
        $descriptor = @(
            $catalogue.Json.controlledUnitTypes |
                Where-Object { $_.typeId -eq $entry.Key }
        )
        Assert-True `
            ($descriptor.Count -eq 1 -and
                $descriptor[0].palette.registeredName -eq $entry.Value -and
                $descriptor[0].palette.menuGroup -eq "Utilities" -and
                $descriptor[0].status.availability -eq "unavailable" -and
                $descriptor[0].status.parity -eq "experimental") `
            "$($entry.Value) catalogue boundary changed"
    }
    Assert-StringSet `
        -Actual @(
            @($catalogue.Json.displayProviderTypes) |
                ForEach-Object { $_.providerTypeId }
        ) `
        -Expected @(
            "pamguard.click-display",
            "pamguard.level-meter-display",
            "pamguard.spectrogram-display"
        ) `
        -Context "Display-provider catalogue"
    Assert-StringSet `
        -Actual @(
            @($catalogue.Json.globalSettingsTypes) |
                ForEach-Object { $_.typeId }
        ) `
        -Expected @("pamguard.array-manager") `
        -Context "Global-settings catalogue"
    $arrayManager = @(
        $catalogue.Json.globalSettingsTypes |
            Where-Object {
                $_.typeId -eq "pamguard.array-manager"
            }
    )[0]
    Assert-True `
        ($null -ne $arrayManager -and
            $arrayManager.required -and
            $arrayManager.adapterId -eq
                "pamguard.array-manager-settings.v1" -and
            $arrayManager.settings.version -eq 1 -and
            @($arrayManager.settings.defaults.streamers).Count -eq 1 -and
            @($arrayManager.settings.defaults.hydrophones).Count -eq 2) `
        "Array Manager descriptor/default geometry is absent"
    $spectrogramProvider = @(
        $catalogue.Json.displayProviderTypes |
            Where-Object {
                $_.providerTypeId -eq
                    "pamguard.spectrogram-display"
            }
    )[0]
    Assert-True `
        ($null -ne $spectrogramProvider -and
            $spectrogramProvider.settings.version -eq 2 -and
            $null -ne $spectrogramProvider.settings.defaults) `
        "Spectrogram provider settings/defaults are absent"

    $blank = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/projects/active" `
        -Headers $baseHeaders
    Assert-Status $blank 200 "Blank active project"
    Assert-JsonResponse $blank "Blank active project"
    $blankEtag = Get-ResponseEtag $blank "Blank active project"
    Assert-True `
        ($blank.Json.etag -ceq $blankEtag) `
        "Blank project body/header ETags differ"
    Assert-True `
        ($blank.Json.workingRevision -eq 0 -and
            $blank.Json.authorityRevision -eq 0 -and
            $blank.Json.dirty -and
            @($blank.Json.project.controlledUnits).Count -eq 0 -and
            @($blank.Json.project.globalSettings.components).Count -eq 1 -and
            $blank.Json.project.globalSettings.components[0].typeId -eq
                "pamguard.array-manager" -and
            @($blank.Json.project.globalSettings.components[0].
                settings.hydrophones).Count -eq 2 -and
            @($blank.Json.project.displayTabs).Count -eq 0 -and
            $blank.Json.projection.status -eq "runnable") `
        "Cold service did not expose one blank, runnable, dirty project"
    Assert-True `
        ($blank.Headers["Access-Control-Allow-Origin"] -ceq
            $corsOrigin -and
            $blank.Headers["Access-Control-Expose-Headers"] -match
                '(^|,\s*)ETag(\s*,|$)' -and
            $blank.Headers["Access-Control-Allow-Headers"] -match
                '(^|,\s*)If-Match(\s*,|$)') `
        "Project CORS headers do not expose/allow ETag concurrency"

    $preflight = Invoke-LocalHttp `
        -Method Options `
        -Target "/v1/projects/active/mutations" `
        -Headers @{
            "Origin" = $corsOrigin
            "Access-Control-Request-Method" = "POST"
            "Access-Control-Request-Headers" =
                "If-Match, Content-Type"
        }
    Assert-Status $preflight 204 "Project mutation CORS preflight"
    Assert-True `
        (@(
                [string]$preflight.Headers[
                    "Access-Control-Allow-Origin"
                ] -split ',\s*'
            ) -ccontains $corsOrigin -and
            $preflight.Headers["Access-Control-Allow-Headers"] -match
                '(^|,\s*)If-Match(\s*,|$)' -and
            $preflight.Headers["Access-Control-Expose-Headers"] -match
                '(^|,\s*)ETag(\s*,|$)') `
        "Project mutation preflight omitted the ETag CORS contract"

    $templateOperation = @{
        op = "addConfigurationTemplate"
        clientRef = "clickTemplate"
        templateId = "pamguard.click-monitoring"
    }
    $templatePreview = Invoke-ProjectMutation `
        -Etag $blankEtag `
        -ValidateOnly $true `
        -Operations @($templateOperation)
    $templatePreviewIds =
        Assert-ClickMonitoringTemplateResult `
            -Response $templatePreview `
            -ClientRef "clickTemplate" `
            -Context "Click monitoring template preview"
    Assert-True `
        ($templatePreview.Json.validatedOnly -and
            (Get-ResponseEtag `
                $templatePreview `
                "Click monitoring template preview") -ceq
                    $blankEtag) `
        "Template preview did not retain the active ETag"
    $afterTemplatePreview = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/projects/active"
    Assert-True `
        ($afterTemplatePreview.Json.etag -ceq $blankEtag -and
            @($afterTemplatePreview.Json.project.controlledUnits).
                Count -eq 0) `
        "Template preview changed active project authority"

    $templateCommit = Invoke-ProjectMutation `
        -Etag $blankEtag `
        -ValidateOnly $false `
        -Operations @($templateOperation)
    $templateCommitIds =
        Assert-ClickMonitoringTemplateResult `
            -Response $templateCommit `
            -ClientRef "clickTemplate" `
            -Context "Click monitoring template commit"
    $templateEtag = Get-ResponseEtag `
        $templateCommit `
        "Click monitoring template commit"
    Assert-True `
        (-not $templateCommit.Json.validatedOnly -and
            $templateEtag -cne $blankEtag -and
            $templateCommit.Json.active.workingRevision -eq 1 -and
            $templateCommit.Json.active.authorityRevision -eq 1) `
        "Template commit was not one atomic project revision"
    $templateAcquisitions = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/projects/active/acquisitions"
    Assert-Status `
        $templateAcquisitions `
        200 `
        "Template Acquisition inventory"
    Assert-True `
        (@($templateAcquisitions.Json.acquisitions).Count -eq 1 -and
            $templateAcquisitions.Json.acquisitions[0].unitId -ceq
                $templateCommitIds.Acquisition -and
            $null -eq $templateAcquisitions.Json.acquisitions[0].
                hostBindingRevision -and
            $templateAcquisitions.Json.acquisitions[0].
                configurationStatus -eq "needsConfiguration") `
        "Template Acquisition selected or persisted a host source"

    $reset = Invoke-LocalHttp `
        -Method Post `
        -Target "/v1/projects/active/new" `
        -Headers @{ "If-Match" = $templateEtag } `
        -Body (ConvertTo-CompactJson @{
            schemaVersion = 1
            name = "Project Service Smoke"
            description = ""
            discardDirty = $true
        })
    Assert-Status $reset 200 "Reset after template commit"
    Assert-JsonResponse $reset "Reset after template commit"
    $blank = $reset
    $blankEtag = Get-ResponseEtag `
        $blank `
        "Reset after template commit"
    Assert-True `
        ($blank.Json.workingRevision -eq 0 -and
            $blank.Json.authorityRevision -eq 0 -and
            @($blank.Json.project.controlledUnits).Count -eq 0 -and
            $blank.Json.projection.status -eq "runnable") `
        "Template smoke could not return to a blank project"

    $arraySettings = (
        $arrayManager.settings.defaults |
            ConvertTo-Json -Depth 64 -Compress |
            ConvertFrom-Json
    )
    $arraySettings.speedOfSoundMps = 1482.5
    $arrayPreview = Invoke-ProjectMutation `
        -Etag $blankEtag `
        -ValidateOnly $true `
        -Operations @(
            @{
                op = "replaceGlobalSettings"
                typeId = "pamguard.array-manager"
                settingsVersion = 1
                settings = $arraySettings
            }
        )
    Assert-Status $arrayPreview 200 "Array Manager mutation preview"
    Assert-JsonResponse $arrayPreview "Array Manager mutation preview"
    Assert-True `
        ($arrayPreview.Json.changed -and
            $arrayPreview.Json.validatedOnly -and
            $arrayPreview.Json.active.project.globalSettings.
                components[0].settings.speedOfSoundMps -eq 1482.5 -and
            $arrayPreview.Json.active.projection.status -eq "runnable" -and
            (Get-ResponseEtag `
                $arrayPreview `
                "Array Manager mutation preview") -ceq $blankEtag) `
        "Array Manager mutation did not round-trip through HTTP authority"
    $badArraySettings = (
        $arraySettings |
            ConvertTo-Json -Depth 64 -Compress |
            ConvertFrom-Json
    )
    $badArraySettings.hydrophones[1].channel = 2
    $badArray = Invoke-ProjectMutation `
        -Etag $blankEtag `
        -ValidateOnly $true `
        -Operations @(
            @{
                op = "replaceGlobalSettings"
                typeId = "pamguard.array-manager"
                settingsVersion = 1
                settings = $badArraySettings
            }
        )
    Assert-Status $badArray 422 "Invalid Array Manager geometry"
    Assert-JsonResponse $badArray "Invalid Array Manager geometry"
    Assert-True `
        ($badArray.Json.code -eq "invalid_project_projection") `
        "Invalid Array Manager geometry did not fail typed projection"

    $addFftOperation = @{
        op = "addControlledUnit"
        clientRef = "fft"
        typeId = "pamguard.fft"
        name = $null
        dependencyPolicy = "add-defaults"
    }
    $missingPrecondition = Invoke-LocalHttp `
        -Method Post `
        -Target "/v1/projects/active/mutations" `
        -Body (New-MutationBody `
            -ValidateOnly $false `
            -Operations @($addFftOperation))
    Assert-Status $missingPrecondition 428 "Missing project precondition"
    Assert-JsonResponse $missingPrecondition "Missing project precondition"
    Assert-True `
        ($missingPrecondition.Json.code -eq "precondition_required" -and
            $missingPrecondition.Json.currentEtag -ceq $blankEtag -and
            (Get-ResponseEtag `
                $missingPrecondition `
                "Missing project precondition") -ceq $blankEtag) `
        "Missing If-Match did not return the current project authority"

    $preview = Invoke-ProjectMutation `
        -Etag $blankEtag `
        -ValidateOnly $true `
        -Operations @($addFftOperation)
    Assert-Status $preview 200 "Validate-only FFT mutation"
    Assert-JsonResponse $preview "Validate-only FFT mutation"
    Assert-True `
        ($preview.Json.changed -and
            $preview.Json.validatedOnly -and
            @($preview.Json.active.project.controlledUnits).Count -eq 2 -and
            $preview.Json.active.projection.status -eq "runnable" -and
            (Get-ResponseEtag `
                $preview `
                "Validate-only FFT mutation") -ceq $blankEtag) `
        "Validate-only FFT mutation did not produce a runnable preview"
    $afterPreview = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/projects/active"
    Assert-Status $afterPreview 200 "Active project after validation"
    Assert-True `
        ($afterPreview.Json.etag -ceq $blankEtag -and
            $afterPreview.Json.workingContentHash -ceq
                $blank.Json.workingContentHash -and
            $afterPreview.Json.workingRevision -eq 0 -and
            @($afterPreview.Json.project.controlledUnits).Count -eq 0) `
        "Validate-only request changed active project authority"

    $added = Invoke-ProjectMutation `
        -Etag $blankEtag `
        -ValidateOnly $false `
        -Operations @($addFftOperation)
    Assert-Status $added 200 "FFT/default-Acquisition mutation"
    Assert-JsonResponse $added "FFT/default-Acquisition mutation"
    $etag = Get-ResponseEtag $added "FFT/default-Acquisition mutation"
    $addedActive = $added.Json.active
    $fftId = [string]@(
        $added.Json.createdEntities |
            Where-Object { $_.clientRef -eq "fft" }
    )[0].id
    $fftUnit = @(
        $addedActive.project.controlledUnits |
            Where-Object { $_.id -ceq $fftId }
    )[0]
    $acquisitionUnit = @(
        $addedActive.project.controlledUnits |
            Where-Object { $_.typeId -eq "pamguard.acquisition" }
    )[0]
    Assert-True `
        ($added.Json.changed -and
            -not $added.Json.validatedOnly -and
            $addedActive.workingRevision -eq 1 -and
            $addedActive.authorityRevision -eq 1 -and
            $addedActive.dirty -and
            @($addedActive.project.controlledUnits).Count -eq 2 -and
            $addedActive.projection.status -eq "runnable" -and
            $fftId -match
                '^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$' -and
            $null -ne $acquisitionUnit) `
        "FFT add-defaults did not atomically create Acquisition + FFT"
    Assert-True `
        (@($fftUnit.bindings).Count -eq 1 -and
            $fftUnit.bindings[0].inputRole -eq "rawAudio" -and
            @($fftUnit.bindings[0].sources).Count -eq 1 -and
            $fftUnit.bindings[0].sources[0].unitId -ceq
                $acquisitionUnit.id -and
            $fftUnit.bindings[0].sources[0].outputRole -eq
                "rawAudio") `
        "Generated FFT binding is not owned by the project model"

    $inspection = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/projects/active/inspection"
    Assert-Status $inspection 200 "Generated project inspection"
    Assert-JsonResponse $inspection "Generated project inspection"
    Assert-True `
        ((Get-ResponseEtag `
                $inspection `
                "Generated project inspection") -ceq $etag -and
            $inspection.Json.projection.status -eq "runnable" -and
            @($inspection.Json.projection.runtimeChildren).Count -eq 3 -and
            @($inspection.Json.projection.dataBlocks).Count -eq 3 -and
            @($inspection.Json.projection.publicInputs).Count -eq 1 -and
            @($inspection.Json.projection.connections).Count -eq 2 -and
            @($inspection.Json.projection.graph.modules).Count -eq 3) `
        "Generated Acquisition + FFT projection is incomplete"

    $compatible = Invoke-LocalHttp `
        -Method Get `
        -Target (
            "/v1/projects/active/compatible-sources?unitId=" +
            "$fftId&inputRole=rawAudio"
        )
    Assert-Status $compatible 200 "FFT compatible sources"
    Assert-JsonResponse $compatible "FFT compatible sources"
    Assert-True `
        ((Get-ResponseEtag `
                $compatible `
                "FFT compatible sources") -ceq $etag -and
            $compatible.Json.target.unitId -ceq $fftId -and
            $compatible.Json.target.inputRole -eq "rawAudio" -and
            @($compatible.Json.sources).Count -eq 1 -and
            $compatible.Json.sources[0].unitId -ceq
                $acquisitionUnit.id -and
            $compatible.Json.sources[0].outputRole -eq "rawAudio" -and
            $compatible.Json.sources[0].dataType -eq
                "pamguard.raw-audio") `
        "Compatible-source endpoint did not derive the Acquisition output"

    # Simulate two browsers that both read this exact token.
    $clientAEtag = $etag
    $clientBEtag = $etag
    $clientA = Invoke-ProjectMutation `
        -Etag $clientAEtag `
        -ValidateOnly $false `
        -Operations @(
            @{
                op = "renameControlledUnit"
                unit = @{ id = $fftId }
                name = "FFT Smoke"
            }
        )
    Assert-Status $clientA 200 "First concurrent project writer"
    $etag = Get-ResponseEtag $clientA "First concurrent project writer"
    Assert-True `
        ($etag -cne $clientAEtag) `
        "Committed project edit did not advance its ETag"

    $clientB = Invoke-ProjectMutation `
        -Etag $clientBEtag `
        -ValidateOnly $false `
        -Operations @(
            @{
                op = "renameControlledUnit"
                unit = @{ id = [string]$acquisitionUnit.id }
                name = "Stale Client B"
            }
        )
    Assert-Status $clientB 412 "Stale concurrent project writer"
    Assert-JsonResponse $clientB "Stale concurrent project writer"
    Assert-True `
        ($clientB.Json.code -eq "precondition_failed" -and
            $clientB.Json.currentEtag -ceq $etag -and
            (Get-ResponseEtag `
                $clientB `
                "Stale concurrent project writer") -ceq $etag) `
        "Stale project writer did not receive the winning ETag"
    $afterConflict = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/projects/active"
    $currentFft = @(
        $afterConflict.Json.project.controlledUnits |
            Where-Object { $_.id -ceq $fftId }
    )[0]
    $currentAcquisition = @(
        $afterConflict.Json.project.controlledUnits |
            Where-Object { $_.id -ceq $acquisitionUnit.id }
    )[0]
    Assert-True `
        ($afterConflict.Json.etag -ceq $etag -and
            $currentFft.name -eq "FFT Smoke" -and
            $currentAcquisition.name -ne "Stale Client B") `
        "Stale project writer overwrote the winning edit"

    $started = Invoke-LocalHttp `
        -Method Post `
        -Target "/module-runtime/control" `
        -Body '{"action":"start"}'
    Assert-Status $started 200 "Project runtime start"
    Assert-True `
        ($started.Json.running) `
        "Runnable active project did not start"

    $addDisplayOwnerOperation = @{
        op = "addControlledUnit"
        clientRef = "displayOwner"
        typeId = "pamguard.user-display"
        name = $null
        dependencyPolicy = "reject"
    }
    $runningMutation = Invoke-ProjectMutation `
        -Etag $etag `
        -ValidateOnly $false `
        -Operations @($addDisplayOwnerOperation)
    Assert-Status $runningMutation 409 "Running project mutation"
    Assert-JsonResponse $runningMutation "Running project mutation"
    Assert-True `
        ($runningMutation.Json.code -eq "runtime_running" -and
            $runningMutation.Json.currentEtag -ceq $etag -and
            (Get-ResponseEtag `
                $runningMutation `
                "Running project mutation") -ceq $etag) `
        "Running structural project mutation did not return runtime_running"
    $runningStatus = Invoke-LocalHttp `
        -Method Get `
        -Target "/module-runtime/status"
    Assert-True `
        ($runningStatus.Json.running) `
        "Rejected running mutation stopped the runtime"

    $stopped = Invoke-LocalHttp `
        -Method Post `
        -Target "/module-runtime/control" `
        -Body '{"action":"stop"}'
    Assert-Status $stopped 200 "Project runtime stop"
    Assert-True `
        (-not $stopped.Json.running) `
        "Project runtime did not stop"

    $displayOwnerResult = Invoke-ProjectMutation `
        -Etag $etag `
        -ValidateOnly $false `
        -Operations @($addDisplayOwnerOperation)
    Assert-Status $displayOwnerResult 200 "User Display mutation"
    $etag = Get-ResponseEtag $displayOwnerResult "User Display mutation"
    $activeWithOwner = $displayOwnerResult.Json.active
    $displayOwnerId = [string]@(
        $displayOwnerResult.Json.createdEntities |
            Where-Object { $_.clientRef -eq "displayOwner" }
    )[0].id
    $ownedTab = @($activeWithOwner.project.displayTabs)[0]
    Assert-True `
        (@($activeWithOwner.project.controlledUnits).Count -eq 3 -and
            @($activeWithOwner.project.displayTabs).Count -eq 1 -and
            $ownedTab.owner.unitId -ceq $displayOwnerId -and
            $ownedTab.owner.role -eq "main" -and
            @($ownedTab.displays).Count -eq 0) `
        "User Display did not create exactly one empty owned tab"

    $displayId = [System.Guid]::NewGuid().ToString(
        "D"
    ).ToLowerInvariant()
    $layoutNodes = @()
    $nodeIndex = 0
    foreach ($unit in @($activeWithOwner.project.controlledUnits)) {
        $layoutNodes += @{
            unitId = [string]$unit.id
            x = 80 + (260 * $nodeIndex)
            y = 120 + (40 * $nodeIndex)
        }
        $nodeIndex++
    }
    $dataModelLayout = @{
        nodes = @($layoutNodes)
        viewport = @{
            x = 35
            y = 55
            zoom = 1.25
        }
    }
    $displayTab = @{
        id = [string]$ownedTab.id
        name = "Operator Displays"
        owner = @{
            unitId = $displayOwnerId
            role = "main"
        }
        displays = @(
            @{
                id = $displayId
                providerTypeId =
                    [string]$spectrogramProvider.providerTypeId
                providerVersion =
                    [int]$spectrogramProvider.descriptorVersion
                owner = @{
                    unitId = $displayOwnerId
                    role = "provider"
                }
                source = @{
                    unitId = $fftId
                    outputRole = "fft"
                }
                settingsVersion =
                    [int]$spectrogramProvider.settings.version
                settings = $spectrogramProvider.settings.defaults
            }
        )
        layout = @{
            mode = "grid"
            columns = 12
            selectedDisplayId = $displayId
            items = @(
                @{
                    displayId = $displayId
                    column = 0
                    row = 0
                    width = 12
                    height = 6
                }
            )
        }
    }
    $presentationResult = Invoke-ProjectMutation `
        -Etag $etag `
        -ValidateOnly $false `
        -Operations @(
            @{
                op = "replaceDataModelLayout"
                layout = $dataModelLayout
            },
            @{
                op = "replaceDisplayHierarchy"
                displayTabs = @($displayTab)
            }
        )
    Assert-Status $presentationResult 200 "Project presentation mutation"
    Assert-JsonResponse $presentationResult "Project presentation mutation"
    $etag = Get-ResponseEtag `
        $presentationResult `
        "Project presentation mutation"
    $presentationActive = $presentationResult.Json.active
    $expectedNodePositions = @{}
    foreach ($node in $layoutNodes) {
        $expectedNodePositions[[string]$node.unitId] =
            "$($node.x),$($node.y)"
    }
    $actualLayoutNodes =
        @($presentationActive.project.dataModelLayout.nodes)
    foreach ($node in $actualLayoutNodes) {
        Assert-True `
            ($expectedNodePositions.ContainsKey(
                    [string]$node.unitId
                ) -and
                $expectedNodePositions[[string]$node.unitId] -ceq
                    "$($node.x),$($node.y)") `
            "Project changed a submitted data-model node position"
    }
    Assert-True `
        ($actualLayoutNodes.Count -eq $layoutNodes.Count -and
            $presentationActive.project.dataModelLayout.viewport.x -eq
                35 -and
            $presentationActive.project.dataModelLayout.viewport.y -eq
                55 -and
            $presentationActive.project.dataModelLayout.viewport.zoom -eq
                1.25 -and
            @($presentationActive.project.displayTabs).Count -eq 1 -and
            @($presentationActive.project.displayTabs[0].displays).Count -eq
                1 -and
            $presentationActive.project.displayTabs[0].displays[0].id -ceq
                $displayId -and
            $presentationActive.project.displayTabs[0].layout.
                selectedDisplayId -ceq $displayId) `
        "Project did not own the submitted graph/display layout"

    $presentationInspection = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/projects/active/inspection"
    Assert-Status `
        $presentationInspection `
        200 `
        "Project presentation inspection"
    $projectedFft = @(
        $presentationInspection.Json.projection.publicOutputs |
            Where-Object {
                $_.unitId -ceq $fftId -and
                $_.outputRole -eq "fft"
            }
    )[0]
    $projectedDisplay = @(
        $presentationInspection.Json.projection.displays |
            Where-Object { $_.displayId -ceq $displayId }
    )[0]
    Assert-True `
        (@($presentationInspection.Json.projection.displayTabs).Count -eq
            1 -and
            @($presentationInspection.Json.projection.displays).Count -eq
                1 -and
            $projectedDisplay.ownerUnitId -ceq $displayOwnerId -and
            $projectedDisplay.publicSource.unitId -ceq $fftId -and
            $projectedDisplay.publicSource.outputRole -eq "fft" -and
            $projectedDisplay.sourceBlockId -ceq $projectedFft.blockId -and
            @(
                $presentationInspection.Json.projection.graph.modules |
                    Where-Object {
                        $_.typeId -eq "pamguard.user-display" -or
                        $_.typeId -eq
                            "pamguard.spectrogram-display"
                    }
            ).Count -eq 0) `
        "Display ownership/source leaked presentation objects into runtime graph"

    $unitSignature = ConvertTo-CompactJson `
        $presentationActive.project.controlledUnits
    $layoutSignature = ConvertTo-CompactJson `
        $presentationActive.project.dataModelLayout
    $displaySignature = ConvertTo-CompactJson `
        $presentationActive.project.displayTabs
    $blockSignature = ConvertTo-CompactJson `
        $presentationInspection.Json.projection.dataBlocks
    $connectionSignature = ConvertTo-CompactJson `
        $presentationInspection.Json.projection.connections
    $projectedDisplaySignature = ConvertTo-CompactJson `
        $presentationInspection.Json.projection.displays

    $savedAs = Invoke-LocalHttp `
        -Method Post `
        -Target "/v1/projects/active/save-as" `
        -Headers @{ "If-Match" = $etag } `
        -Body (ConvertTo-CompactJson @{
            schemaVersion = 1
            name = "Phase 1 Service Smoke"
        })
    Assert-Status $savedAs 201 "Project Save As"
    Assert-JsonResponse $savedAs "Project Save As"
    $savedEtag = Get-ResponseEtag $savedAs "Project Save As"
    $savedProjectId = [string]$savedAs.Json.project.projectId
    Assert-True `
        ($savedProjectId -cne
            $presentationActive.project.projectId -and
            $savedAs.Json.project.metadata.name -eq
                "Phase 1 Service Smoke" -and
            -not $savedAs.Json.dirty -and
            $null -ne $savedAs.Json.savedRevision -and
            $savedAs.Json.savedContentHash -ceq
                $savedAs.Json.workingContentHash -and
            (ConvertTo-CompactJson `
                $savedAs.Json.project.controlledUnits) -ceq
                $unitSignature -and
            (ConvertTo-CompactJson `
                $savedAs.Json.project.dataModelLayout) -ceq
                $layoutSignature -and
            (ConvertTo-CompactJson `
                $savedAs.Json.project.displayTabs) -ceq
                $displaySignature -and
            $savedAs.Headers["Location"] -ceq
                "/v1/projects/$savedProjectId") `
        "Save As did not durably clone project state with stable identities"
    $savedFile = Join-Path `
        $resolvedProjectDir `
        "$savedProjectId.pamguard-project.json"
    Assert-True `
        (Test-Path -LiteralPath $savedFile -PathType Leaf) `
        "Save As did not publish the server-rooted project file"

    $savedList = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/projects"
    Assert-Status $savedList 200 "Saved project list"
    $savedSummary = @(
        $savedList.Json.projects |
            Where-Object { $_.projectId -ceq $savedProjectId }
    )[0]
    Assert-True `
        ($null -ne $savedSummary -and
            $savedSummary.status -eq "available" -and
            $savedSummary.name -eq "Phase 1 Service Smoke") `
        "Saved project list omitted the durable project"

    $savedFileResponse = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/projects/$savedProjectId"
    Assert-Status $savedFileResponse 200 "Saved project resource"
    Assert-JsonResponse $savedFileResponse "Saved project resource"
    Assert-True `
        ($savedFileResponse.Json.project.projectId -ceq
            $savedProjectId -and
            $savedFileResponse.Json.contentHash -ceq
                $savedAs.Json.savedContentHash -and
            (Get-ResponseEtag `
                $savedFileResponse `
                "Saved project resource") -ceq $savedEtag) `
        "Saved project resource differs from the active durable baseline"

    Stop-TestService $service
    $service = $null
    $env:PAMGUARD_ACTIVE_PROJECT_ID = $savedProjectId
    $service = Start-TestService -Label "restart"
    $secondService = $service

    $restored = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/projects/active"
    Assert-Status $restored 200 "Restored active project"
    Assert-JsonResponse $restored "Restored active project"
    $restoredEtag = Get-ResponseEtag $restored "Restored active project"
    Assert-True `
        ($restoredEtag -ceq $savedEtag -and
            $restored.Json.etag -ceq $savedEtag -and
            $restored.Json.project.projectId -ceq $savedProjectId -and
            -not $restored.Json.dirty -and
            (ConvertTo-CompactJson `
                $restored.Json.project.controlledUnits) -ceq
                $unitSignature -and
            (ConvertTo-CompactJson `
                $restored.Json.project.dataModelLayout) -ceq
                $layoutSignature -and
            (ConvertTo-CompactJson `
                $restored.Json.project.displayTabs) -ceq
                $displaySignature) `
        "Cold restart did not restore the durable project authority exactly"

    $restoredInspection = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/projects/active/inspection"
    Assert-Status $restoredInspection 200 "Restored project inspection"
    Assert-True `
        ((ConvertTo-CompactJson `
                $restoredInspection.Json.projection.dataBlocks) -ceq
            $blockSignature -and
            (ConvertTo-CompactJson `
                $restoredInspection.Json.projection.connections) -ceq
            $connectionSignature -and
            (ConvertTo-CompactJson `
                $restoredInspection.Json.projection.displays) -ceq
            $projectedDisplaySignature) `
        "Cold restart changed block, connection, or display identities"

    $restoredRuntime = Invoke-LocalHttp `
        -Method Get `
        -Target "/module-runtime/status"
    Assert-Status $restoredRuntime 200 "Restored project runtime status"
    Assert-True `
        ($restoredRuntime.Json.authorityMode -eq "project" -and
            $restoredRuntime.Json.projectId -ceq $savedProjectId -and
            $restoredRuntime.Json.prepared -and
            -not $restoredRuntime.Json.running -and
            @(
                $restoredRuntime.Json.modules |
                    Where-Object { $_.state -ne "prepared" }
            ).Count -eq 0) `
        "Cold restart opened or failed to prepare the project runtime"

    $lowLevelGraphWrite = Invoke-LocalHttp `
        -Method Put `
        -Target "/module-graph" `
        -Body '{}'
    Assert-Status `
        $lowLevelGraphWrite `
        405 `
        "Low-level module graph write"
    Assert-True `
        ($lowLevelGraphWrite.Json.code -eq
            "project_authority_required" -and
            $lowLevelGraphWrite.Headers["Allow"] -eq "GET") `
        "Low-level graph write did not enforce project authority"

    $lowLevelWorkspaceWrite = Invoke-LocalHttp `
        -Method Put `
        -Target "/workspaces/project-smoke" `
        -Body '{}'
    Assert-Status `
        $lowLevelWorkspaceWrite `
        405 `
        "Low-level workspace write"
    Assert-True `
        ($lowLevelWorkspaceWrite.Json.code -eq
            "project_authority_required" -and
            $lowLevelWorkspaceWrite.Headers["Allow"] -eq "GET") `
        "Low-level workspace write did not enforce project authority"

    $lowLevelWorkspaceDelete = Invoke-LocalHttp `
        -Method Delete `
        -Target "/workspaces/project-smoke"
    Assert-Status `
        $lowLevelWorkspaceDelete `
        405 `
        "Low-level workspace delete"
    Assert-True `
        ($lowLevelWorkspaceDelete.Json.code -eq
            "project_authority_required" -and
            $lowLevelWorkspaceDelete.Headers["Allow"] -eq "GET") `
        "Low-level workspace delete did not enforce project authority"

    foreach ($legacyTarget in @(
            "/sessions",
            "/sessions/oracle/archive",
            "/jobs",
            "/capture/status",
            "/workspaces"
        )) {
        $legacyResponse = Invoke-LocalHttp `
            -Method Get `
            -Target $legacyTarget
        Assert-Status `
            $legacyResponse `
            404 `
            "Project-mode compatibility route $legacyTarget"
        Assert-True `
            ($legacyResponse.Json.code -eq
                "legacy_compatibility_required") `
            "Project mode exposed compatibility route $legacyTarget"
    }

    $legacyBrowser = Invoke-LocalHttp `
        -Method Get `
        -Target "/legacy-compat.html"
    Assert-Status `
        $legacyBrowser `
        404 `
        "Production service legacy browser entry point"

    foreach ($legacyPost in @(
            "/capture/start",
            "/module-runtime/acquisitions/oracle/pcm-f32le",
            "/module-runtime/operator-inputs/oracle/events"
        )) {
        $legacyResponse = Invoke-LocalHttp `
            -Method Post `
            -Target $legacyPost `
            -Body '{}'
        Assert-Status `
            $legacyResponse `
            404 `
            "Project-mode compatibility route $legacyPost"
        Assert-True `
            ($legacyResponse.Json.code -eq
                "legacy_compatibility_required") `
            "Project mode exposed compatibility route $legacyPost"
    }

    $afterCompatibilityWrites = Invoke-LocalHttp `
        -Method Get `
        -Target "/v1/projects/active"
    Assert-Status `
        $afterCompatibilityWrites `
        200 `
        "Project authority after compatibility write attempts"
    $afterCompatibilityEtag = Get-ResponseEtag `
        $afterCompatibilityWrites `
        "Project authority after compatibility write attempts"
    Assert-True `
        ($afterCompatibilityEtag -ceq $restoredEtag -and
            $afterCompatibilityWrites.Json.workingRevision -eq
                $restored.Json.workingRevision -and
            (ConvertTo-CompactJson `
                $afterCompatibilityWrites.Json.project) -ceq
                (ConvertTo-CompactJson $restored.Json.project) -and
            (ConvertTo-CompactJson `
                $afterCompatibilityWrites.Json.projection) -ceq
                (ConvertTo-CompactJson $restored.Json.projection)) `
        "Compatibility routes or rejected low-level writes changed the active project"

    Write-Host (
        "Project service smoke passed: project authority, atomic " +
        "configuration templates, ETags, runtime guard, display/layout " +
        "ownership, persistence/restart, compatibility route gating, and " +
        "low-level write isolation"
    )
}
catch {
    Stop-TestService $service
    $service = $null
    $firstLog = Get-ServiceLogText $firstService
    $secondLog = Get-ServiceLogText $secondService
    $logText = @($firstLog, $secondLog) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    $detail = if (@($logText).Count -gt 0) {
        "`nService logs:`n" + ($logText -join "`n")
    }
    else {
        ""
    }
    throw "$($_.Exception.Message)$detail"
}
finally {
    Stop-TestService $service
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $oldEnvironment[$name],
            "Process"
        )
    }

    $candidate = [System.IO.Path]::GetFullPath($resolvedTestRoot)
    $tempPrefix = $tempBase.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    ) + [System.IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith(
            $tempPrefix,
            [System.StringComparison]::OrdinalIgnoreCase
        ) -or
        [System.IO.Path]::GetFileName($candidate) -notlike
            "pamguard-project-service-*") {
        throw "Refusing to remove unexpected test directory: $candidate"
    }
    if (Test-Path -LiteralPath $candidate) {
        Remove-Item `
            -LiteralPath $candidate `
            -Recurse `
            -Force
    }
}
