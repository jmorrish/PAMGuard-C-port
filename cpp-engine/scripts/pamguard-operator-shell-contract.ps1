<#
.SYNOPSIS
Pending browser contract for the PAMGuard-authoritative operator shell.

.DESCRIPTION
The default mode is an executable expected-failure characterization. It must
observe and print the current legacy-shell mismatch, but returns success so the
pending target does not break the default suite before the shell cutover.

Use -EnforceTarget to turn the same observations into a hard gate. That mode
requires all of these target contracts:

  * an empty project exposes exactly one operator tab, Data Model;
  * normal startup makes no request to /sessions or /sessions/**; and
  * display state is controlled-unit-owned, with no independent Workspace
    surface, /workspaces startup request, legacy global display registry, or
    orphan display element.

The test intentionally uses no browser-test dependency. It drives an installed
Chromium browser through the Chrome DevTools Protocol, matching the existing
visual graph smoke-test convention.
#>

param(
    [int]$Port = 18196,
    [int]$DebugPort = 19228,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [string]$BrowserPath = "",
    [switch]$EnforceTarget
)

$ErrorActionPreference = "Stop"

function Resolve-ContractBrowser {
    param([string]$RequestedPath)

    if ($RequestedPath) {
        $candidatePath = if (
            [System.IO.Path]::IsPathRooted($RequestedPath)) {
            $RequestedPath
        }
        else {
            Join-Path (Get-Location) $RequestedPath
        }
        $resolved = [System.IO.Path]::GetFullPath($candidatePath)
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

$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
if (-not (Test-Path -LiteralPath $serviceExe)) {
    throw "Service executable not found: $serviceExe"
}
$browserExe = Resolve-ContractBrowser -RequestedPath $BrowserPath

$tempBase = [System.IO.Path]::GetTempPath()
$testRoot = Join-Path $tempBase (
    "pamguard-operator-shell-contract-" +
    [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$profile = Join-Path $testRoot "browser-profile"
New-Item -ItemType Directory -Path $profile | Out-Null
$sessionDirectory = Join-Path $testRoot "sessions"
New-Item -ItemType Directory -Path $sessionDirectory | Out-Null

$environmentNames = @(
    "PAMGUARD_API_KEY",
    "PAMGUARD_MODULE_GRAPH_FILE",
    "PAMGUARD_SESSION_CONFIG_DIR",
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

function Add-ContractResult {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [System.Collections.ArrayList]$Results,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [bool]$Passed,
        [Parameter(Mandatory = $true)]
        [object]$Evidence
    )

    [void]$Results.Add([pscustomobject]@{
        name = $Name
        passed = $Passed
        evidence = $Evidence
    })
}

try {
    [Environment]::SetEnvironmentVariable(
        "PAMGUARD_API_KEY",
        $null,
        "Process")
    [Environment]::SetEnvironmentVariable(
        "PAMGUARD_MODULE_GRAPH_FILE",
        (Join-Path $testRoot "empty-module-graph.json"),
        "Process")
    [Environment]::SetEnvironmentVariable(
        "PAMGUARD_SESSION_CONFIG_DIR",
        $sessionDirectory,
        "Process")
    [Environment]::SetEnvironmentVariable(
        "PAMGUARD_WEB_UI_FILE",
        (Resolve-Path (
            Join-Path $PSScriptRoot "..\..\web-ui\index.html")).Path,
        "Process")
    [Environment]::SetEnvironmentVariable(
        "PAMGUARD_WORKSPACE_FILE",
        (Join-Path $testRoot "empty-workspaces.json"),
        "Process")

    $service = Start-Process `
        -FilePath $serviceExe `
        -ArgumentList "$Port" `
        -PassThru `
        -WindowStyle Hidden
    $base = "http://127.0.0.1:$Port"
    $healthy = $false
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        try {
            $health = Invoke-RestMethod -Uri "$base/health"
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
        throw "Operator-shell contract service did not become healthy"
    }

    $graph = Invoke-RestMethod -Uri "$base/module-graph"
    if (@($graph.modules).Count -ne 0 -or
        @($graph.connections).Count -ne 0) {
        throw (
            "Contract fixture is not an empty module graph: " +
            ($graph | ConvertTo-Json -Depth 8 -Compress))
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
            "about:blank"
        ) `
        -PassThru `
        -WindowStyle Hidden

    $page = $null
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        try {
            $pages = Invoke-RestMethod `
                -Uri "http://127.0.0.1:$DebugPort/json/list" `
                -TimeoutSec 1
            $page = $pages |
                Where-Object type -eq "page" |
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

    $requestRecorder = @'
(() => {
  const requests = [];
  Object.defineProperty(globalThis, "__pamguardOperatorContractRequests", {
    configurable: false,
    enumerable: false,
    value: requests,
    writable: false
  });
  const record = (kind, rawUrl, method = "GET") => {
    try {
      const url = new URL(String(rawUrl), location.href).href;
      requests.push({ kind, method: String(method || "GET").toUpperCase(), url });
    } catch {
      requests.push({
        kind,
        method: String(method || "GET").toUpperCase(),
        url: String(rawUrl)
      });
    }
  };

  const nativeFetch = globalThis.fetch.bind(globalThis);
  globalThis.fetch = (input, init = {}) => {
    const url = input instanceof Request ? input.url : input;
    const method = init.method ||
      (input instanceof Request ? input.method : "GET");
    record("fetch", url, method);
    return nativeFetch(input, init);
  };

  const nativeXhrOpen = XMLHttpRequest.prototype.open;
  XMLHttpRequest.prototype.open = function(method, url, ...rest) {
    record("xhr", url, method);
    return nativeXhrOpen.call(this, method, url, ...rest);
  };

  if (globalThis.EventSource) {
    globalThis.EventSource = new Proxy(globalThis.EventSource, {
      construct(target, args, newTarget) {
        record("event-source", args[0], "GET");
        return Reflect.construct(target, args, newTarget);
      }
    });
  }
  if (globalThis.WebSocket) {
    globalThis.WebSocket = new Proxy(globalThis.WebSocket, {
      construct(target, args, newTarget) {
        record("web-socket", args[0], "CONNECT");
        return Reflect.construct(target, args, newTarget);
      }
    });
  }
  if (navigator.sendBeacon) {
    const nativeSendBeacon = navigator.sendBeacon.bind(navigator);
    navigator.sendBeacon = (url, data) => {
      record("beacon", url, "POST");
      return nativeSendBeacon(url, data);
    };
  }
})()
'@
    Invoke-Cdp `
        -Method "Page.addScriptToEvaluateOnNewDocument" `
        -Parameters @{ source = $requestRecorder } |
        Out-Null
    Invoke-Cdp `
        -Method "Page.navigate" `
        -Parameters @{ url = "$base/" } |
        Out-Null

    $loaded = $false
    for ($attempt = 0; $attempt -lt 60; $attempt++) {
        try {
            $state = Invoke-BrowserExpression `
                -Expression "document.readyState"
            if ($state -eq "complete") {
                $loaded = $true
                break
            }
        }
        catch {
            # Navigation can replace the execution context between polls.
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $loaded) {
        throw "Operator-shell contract page did not finish loading"
    }
    Invoke-BrowserExpression -Expression @'
new Promise((resolve) => setTimeout(() => resolve(true), 1500))
'@ | Out-Null

    $snapshot = (
        Invoke-BrowserExpression -Expression @'
(() => {
  const unique = (values) => Array.from(new Set(values));
  const normalize = (value) =>
    String(value || "").replace(/\s+/g, " ").trim().toLowerCase();
  const requests = Array.from(
    globalThis.__pamguardOperatorContractRequests || []);
  const requestPaths = requests.map((request) => {
    try {
      return new URL(request.url, location.href).pathname;
    } catch {
      return String(request.url || "");
    }
  });
  const legacySessionRequests = requests.filter((request, index) =>
    /^\/sessions(?:\/|$)/.test(requestPaths[index]));
  const legacyWorkspaceRequests = requests.filter((request, index) =>
    /^\/workspaces(?:\/|$)/.test(requestPaths[index]));

  const tabElements = unique([
    ...document.querySelectorAll('[role="tab"]'),
    ...document.querySelectorAll("[data-pamguard-tab-kind]"),
    ...document.querySelectorAll(".tabbar button[data-tab]")
  ]);
  const tabs = tabElements.map((element) => ({
    id: element.id || "",
    kind: element.getAttribute("data-pamguard-tab-kind") || "",
    name: element.getAttribute("aria-label") ||
      element.textContent.trim(),
    active: element.getAttribute("aria-selected") === "true" ||
      element.classList.contains("active")
  }));
  const normalizedTabNames = tabs.map((tab) => normalize(tab.name));
  const phantomNames = new Set([
    "workspace",
    "spectrogram",
    "click detector",
    "clicks",
    "detections",
    "archive",
    "console"
  ]);
  const phantomTabs = tabs.filter((tab) =>
    phantomNames.has(normalize(tab.name)));
  const phantomPanels = Array.from(document.querySelectorAll([
    "#tab-workspace",
    "#tab-spectrogram",
    "#tab-clicks",
    "#tab-detections",
    "#tab-archive",
    "#tab-console"
  ].join(","))).map((element) => element.id);

  const globalWorkspaceSelectors = [
    '#tab-workspace',
    '.tabbar [data-tab="workspace"]',
    '[data-tab-jump="workspace"]',
    '#workspaceAudioSource',
    '#workspaceArrangement',
    '#operatorGrid',
    '#operatorTabs'
  ];
  const globalWorkspaceSurfaces = globalWorkspaceSelectors.filter(
    (selector) => document.querySelector(selector));
  const testState = globalThis.__pamguardTest;
  const legacyGlobalDisplayState = [
    ["workspaceDisplays", Boolean(testState?.workspaceDisplays)],
    ["workspaceBlocks", Boolean(testState?.workspaceBlocks)]
  ].filter((entry) => entry[1]).map((entry) => entry[0]);

  const displayElements = unique([
    ...document.querySelectorAll("[data-pamguard-display-instance-id]"),
    ...document.querySelectorAll("[data-display-instance-id]"),
    ...document.querySelectorAll(".operator-display")
  ]);
  const displayTabs = tabs.filter((tab) => tab.kind === "display");
  const orphanDisplays = displayElements.filter((element) =>
    !(element.getAttribute("data-owner-controlled-unit-id") ||
      element.getAttribute("data-owner-unit-id"))).map((element) =>
        element.getAttribute("data-pamguard-display-instance-id") ||
        element.getAttribute("data-display-instance-id") ||
        element.id ||
        element.className);
  const orphanDisplayTabs = tabElements.filter((element) =>
    element.getAttribute("data-pamguard-tab-kind") === "display" &&
    !(element.getAttribute("data-owner-controlled-unit-id") ||
      element.getAttribute("data-owner-unit-id"))).map((element) =>
        element.id || element.textContent.trim());

  const activeTabs = tabs.filter((tab) => tab.active);
  const dataModelOnly =
    tabs.length === 1 &&
    normalizedTabNames[0] === "data model" &&
    activeTabs.length === 1 &&
    normalize(activeTabs[0].name) === "data model" &&
    phantomTabs.length === 0 &&
    phantomPanels.length === 0;
  const displayOwnershipNotGlobal =
    displayElements.length === 0 &&
    displayTabs.length === 0 &&
    orphanDisplays.length === 0 &&
    orphanDisplayTabs.length === 0 &&
    globalWorkspaceSurfaces.length === 0 &&
    legacyWorkspaceRequests.length === 0 &&
    legacyGlobalDisplayState.length === 0;

  return JSON.stringify({
    readyState: document.readyState,
    requestCount: requests.length,
    requests,
    tabs,
    phantomTabs,
    phantomPanels,
    legacySessionRequests,
    legacyWorkspaceRequests,
    displayElementCount: displayElements.length,
    displayTabCount: displayTabs.length,
    orphanDisplays,
    orphanDisplayTabs,
    globalWorkspaceSurfaces,
    legacyGlobalDisplayState,
    contracts: {
      dataModelOnly,
      noLegacySessionRequests: legacySessionRequests.length === 0,
      displayOwnershipNotGlobal
    }
  });
})()
'@
    ) | ConvertFrom-Json

    $results = [System.Collections.ArrayList]::new()
    Add-ContractResult `
        -Results $results `
        -Name "blank-project-data-model-only" `
        -Passed ([bool]$snapshot.contracts.dataModelOnly) `
        -Evidence ([pscustomobject]@{
            tabs = $snapshot.tabs
            phantomTabs = $snapshot.phantomTabs
            phantomPanels = $snapshot.phantomPanels
        })
    Add-ContractResult `
        -Results $results `
        -Name "startup-does-not-call-legacy-sessions" `
        -Passed ([bool]$snapshot.contracts.noLegacySessionRequests) `
        -Evidence ([pscustomobject]@{
            startupRequestCount = $snapshot.requestCount
            legacySessionRequests = $snapshot.legacySessionRequests
        })
    Add-ContractResult `
        -Results $results `
        -Name "display-state-is-controlled-unit-owned" `
        -Passed ([bool]$snapshot.contracts.displayOwnershipNotGlobal) `
        -Evidence ([pscustomobject]@{
            displayElementCount = $snapshot.displayElementCount
            displayTabCount = $snapshot.displayTabCount
            orphanDisplays = $snapshot.orphanDisplays
            orphanDisplayTabs = $snapshot.orphanDisplayTabs
            globalWorkspaceSurfaces = $snapshot.globalWorkspaceSurfaces
            legacyWorkspaceRequests = $snapshot.legacyWorkspaceRequests
            legacyGlobalDisplayState = $snapshot.legacyGlobalDisplayState
        })

    $lifecycle = (
        Invoke-BrowserExpression -Expression @'
(async () => {
  const requests =
    globalThis.__pamguardOperatorContractRequests || [];
  const requestsBefore = requests.length;
  const listenersBefore =
    globalThis.PamguardPlatform.lifecycle.listenerCount;
  await globalThis.PamguardApplication.active.dispose();
  let remountRejected = false;
  let remountMessage = "";
  try {
    globalThis.PamguardApplication.mount();
  } catch (error) {
    remountRejected = true;
    remountMessage = String(error?.message || error);
  }
  await new Promise((resolve) => setTimeout(resolve, 2300));
  return JSON.stringify({
    requestsBefore,
    requestsAfter: requests.length,
    listenersBefore,
    listenersAfter:
      globalThis.PamguardPlatform.lifecycle.listenerCount,
    active: globalThis.PamguardApplication.active !== null,
    remountRejected,
    remountMessage,
    graphNodes:
      document.querySelectorAll(".graph-node").length,
    workspaceDisplays:
      document.querySelectorAll(".operator-display").length
  });
})()
'@
    ) | ConvertFrom-Json
    Add-ContractResult `
        -Results $results `
        -Name "application-dispose-quiesces-browser-resources" `
        -Passed (
            -not $lifecycle.active -and
            $lifecycle.listenersBefore -gt 0 -and
            $lifecycle.listenersAfter -eq 0 -and
            $lifecycle.requestsAfter -eq
                $lifecycle.requestsBefore -and
            $lifecycle.remountRejected -and
            $lifecycle.graphNodes -eq 0 -and
            $lifecycle.workspaceDisplays -eq 0
        ) `
        -Evidence $lifecycle

    foreach ($result in $results) {
        $label = if ($result.passed) {
            "PASS"
        }
        elseif ($EnforceTarget) {
            "FAIL"
        }
        else {
            "EXPECTED-FAIL"
        }
        Write-Host (
            "[$label] $($result.name): " +
            ($result.evidence | ConvertTo-Json -Depth 12 -Compress))
    }

    $failures = @($results | Where-Object { -not $_.passed })
    if ($EnforceTarget) {
        if ($failures.Count) {
            throw (
                "PAMGuard operator-shell target contract failed: " +
                (($failures | ForEach-Object name) -join ", "))
        }
        Write-Host (
            "PAMGuard operator-shell target contract passed: empty " +
            "Data Model shell, no legacy session startup calls, and " +
            "controlled-unit-owned display state")
    }
    else {
        $expectedFailureNames = @(
            "blank-project-data-model-only",
            "display-state-is-controlled-unit-owned"
        )
        $unexpectedFailures = @($failures | Where-Object {
            $expectedFailureNames -notcontains $_.name
        })
        if ($unexpectedFailures.Count) {
            throw (
                "Expected-failure mode found a new regression: " +
                (($unexpectedFailures | ForEach-Object name) -join ", "))
        }
        if (-not $failures.Count) {
            throw (
                "Expected-failure mode found no legacy mismatch. Promote " +
                "the contract by enabling its CTest gate and running with " +
                "-EnforceTarget.")
        }
        Write-Host (
            "Expected legacy mismatch proved. This pending contract is " +
            "non-blocking only until the PAMGuard shell cutover; use " +
            "-EnforceTarget to make all three assertions mandatory.")
    }
}
finally {
    if ($socket -and
        $socket.State -eq
            [System.Net.WebSockets.WebSocketState]::Open) {
        [void]$socket.CloseAsync(
            [System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure,
            "done",
            [Threading.CancellationToken]::None
        ).GetAwaiter().GetResult()
    }
    if ($browser -and -not $browser.HasExited) {
        Stop-Process -Id $browser.Id -Force
    }
    if ($service -and -not $service.HasExited) {
        Stop-Process -Id $service.Id -Force
    }
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $oldEnvironment[$name],
            "Process")
    }

    $resolvedRoot = [System.IO.Path]::GetFullPath($testRoot)
    $resolvedBase = [System.IO.Path]::GetFullPath($tempBase)
    if ($resolvedRoot.StartsWith(
            $resolvedBase,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        $resolvedRoot -ne $resolvedBase) {
        Get-CimInstance Win32_Process |
            Where-Object {
                $_.CommandLine -and
                $_.CommandLine.IndexOf(
                    $resolvedRoot,
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
                -Force
        }
    }
}
