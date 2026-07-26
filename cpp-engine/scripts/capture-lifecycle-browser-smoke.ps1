param(
    [int]$Port = 18199,
    [int]$DebugPort = 19229,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [string]$ChildExe = "",
    [string]$BrowserPath = ""
)

$ErrorActionPreference = "Stop"

function Resolve-CaptureBrowser {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        $candidate = if (
            [System.IO.Path]::IsPathRooted($RequestedPath)) {
            $RequestedPath
        }
        else {
            Join-Path (Get-Location) $RequestedPath
        }
        $resolved = [System.IO.Path]::GetFullPath($candidate)
        if (-not (Test-Path -LiteralPath $resolved)) {
            throw "Requested Chromium browser not found: $resolved"
        }
        return $resolved
    }

    $candidates = @(
        "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
        "C:\Program Files\Google\Chrome\Application\chrome.exe",
        "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        "C:\Program Files\Microsoft\Edge\Application\msedge.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    throw (
        "No supported Chromium browser found. Pass -BrowserPath with " +
        "Chrome or Edge.")
}

$resolvedBuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$serviceExe = Join-Path $resolvedBuildDir "pamguard_engine_service.exe"
if (-not $ChildExe) {
    $ChildExe = Join-Path (
        $resolvedBuildDir) "capture_lifecycle_test_child.exe"
}
$resolvedChildExe = [System.IO.Path]::GetFullPath($ChildExe)
if (-not (Test-Path -LiteralPath $serviceExe)) {
    throw "Service executable not found: $serviceExe"
}
if (-not (Test-Path -LiteralPath $resolvedChildExe)) {
    throw "Capture lifecycle child executable not found: $resolvedChildExe"
}
$browserExe = Resolve-CaptureBrowser -RequestedPath $BrowserPath

$tempBase = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::GetTempPath())
$testRoot = Join-Path $tempBase (
    "pamguard-capture-lifecycle-browser-" +
    [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$resolvedRoot = [System.IO.Path]::GetFullPath($testRoot)
$profile = Join-Path $resolvedRoot "browser-profile"
$sessionDirectory = Join-Path $resolvedRoot "sessions"
New-Item -ItemType Directory -Path $profile | Out-Null
New-Item -ItemType Directory -Path $sessionDirectory | Out-Null

$environmentNames = @(
    "PAMGUARD_API_KEY",
    "PAMGUARD_API_KEY_FILE",
    "PAMGUARD_AUDIT_LOG_FILE",
    "PAMGUARD_CAPTURE_ENABLED",
    "PAMGUARD_INGEST_EXE",
    "PAMGUARD_LEGACY_MODEL_COMPAT",
    "PAMGUARD_MODULE_GRAPH_FILE",
    "PAMGUARD_SESSION_CONFIG_DIR",
    "PAMGUARD_WEB_ASSET_DIR",
    "PAMGUARD_WEB_UI_FILE",
    "PAMGUARD_WORKSPACE_FILE"
)
$oldEnvironment = @{}
foreach ($name in $environmentNames) {
    $oldEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, "Process")
}

$service = $null
$browser = $null
$socket = $null
$cdpId = 0
$captureProcessIds = [System.Collections.Generic.HashSet[int]]::new()
$serviceStdout = Join-Path $resolvedRoot "service.stdout.log"
$serviceStderr = Join-Path $resolvedRoot "service.stderr.log"

function Invoke-Cdp {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Method,
        [hashtable]$Parameters = @{}
    )

    $script:cdpId++
    $requestId = $script:cdpId
    $payload = @{
        id = $requestId
        method = $Method
        params = $Parameters
    } | ConvertTo-Json -Depth 30 -Compress
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($payload)
    [void]$socket.SendAsync(
        [ArraySegment[byte]]::new($bytes),
        [System.Net.WebSockets.WebSocketMessageType]::Text,
        $true,
        [Threading.CancellationToken]::None
    ).GetAwaiter().GetResult()

    while ($true) {
        $buffer = New-Object byte[] 1048576
        $stream = [System.IO.MemoryStream]::new()
        do {
            $received = $socket.ReceiveAsync(
                [ArraySegment[byte]]::new($buffer),
                [Threading.CancellationToken]::None
            ).GetAwaiter().GetResult()
            if ($received.MessageType -eq
                [System.Net.WebSockets.WebSocketMessageType]::Close) {
                throw "Chromium closed the DevTools connection"
            }
            $stream.Write($buffer, 0, $received.Count)
        } while (-not $received.EndOfMessage)
        $message = (
            [System.Text.Encoding]::UTF8.GetString(
                $stream.ToArray()
            ) | ConvertFrom-Json
        )
        if ($message.id -eq $requestId) {
            if ($message.error) {
                throw (
                    "Chrome DevTools command '$Method' failed: " +
                    $message.error.message)
            }
            return $message
        }
    }
}

function Invoke-BrowserExpression {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Expression
    )

    $response = Invoke-Cdp `
        -Method "Runtime.evaluate" `
        -Parameters @{
            expression = $Expression
            returnByValue = $true
            awaitPromise = $true
        }
    if ($response.result.exceptionDetails) {
        $description =
            $response.result.exceptionDetails.exception.description
        throw (
            "Browser expression failed: " +
            $response.result.exceptionDetails.text +
            $(if ($description) { ": $description" } else { "" }))
    }
    return $response.result.result.value
}

function Wait-BrowserCondition {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Expression,
        [Parameter(Mandatory = $true)]
        [string]$Description,
        [int]$Attempts = 100
    )

    for ($attempt = 0; $attempt -lt $Attempts; $attempt++) {
        try {
            if (Invoke-BrowserExpression -Expression $Expression) {
                return
            }
        }
        catch {
            # A reload can briefly destroy the execution context. Retry
            # against the replacement document.
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for browser state: $Description"
}

function Invoke-ExpectedHttpFailure {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Uri,
        [Parameter(Mandatory = $true)]
        [string]$Body
    )

    try {
        Invoke-WebRequest `
            -Method Post `
            -Uri $Uri `
            -ContentType "application/json" `
            -Body $Body `
            -UseBasicParsing |
            Out-Null
        throw "Request unexpectedly succeeded: $Uri"
    }
    catch {
        if ($null -eq $_.Exception.Response) {
            throw
        }
        $statusCode = [int]$_.Exception.Response.StatusCode
        $responseBody = $_.ErrorDetails.Message
        if (-not $responseBody) {
            try {
                $stream = $_.Exception.Response.GetResponseStream()
                if ($stream) {
                    $reader = [System.IO.StreamReader]::new($stream)
                    try {
                        $responseBody = $reader.ReadToEnd()
                    }
                    finally {
                        $reader.Dispose()
                    }
                }
            }
            catch {
                $responseBody = ""
            }
        }
        $json = $null
        if ($responseBody) {
            try {
                $json = $responseBody | ConvertFrom-Json
            }
            catch {
                $json = $null
            }
        }
        return [pscustomobject]@{
            StatusCode = $statusCode
            Body = $responseBody
            Json = $json
        }
    }
}

function Start-ModuleCapture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BaseUrl,
        [Parameter(Mandatory = $true)]
        [string]$ModuleId,
        [Parameter(Mandatory = $true)]
        [string]$SourceUrl,
        [Parameter(Mandatory = $true)]
        [uint64]$ExpectedRevision
    )

    $body = @{
        moduleId = $ModuleId
        expectedGraphRevision = $ExpectedRevision
        url = $SourceUrl
    } | ConvertTo-Json -Compress
    $result = Invoke-RestMethod `
        -Method Post `
        -Uri "$BaseUrl/capture/start" `
        -ContentType "application/json" `
        -Body $body
    if (-not $result.started -or
        $result.moduleId -ne $ModuleId -or
        $result.source -ne $SourceUrl -or
        $result.kind -ne "url" -or
        $result.graphRevision -ne $ExpectedRevision -or
        $result.pid -le 0) {
        throw (
            "Capture start returned an invalid lifecycle record: " +
            ($result | ConvertTo-Json -Compress))
    }
    [void]$script:captureProcessIds.Add([int]$result.pid)
    return $result
}

function Wait-CaptureCount {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BaseUrl,
        [Parameter(Mandatory = $true)]
        [int]$Count,
        [int]$Attempts = 100
    )

    $last = $null
    for ($attempt = 0; $attempt -lt $Attempts; $attempt++) {
        $last = Invoke-RestMethod -Uri "$BaseUrl/capture/status"
        if (@($last.captures).Count -eq $Count) {
            return $last
        }
        Start-Sleep -Milliseconds 100
    }
    throw (
        "Capture registry did not reach count $Count. Last status: " +
        ($last | ConvertTo-Json -Depth 8 -Compress))
}

function Wait-ProcessExit {
    param(
        [Parameter(Mandatory = $true)]
        [int]$ChildProcessId,
        [int]$Attempts = 100
    )

    for ($attempt = 0; $attempt -lt $Attempts; $attempt++) {
        if (-not (Get-Process `
                -Id $ChildProcessId `
                -ErrorAction SilentlyContinue)) {
            return
        }
        Start-Sleep -Milliseconds 50
    }
    throw "Capture child process $ChildProcessId did not exit"
}

function Assert-TestChildProcess {
    param(
        [Parameter(Mandatory = $true)]
        [int]$ChildProcessId
    )

    $process = Get-CimInstance `
        -ClassName Win32_Process `
        -Filter "ProcessId = $ChildProcessId"
    if (-not $process -or -not $process.ExecutablePath) {
        throw "Capture child process $ChildProcessId is not inspectable"
    }
    $actualPath = [System.IO.Path]::GetFullPath(
        $process.ExecutablePath)
    if (-not $actualPath.Equals(
            $resolvedChildExe,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw (
            "Capture pid $ChildProcessId is not the test child. " +
            "Expected '$resolvedChildExe', found '$actualPath'.")
    }
}

try {
    $webUi = (
        Resolve-Path (
            Join-Path $PSScriptRoot "..\..\web-ui\legacy-compat.html")
    ).Path
    [Environment]::SetEnvironmentVariable(
        "PAMGUARD_API_KEY", $null, "Process")
    [Environment]::SetEnvironmentVariable(
        "PAMGUARD_API_KEY_FILE", $null, "Process")
    [Environment]::SetEnvironmentVariable(
        "PAMGUARD_WEB_ASSET_DIR", $null, "Process")
    $env:PAMGUARD_AUDIT_LOG_FILE =
        Join-Path $resolvedRoot "audit.jsonl"
    $env:PAMGUARD_CAPTURE_ENABLED = "1"
    $env:PAMGUARD_INGEST_EXE = $resolvedChildExe
    $env:PAMGUARD_LEGACY_MODEL_COMPAT = "1"
    $env:PAMGUARD_MODULE_GRAPH_FILE =
        Join-Path $resolvedRoot "module-graph.json"
    $env:PAMGUARD_SESSION_CONFIG_DIR = $sessionDirectory
    $env:PAMGUARD_WEB_UI_FILE = $webUi
    $env:PAMGUARD_WORKSPACE_FILE =
        Join-Path $resolvedRoot "workspace.json"

    $service = Start-Process `
        -FilePath $serviceExe `
        -ArgumentList "$Port" `
        -RedirectStandardOutput $serviceStdout `
        -RedirectStandardError $serviceStderr `
        -PassThru `
        -WindowStyle Hidden
    $base = "http://127.0.0.1:$Port"
    $healthy = $false
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
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
        throw "Capture lifecycle service did not become healthy"
    }

    $initialGraph = @{
        expectedRevision = [uint64]0
        schemaVersion = 1
        revision = 0
        modules = @(
            @{
                id = "source-a"
                typeId = "pamguard.acquisition"
                name = "Acquisition Alpha"
                enabled = $true
                settings = @{
                    sourceId = "capture-alpha"
                    sampleRateHz = 48000
                    channelCount = 1
                    subtractDC = $false
                    dcTimeConstantSeconds = 1
                }
            },
            @{
                id = "source-b"
                typeId = "pamguard.acquisition"
                name = "Acquisition Bravo"
                enabled = $true
                settings = @{
                    sourceId = "capture-bravo"
                    sampleRateHz = 48000
                    channelCount = 2
                    subtractDC = $false
                    dcTimeConstantSeconds = 1
                }
            }
        )
        connections = @()
    } | ConvertTo-Json -Depth 12 -Compress
    $applied = Invoke-RestMethod `
        -Method Put `
        -Uri "$base/module-graph" `
        -ContentType "application/json" `
        -Body $initialGraph
    if (-not $applied.applied -or
        $applied.revision -ne 1 -or
        $applied.running) {
        throw "Two-acquisition graph was not applied cold at revision 1"
    }
    $runtime = Invoke-RestMethod `
        -Method Post `
        -Uri "$base/module-runtime/control" `
        -ContentType "application/json" `
        -Body '{"action":"start"}'
    if (-not $runtime.running -or $runtime.graphRevision -ne 1) {
        throw "Two-acquisition runtime did not start at revision 1"
    }

    $alphaUrl = "https://fixture.invalid/alpha"
    $bravoUrl = "https://fixture.invalid/bravo"
    $alpha = Start-ModuleCapture `
        -BaseUrl $base `
        -ModuleId "source-a" `
        -SourceUrl $alphaUrl `
        -ExpectedRevision 1
    $bravo = Start-ModuleCapture `
        -BaseUrl $base `
        -ModuleId "source-b" `
        -SourceUrl $bravoUrl `
        -ExpectedRevision 1
    if ($alpha.pid -eq $bravo.pid) {
        throw "Independent acquisition captures returned the same pid"
    }

    $both = Wait-CaptureCount -BaseUrl $base -Count 2
    $bothByModule = @{}
    foreach ($capture in $both.captures) {
        $bothByModule[$capture.moduleId] = $capture
    }
    if ($both.currentGraphRevision -ne 1 -or
        $bothByModule.Count -ne 2 -or
        $bothByModule["source-a"].source -ne $alphaUrl -or
        $bothByModule["source-b"].source -ne $bravoUrl -or
        $bothByModule["source-a"].pid -ne $alpha.pid -or
        $bothByModule["source-b"].pid -ne $bravo.pid) {
        throw (
            "Capture status did not preserve both independent targets: " +
            ($both | ConvertTo-Json -Depth 8 -Compress))
    }

    $stale = Invoke-ExpectedHttpFailure `
        -Uri "$base/capture/stop" `
        -Body (
            @{
                moduleId = "source-a"
                expectedGraphRevision = [uint64]0
            } | ConvertTo-Json -Compress)
    if ($stale.StatusCode -ne 409 -or
        $null -eq $stale.Json -or
        $stale.Json.code -ne "graph_revision_conflict" -or
        $stale.Json.currentGraphRevision -ne 1) {
        throw (
            "Stale capture stop did not return the revision conflict: " +
            ($stale | ConvertTo-Json -Depth 8 -Compress))
    }
    $stillBoth = Wait-CaptureCount -BaseUrl $base -Count 2
    if (@($stillBoth.captures |
            ForEach-Object pid) -notcontains $alpha.pid -or
        @($stillBoth.captures |
            ForEach-Object pid) -notcontains $bravo.pid) {
        throw "Stale capture stop mutated the active capture registry"
    }

    $browser = Start-Process `
        -FilePath $browserExe `
        -ArgumentList @(
            "--headless=new",
            "--disable-gpu",
            "--no-first-run",
            "--window-size=1800,1000",
            "--remote-debugging-port=$DebugPort",
            "--user-data-dir=$profile",
            "$base/?tab=graph"
        ) `
        -PassThru `
        -WindowStyle Hidden

    $page = $null
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        try {
            $pages = Invoke-RestMethod `
                -Uri "http://127.0.0.1:$DebugPort/json/list" `
                -TimeoutSec 1
            $page = $pages |
                Where-Object {
                    $_.type -eq "page" -and
                    $_.url.StartsWith(
                        $base,
                        [System.StringComparison]::OrdinalIgnoreCase)
                } |
                Select-Object -First 1
            if ($page) {
                break
            }
        }
        catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $page) {
        throw "Chromium DevTools page did not become available"
    }

    $socket = [System.Net.WebSockets.ClientWebSocket]::new()
    [void]$socket.ConnectAsync(
        [Uri]$page.webSocketDebuggerUrl,
        [Threading.CancellationToken]::None
    ).GetAwaiter().GetResult()
    Invoke-Cdp -Method "Page.enable" | Out-Null
    Invoke-Cdp -Method "Runtime.enable" | Out-Null

    Wait-BrowserCondition `
        -Description "initial graph and both capture indicators" `
        -Expression @'
(() => {
  const alpha = document.querySelector(
    '.graph-node[data-module-id="source-a"] .graph-capture-state.running');
  const bravo = document.querySelector(
    '.graph-node[data-module-id="source-b"] .graph-capture-state.running');
  const options = Array.from(
    document.querySelector("#captureTarget")?.options || []);
  return Boolean(
    document.querySelector("#tab-graph.active") &&
    alpha && bravo &&
    options.some((option) =>
      option.value === "module:source-a" &&
      option.textContent.includes("capturing")) &&
    options.some((option) =>
      option.value === "module:source-b" &&
      option.textContent.includes("capturing")));
})()
'@

    $oldTimeOrigin = Invoke-BrowserExpression `
        -Expression "performance.timeOrigin"
    $oldTimeOriginJson = $oldTimeOrigin |
        ConvertTo-Json -Compress
    Invoke-Cdp `
        -Method "Page.reload" `
        -Parameters @{ ignoreCache = $true } |
        Out-Null
    Wait-BrowserCondition `
        -Description "a replacement browser document after reload" `
        -Expression (
            "performance.timeOrigin !== $oldTimeOriginJson")
    Wait-BrowserCondition `
        -Description (
            "reload-hydrated Alpha and Bravo capture status") `
        -Expression @'
(() => {
  const options = Array.from(
    document.querySelector("#captureTarget")?.options || []);
  const alpha = document.querySelector(
    '.graph-node[data-module-id="source-a"] .graph-capture-state.running');
  const bravo = document.querySelector(
    '.graph-node[data-module-id="source-b"] .graph-capture-state.running');
  return Boolean(
    alpha && bravo &&
    options.some((option) =>
      option.value === "module:source-a" &&
      option.textContent.includes("Acquisition Alpha") &&
      option.textContent.includes("capturing")) &&
    options.some((option) =>
      option.value === "module:source-b" &&
      option.textContent.includes("Acquisition Bravo") &&
      option.textContent.includes("capturing")));
})()
'@

    $hydrated = (
        Invoke-BrowserExpression -Expression @'
(() => {
  const option = (id) => Array.from(
    document.querySelector("#captureTarget").options).find(
      (candidate) => candidate.value === `module:${id}`);
  const state = (id) => document.querySelector(
    `.graph-node[data-module-id="${id}"] .graph-capture-state`);
  return JSON.stringify({
    captureStatusRequests: performance.getEntriesByType("resource").filter(
      (entry) => new URL(entry.name).pathname === "/capture/status").length,
    alphaOption: option("source-a")?.textContent || "",
    bravoOption: option("source-b")?.textContent || "",
    alphaText: state("source-a")?.textContent || "",
    bravoText: state("source-b")?.textContent || "",
    alphaTitle: state("source-a")?.title || "",
    bravoTitle: state("source-b")?.title || ""
  });
})()
'@
    ) | ConvertFrom-Json
    if ($hydrated.captureStatusRequests -lt 1 -or
        -not $hydrated.alphaOption.Contains("Acquisition Alpha") -or
        -not $hydrated.alphaOption.Contains("capturing") -or
        -not $hydrated.bravoOption.Contains("Acquisition Bravo") -or
        -not $hydrated.bravoOption.Contains("capturing") -or
        -not $hydrated.alphaText.Contains("Capture running") -or
        -not $hydrated.alphaText.Contains([string]$alpha.pid) -or
        -not $hydrated.bravoText.Contains("Capture running") -or
        -not $hydrated.bravoText.Contains([string]$bravo.pid) -or
        -not $hydrated.alphaTitle.Contains($alphaUrl) -or
        -not $hydrated.alphaTitle.Contains("graph revision 1") -or
        -not $hydrated.bravoTitle.Contains($bravoUrl) -or
        -not $hydrated.bravoTitle.Contains("graph revision 1")) {
        throw (
            "Reload did not hydrate both node and target indicators from " +
            "/capture/status: " +
            ($hydrated | ConvertTo-Json -Compress))
    }

    $alphaControls = (
        Invoke-BrowserExpression -Expression @'
(() => {
  const target = document.querySelector("#captureTarget");
  target.value = "module:source-a";
  target.dispatchEvent(new Event("change", { bubbles: true }));
  return JSON.stringify({
    selected: target.value,
    startDisabled: document.querySelector(
      "#captureStartButton").disabled,
    stopDisabled: document.querySelector(
      "#captureStopButton").disabled,
    meta: document.querySelector("#captureMeta").textContent
  });
})()
'@
    ) | ConvertFrom-Json
    if ($alphaControls.selected -ne "module:source-a" -or
        -not $alphaControls.startDisabled -or
        $alphaControls.stopDisabled -or
        -not $alphaControls.meta.Contains($alphaUrl) -or
        -not $alphaControls.meta.Contains([string]$alpha.pid)) {
        throw (
            "Selecting Alpha did not expose its own Stop action: " +
            ($alphaControls | ConvertTo-Json -Compress))
    }
    Invoke-BrowserExpression `
        -Expression 'document.querySelector("#captureStopButton").click()' |
        Out-Null
    $onlyBravo = Wait-CaptureCount -BaseUrl $base -Count 1
    if ($onlyBravo.captures[0].moduleId -ne "source-b" -or
        $onlyBravo.captures[0].pid -ne $bravo.pid) {
        throw "Stopping Alpha disturbed the independent Bravo capture"
    }
    Wait-BrowserCondition `
        -Description "Alpha stopped while Bravo remains captured" `
        -Expression @'
(() => {
  const alphaState = document.querySelector(
    '.graph-node[data-module-id="source-a"] .graph-capture-state');
  const bravoState = document.querySelector(
    '.graph-node[data-module-id="source-b"] .graph-capture-state');
  const options = Array.from(
    document.querySelector("#captureTarget").options);
  const alphaOption = options.find(
    (option) => option.value === "module:source-a");
  const bravoOption = options.find(
    (option) => option.value === "module:source-b");
  return Boolean(
    alphaState && !alphaState.classList.contains("running") &&
    bravoState?.classList.contains("running") &&
    alphaOption && !alphaOption.textContent.includes("capturing") &&
    bravoOption?.textContent.includes("capturing"));
})()
'@

    $bravoControls = (
        Invoke-BrowserExpression -Expression @'
(() => {
  const target = document.querySelector("#captureTarget");
  target.value = "module:source-b";
  target.dispatchEvent(new Event("change", { bubbles: true }));
  return JSON.stringify({
    selected: target.value,
    startDisabled: document.querySelector(
      "#captureStartButton").disabled,
    stopDisabled: document.querySelector(
      "#captureStopButton").disabled,
    meta: document.querySelector("#captureMeta").textContent
  });
})()
'@
    ) | ConvertFrom-Json
    if ($bravoControls.selected -ne "module:source-b" -or
        -not $bravoControls.startDisabled -or
        $bravoControls.stopDisabled -or
        -not $bravoControls.meta.Contains($bravoUrl) -or
        -not $bravoControls.meta.Contains([string]$bravo.pid)) {
        throw (
            "Selecting Bravo did not expose its own Stop action: " +
            ($bravoControls | ConvertTo-Json -Compress))
    }
    Invoke-BrowserExpression `
        -Expression 'document.querySelector("#captureStopButton").click()' |
        Out-Null
    $none = Wait-CaptureCount -BaseUrl $base -Count 0
    if ($none.currentGraphRevision -ne 1) {
        throw "Independent UI stops changed the graph revision"
    }
    Wait-BrowserCondition `
        -Description "both captures stopped independently through the UI" `
        -Expression @'
Array.from(document.querySelectorAll(
  ".graph-node .graph-capture-state")).every(
  (state) => !state.classList.contains("running"))
'@

    $deadAlpha = Start-ModuleCapture `
        -BaseUrl $base `
        -ModuleId "source-a" `
        -SourceUrl $alphaUrl `
        -ExpectedRevision 1
    Invoke-BrowserExpression `
        -Expression "refreshCaptureStatus()" |
        Out-Null
    Wait-BrowserCondition `
        -Description "Alpha capture shown before external child death" `
        -Expression @'
document.querySelector(
  '.graph-node[data-module-id="source-a"] .graph-capture-state'
)?.classList.contains("running") === true
'@
    Assert-TestChildProcess -ChildProcessId $deadAlpha.pid
    Stop-Process -Id $deadAlpha.pid -Force
    Wait-ProcessExit -ChildProcessId $deadAlpha.pid
    $afterDeath = Wait-CaptureCount -BaseUrl $base -Count 0
    if ($afterDeath.currentGraphRevision -ne 1) {
        throw "Dead-child reaping changed the graph revision"
    }
    Invoke-BrowserExpression `
        -Expression @'
(async () => {
  await refreshCaptureStatus();
  const target = document.querySelector("#captureTarget");
  target.value = "module:source-a";
  target.dispatchEvent(new Event("change", { bubbles: true }));
  return true;
})()
'@ |
        Out-Null
    Wait-BrowserCondition `
        -Description "dead Alpha child reflected as stopped in the UI" `
        -Expression @'
(() => {
  const state = document.querySelector(
    '.graph-node[data-module-id="source-a"] .graph-capture-state');
  const option = Array.from(
    document.querySelector("#captureTarget").options).find(
      (candidate) => candidate.value === "module:source-a");
  return Boolean(
    state && !state.classList.contains("running") &&
    state.textContent.includes("Capture stopped") &&
    option && !option.textContent.includes("capturing") &&
    document.querySelector("#captureStopButton").disabled &&
    !document.querySelector("#captureStartButton").disabled);
})()
'@

    $runtimeAlpha = Start-ModuleCapture `
        -BaseUrl $base `
        -ModuleId "source-a" `
        -SourceUrl $alphaUrl `
        -ExpectedRevision 1
    $runtimeBravo = Start-ModuleCapture `
        -BaseUrl $base `
        -ModuleId "source-b" `
        -SourceUrl $bravoUrl `
        -ExpectedRevision 1
    Invoke-BrowserExpression `
        -Expression "refreshCaptureStatus()" |
        Out-Null
    Wait-BrowserCondition `
        -Description "two captures restored before runtime stop" `
        -Expression @'
document.querySelectorAll(
  ".graph-node .graph-capture-state.running").length === 2
'@
    $runtimeStopped = Invoke-RestMethod `
        -Method Post `
        -Uri "$base/module-runtime/control" `
        -ContentType "application/json" `
        -Body '{"action":"stop"}'
    if ($runtimeStopped.running -or
        $runtimeStopped.graphRevision -ne 1 -or
        $runtimeStopped.capturesStopped -ne 2) {
        throw (
            "Runtime stop did not quiesce both captures: " +
            ($runtimeStopped | ConvertTo-Json -Compress))
    }
    Wait-ProcessExit -ChildProcessId $runtimeAlpha.pid
    Wait-ProcessExit -ChildProcessId $runtimeBravo.pid
    $afterRuntimeStop = Wait-CaptureCount -BaseUrl $base -Count 0
    if ($afterRuntimeStop.currentGraphRevision -ne 1) {
        throw "Runtime capture quiescence changed the graph revision"
    }
    Invoke-BrowserExpression `
        -Expression "refreshCaptureStatus()" |
        Out-Null
    Wait-BrowserCondition `
        -Description "runtime stop reflected on both graph nodes" `
        -Expression @'
Array.from(document.querySelectorAll(
  ".graph-node .graph-capture-state")).every(
  (state) => !state.classList.contains("running"))
'@

    $runtimeRestarted = Invoke-RestMethod `
        -Method Post `
        -Uri "$base/module-runtime/control" `
        -ContentType "application/json" `
        -Body '{"action":"start"}'
    if (-not $runtimeRestarted.running -or
        $runtimeRestarted.graphRevision -ne 1) {
        throw "Runtime did not restart before graph reconfiguration"
    }
    $reconfigureBravo = Start-ModuleCapture `
        -BaseUrl $base `
        -ModuleId "source-b" `
        -SourceUrl $bravoUrl `
        -ExpectedRevision 1
    $beforeReconfigure = Wait-CaptureCount -BaseUrl $base -Count 1
    if ($beforeReconfigure.captures[0].moduleId -ne "source-b" -or
        $beforeReconfigure.captures[0].pid -ne $reconfigureBravo.pid) {
        throw "Reconfiguration precondition lost the Bravo capture"
    }

    $replacement = Invoke-RestMethod -Uri "$base/module-graph"
    ($replacement.modules |
        Where-Object id -eq "source-b").name =
        "Acquisition Bravo Reconfigured"
    $replacement |
        Add-Member `
            -NotePropertyName expectedRevision `
            -NotePropertyValue ([uint64]1)
    $replacement |
        Add-Member `
            -NotePropertyName stopRuntime `
            -NotePropertyValue $true
    $reconfigured = Invoke-RestMethod `
        -Method Put `
        -Uri "$base/module-graph" `
        -ContentType "application/json" `
        -Body ($replacement | ConvertTo-Json -Depth 20 -Compress)
    if (-not $reconfigured.applied -or
        $reconfigured.revision -ne 2 -or
        $reconfigured.running -or
        -not $reconfigured.stoppedRuntime -or
        $reconfigured.capturesStopped -ne 1) {
        throw (
            "Graph reconfiguration did not quiesce its capture: " +
            ($reconfigured | ConvertTo-Json -Compress))
    }
    Wait-ProcessExit -ChildProcessId $reconfigureBravo.pid
    $afterReconfigure = Wait-CaptureCount -BaseUrl $base -Count 0
    if ($afterReconfigure.currentGraphRevision -ne 2) {
        throw "Capture status did not advance to graph revision 2"
    }
    Invoke-BrowserExpression `
        -Expression "loadGraphEditor()" |
        Out-Null
    Wait-BrowserCondition `
        -Description (
            "reconfigured idle graph and stopped capture indicators") `
        -Expression @'
(() => {
  const bravoName = document.querySelector(
    '.graph-node[data-module-id="source-b"] .graph-node-title strong');
  const states = Array.from(document.querySelectorAll(
    ".graph-node .graph-capture-state"));
  const option = Array.from(
    document.querySelector("#captureTarget").options).find(
      (candidate) => candidate.value === "module:source-b");
  return Boolean(
    bravoName?.textContent === "Acquisition Bravo Reconfigured" &&
    states.length === 2 &&
    states.every((state) => !state.classList.contains("running")) &&
    option?.textContent.includes("Acquisition Bravo Reconfigured") &&
    !option.textContent.includes("capturing"));
})()
'@

    Write-Host (
        "Capture lifecycle browser smoke passed: two independent URL " +
        "captures, reload hydration, per-target UI stops, stale-revision " +
        "conflict, dead-child reaping, runtime quiescence, and graph " +
        "reconfiguration quiescence")
}
catch {
    foreach ($logFile in @($serviceStdout, $serviceStderr)) {
        if (Test-Path -LiteralPath $logFile) {
            Write-Warning (
                "$([System.IO.Path]::GetFileName($logFile)) tail:`n" +
                ((Get-Content -LiteralPath $logFile -Tail 40) -join "`n"))
        }
    }
    throw
}
finally {
    if ($socket -and
        $socket.State -eq
            [System.Net.WebSockets.WebSocketState]::Open) {
        try {
            [void]$socket.CloseAsync(
                [System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure,
                "done",
                [Threading.CancellationToken]::None
            ).GetAwaiter().GetResult()
        }
        catch {
            # The browser may already have closed during a failed assertion.
        }
    }
    if ($browser -and -not $browser.HasExited) {
        Stop-Process `
            -Id $browser.Id `
            -Force `
            -ErrorAction SilentlyContinue
        [void]$browser.WaitForExit(5000)
    }
    if ($service -and -not $service.HasExited) {
        Stop-Process `
            -Id $service.Id `
            -Force `
            -ErrorAction SilentlyContinue
        [void]$service.WaitForExit(5000)
    }

    foreach ($childProcessId in $captureProcessIds) {
        try {
            $child = Get-CimInstance `
                -ClassName Win32_Process `
                -Filter "ProcessId = $childProcessId" `
                -ErrorAction SilentlyContinue
            if ($child -and $child.ExecutablePath) {
                $childPath = [System.IO.Path]::GetFullPath(
                    $child.ExecutablePath)
                if ($childPath.Equals(
                        $resolvedChildExe,
                        [System.StringComparison]::OrdinalIgnoreCase)) {
                    Stop-Process `
                        -Id $childProcessId `
                        -Force `
                        -ErrorAction SilentlyContinue
                }
            }
        }
        catch {
            # Cleanup must not hide the test's primary result.
        }
    }

    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $oldEnvironment[$name],
            "Process")
    }

    $resolvedBasePrefix = $tempBase.TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar) +
        [System.IO.Path]::DirectorySeparatorChar
    $safeRoot = (
        $resolvedRoot.StartsWith(
            $resolvedBasePrefix,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedRoot).StartsWith(
            "pamguard-capture-lifecycle-browser-",
            [System.StringComparison]::Ordinal))
    if ($safeRoot) {
        Get-CimInstance Win32_Process |
            Where-Object {
                $_.CommandLine -and
                $_.CommandLine.IndexOf(
                    $profile,
                    [System.StringComparison]::OrdinalIgnoreCase) -ge 0
            } |
            ForEach-Object {
                Stop-Process `
                    -Id $_.ProcessId `
                    -Force `
                    -ErrorAction SilentlyContinue
            }
        Start-Sleep -Milliseconds 100
        if (Test-Path -LiteralPath $resolvedRoot) {
            Remove-Item `
                -LiteralPath $resolvedRoot `
                -Recurse `
                -Force `
                -ErrorAction SilentlyContinue
        }
    }
    else {
        Write-Warning (
            "Refusing unsafe capture lifecycle temp cleanup: " +
            $resolvedRoot)
    }
}
