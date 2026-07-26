<#
.SYNOPSIS
End-to-end browser smoke test for the PAMGuard-authoritative project shell.

.DESCRIPTION
Starts the real engine service with an isolated PAMGUARD_PROJECT_DIR and drives
an installed Chromium browser through the Chrome DevTools Protocol. No browser
test package, mocked API, legacy module-graph setup, or workspace/session setup
is used.

The hard contract covers:

  * a cold blank project exposes only the Data Model operator tab;
  * global Array Manager remains configurable on that blank project, with
    dedicated sections and exact OK/Cancel project semantics;
  * the Click monitoring configuration template previews and atomically adds
    its five independent controlled units and owned displays;
  * Matched Template is then added separately, exposes its dedicated editor,
    binds to the template Click Detector, and preserves PAMGuard's exact
    192 kHz Beaked Whale/Dolphin default waveforms against the 48 kHz source;
  * that template requires an explicit host input and playback-channel choice,
    then runs injected audio through a live Spectrogram, continuous Click
    display, and joined Matched Template click annotations;
  * normal browser operation never calls /sessions, /workspaces, or the
    low-level /module-graph workflow;
  * Sound Acquisition, Sound Recorder, Sound Output, a raw FFT branch, and a
    true Decimator -> FFT branch are configured through visible UI actions;
  * graph-port reconnect and settings-source selection are one bidirectional
    binding, with compatible/incompatible accessibility state;
  * Dynamic Display contributions enforce each owner's provider maximum,
    including static Click and Level displays, and unit-name suggestions
    reuse the first free PAMGuard-style numeric suffix;
  * the running Sound Recorder dialog keeps only Off/Continuous transport
    live, uses stable controlled-unit HTTP routes with strict revisioned
    command bodies, and renders only safe recorder status/file names;
  * User Display owns independent raw and 24 kHz decimated Spectrograms, and
    the latter derives its 12 kHz Nyquist limit and renders live frames;
  * after source/owner removal the FFT/User Display workflow is re-added with
    fresh identities and no orphan display/stream, then Save As survives a
    real service restart with those stable replacement IDs;
  * removing one FFT explicitly unbinds only its Spectrogram while the other
    continues to stream;
  * a forced old-browser If-Match receives a real HTTP 412, is shown visibly,
    and is never silently retried or applied; and
  * removing User Display removes its owned tab and Spectrogram.

Stable semantic data attributes are preferred, while accessible button/menu
names are accepted as a fallback. Styling classes are not the primary contract.
#>

param(
    [int]$Port = 18203,
    [int]$DebugPort = 19231,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [string]$BrowserPath = "",
    [string]$ArtifactPath = ""
)

$ErrorActionPreference = "Stop"

function Resolve-SmokeBrowser {
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
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "Requested Chromium browser not found: $resolved"
        }
        return $resolved
    }

    foreach ($candidate in @(
        "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
        "C:\Program Files\Google\Chrome\Application\chrome.exe",
        "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        "C:\Program Files\Microsoft\Edge\Application\msedge.exe"
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    throw (
        "No supported Chromium browser found. Pass -BrowserPath with " +
        "Chrome or Edge.")
}

function Resolve-ServiceExecutable {
    param([string]$Directory)

    foreach ($candidate in @(
        (Join-Path $Directory "pamguard_engine_service.exe"),
        (Join-Path $Directory "Debug\pamguard_engine_service.exe"),
        (Join-Path $Directory "Release\pamguard_engine_service.exe")
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    throw "Service executable not found under $Directory"
}

function Assert-TcpPortAvailable {
    param(
        [Parameter(Mandatory = $true)]
        [int]$CandidatePort,
        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    $listener = [System.Net.Sockets.TcpListener]::new(
        [System.Net.IPAddress]::Loopback,
        $CandidatePort)
    try {
        $listener.Start()
    }
    catch {
        throw "$Label TCP port $CandidatePort is already in use"
    }
    finally {
        $listener.Stop()
    }
}

$serviceExe = Resolve-ServiceExecutable -Directory $BuildDir
$browserExe = Resolve-SmokeBrowser -RequestedPath $BrowserPath
$webUiFile = (
    Resolve-Path (
        Join-Path $PSScriptRoot "..\..\web-ui\index.html")
).Path
$webAssetDir = (
    Resolve-Path (
        Join-Path $PSScriptRoot "..\..\web-ui\assets")
).Path

$tempBase = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::GetTempPath())
$testRoot = Join-Path $tempBase (
    "pamguard-project-shell-browser-" +
    [System.Guid]::NewGuid().ToString("N"))
$projectDirectory = Join-Path $testRoot "projects"
$legacySessionDirectory = Join-Path $testRoot "legacy-sessions"
$browserProfile = Join-Path $testRoot "browser-profile"
$recordingRoot = Join-Path $testRoot "recordings"
foreach ($directory in @(
    $testRoot,
    $projectDirectory,
    $legacySessionDirectory,
    $browserProfile,
    $recordingRoot
)) {
    New-Item -ItemType Directory -Path $directory | Out-Null
}

$environmentNames = @(
    "PAMGUARD_ACTIVE_PROJECT_ID",
    "PAMGUARD_API_KEY",
    "PAMGUARD_API_KEY_FILE",
    "PAMGUARD_CAPTURE_ENABLED",
    "PAMGUARD_LEGACY_MODEL_COMPAT",
    "PAMGUARD_MODULE_GRAPH_FILE",
    "PAMGUARD_PROJECT_DIR",
    "PAMGUARD_RECORDING_ROOT",
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
$env:PAMGUARD_RECORDING_ROOT = $recordingRoot

$service = $null
$browser = $null
$socket = $null
$cdpId = 0
$serviceGeneration = 0

function Get-ServiceLogText {
    param($Handle)

    if ($null -eq $Handle) {
        return ""
    }
    $parts = @()
    foreach ($path in @($Handle.Stdout, $Handle.Stderr)) {
        if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) {
            $stream = [System.IO.FileStream]::new(
                $path,
                [System.IO.FileMode]::Open,
                [System.IO.FileAccess]::Read,
                [System.IO.FileShare]::ReadWrite)
            try {
                $reader = [System.IO.StreamReader]::new($stream)
                try {
                    $text = $reader.ReadToEnd()
                }
                finally {
                    $reader.Dispose()
                }
            }
            finally {
                $stream.Dispose()
            }
            if (-not [string]::IsNullOrWhiteSpace($text)) {
                $parts += $text.Trim()
            }
        }
    }
    return ($parts -join "`n")
}

function Start-SmokeService {
    param([string]$Label)

    $script:serviceGeneration++
    $stdout = Join-Path $testRoot (
        "$($script:serviceGeneration)-$Label.stdout.log")
    $stderr = Join-Path $testRoot (
        "$($script:serviceGeneration)-$Label.stderr.log")
    $process = Start-Process `
        -FilePath $serviceExe `
        -ArgumentList "$Port" `
        -PassThru `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr
    $handle = [pscustomobject]@{
        Process = $process
        Stdout = $stdout
        Stderr = $stderr
    }
    $base = "http://127.0.0.1:$Port"
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($process.HasExited) {
            throw (
                "Project-shell service exited during $Label startup " +
                "with code $($process.ExitCode): " +
                (Get-ServiceLogText $handle))
        }
        try {
            $health = Invoke-RestMethod `
                -Uri "$base/health" `
                -TimeoutSec 1
            if ($health.ok) {
                return $handle
            }
        }
        catch {
            Start-Sleep -Milliseconds 100
        }
    }
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    Wait-Process -Id $process.Id -ErrorAction SilentlyContinue
    throw (
        "Project-shell service did not become healthy for $Label. " +
        (Get-ServiceLogText $handle))
}

function Stop-SmokeService {
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
    } | ConvertTo-Json -Depth 40 -Compress
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
                $stream.ToArray()) |
                ConvertFrom-Json
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

function Wait-BrowserDocument {
    param([string]$Context)

    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        try {
            $state = Invoke-BrowserExpression `
                -Expression "document.readyState"
            if ($state -eq "complete") {
                Invoke-BrowserExpression -Expression @'
new Promise((resolve) => setTimeout(() => resolve(true), 250))
'@ | Out-Null
                return
            }
        }
        catch {
            # Navigation can replace the execution context between polls.
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Browser document did not finish loading for $Context"
}

$bootstrapScript = @'
(() => {
  "use strict";

  const storageKey = "__pamguardProjectShellSmokeRequestsV1";
  const readRequests = () => {
    try {
      const parsed = JSON.parse(sessionStorage.getItem(storageKey) || "[]");
      return Array.isArray(parsed) ? parsed : [];
    } catch {
      return [];
    }
  };
  let requests = readRequests();
  const persistRequests = () => {
    try {
      sessionStorage.setItem(storageKey, JSON.stringify(requests));
    } catch {
      // The in-memory recorder remains authoritative for this document.
    }
  };
  const pathOf = (rawUrl) => {
    try {
      return new URL(String(rawUrl), location.href).pathname;
    } catch {
      return String(rawUrl || "");
    }
  };
  const record = (kind, rawUrl, method = "GET") => {
    const entry = {
      sequence: requests.length + 1,
      kind,
      method: String(method || "GET").toUpperCase(),
      url: (() => {
        try {
          return new URL(String(rawUrl), location.href).href;
        } catch {
          return String(rawUrl || "");
        }
      })(),
      status: null,
      forcedIfMatch: null
    };
    requests.push(entry);
    persistRequests();
    return entry;
  };
  const finish = (entry, status, error = "") => {
    entry.status = Number.isFinite(Number(status))
      ? Number(status)
      : null;
    if (error) entry.error = String(error);
    persistRequests();
  };

  globalThis.__pamguardForceNextProjectIfMatch = null;
  const nativeFetch = globalThis.fetch.bind(globalThis);
  globalThis.fetch = (input, init = {}) => {
    const request = input instanceof Request ? input : null;
    const rawUrl = request ? request.url : input;
    const method = init.method || request?.method || "GET";
    let effectiveInput = input;
    let effectiveInit = { ...init };
    const force = globalThis.__pamguardForceNextProjectIfMatch;
    if (force &&
        String(method).toUpperCase() === "POST" &&
        pathOf(rawUrl) === "/v1/projects/active/mutations") {
      const headers = new Headers(request?.headers || {});
      new Headers(init.headers || {}).forEach(
        (value, name) => headers.set(name, value));
      headers.set("If-Match", String(force));
      effectiveInit.headers = headers;
      if (request) {
        effectiveInput = new Request(request, effectiveInit);
        effectiveInit = {};
      }
      globalThis.__pamguardForceNextProjectIfMatch = null;
    }
    const entry = record("fetch", rawUrl, method);
    const captureBody = (rawBody) => {
      if (typeof rawBody !== "string" || rawBody.length === 0) return;
      try {
        entry.body = JSON.parse(rawBody);
      } catch {
        entry.body = rawBody;
      }
      persistRequests();
    };
    captureBody(effectiveInit.body);
    if (request && effectiveInit.body === undefined) {
      request.clone().text().then(captureBody).catch(() => {});
    }
    if (force &&
        pathOf(rawUrl) === "/v1/projects/active/mutations") {
      entry.forcedIfMatch = String(force);
      persistRequests();
    }
    return nativeFetch(effectiveInput, effectiveInit).then(
      (response) => {
        finish(entry, response.status);
        return response;
      },
      (error) => {
        finish(entry, null, error?.message || error);
        throw error;
      });
  };

  const nativeXhrOpen = XMLHttpRequest.prototype.open;
  XMLHttpRequest.prototype.open = function(method, url, ...rest) {
    const entry = record("xhr", url, method);
    this.addEventListener("loadend", () =>
      finish(entry, this.status), { once: true });
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
    const nativeBeacon = navigator.sendBeacon.bind(navigator);
    navigator.sendBeacon = (url, data) => {
      record("beacon", url, "POST");
      return nativeBeacon(url, data);
    };
  }

  const sleep = (milliseconds) =>
    new Promise((resolve) => setTimeout(resolve, milliseconds));
  const normalize = (value) =>
    String(value || "").replace(/\s+/g, " ").trim().toLowerCase();
  const isVisible = (element) => {
    if (!element || !element.isConnected) return false;
    const style = getComputedStyle(element);
    const rectangle = element.getBoundingClientRect();
    return style.display !== "none" &&
      style.visibility !== "hidden" &&
      !element.hidden &&
      rectangle.width > 0 &&
      rectangle.height > 0;
  };
  const assert = (condition, message, evidence = null) => {
    if (condition) return;
    const suffix = evidence === null
      ? ""
      : ` Evidence: ${JSON.stringify(evidence)}`;
    throw new Error(message + suffix);
  };
  async function waitFor(callback, label, timeout = 10000) {
    const deadline = performance.now() + timeout;
    let lastError = null;
    while (performance.now() < deadline) {
      try {
        const value = await callback();
        if (value) return value;
      } catch (error) {
        lastError = error;
      }
      await sleep(100);
    }
    throw new Error(
      `Timed out waiting for ${label}` +
      (lastError ? `: ${lastError.message}` : ""));
  }
  function unique(elements) {
    return Array.from(new Set(elements));
  }
  function queryVisible(selectors, root = document) {
    return unique(selectors.flatMap((selector) =>
      Array.from(root.querySelectorAll(selector))))
      .filter(isVisible);
  }
  function byText(
    selectors,
    names,
    root = document,
    { exact = true, excludeNodes = false } = {}) {
    const normalizedNames = names.map(normalize);
    return queryVisible(selectors, root).find((element) => {
      if (excludeNodes && element.closest(
          "[data-unit-id], [data-controlled-unit-id], .graph-node")) {
        return false;
      }
      const text = normalize(
        element.getAttribute("aria-label") ||
        element.getAttribute("title") ||
        element.textContent);
      return normalizedNames.some((name) =>
        exact ? text === name : text.includes(name));
    }) || null;
  }
  function click(element, label) {
    assert(element, `Could not find ${label}`);
    element.scrollIntoView({ block: "center", inline: "center" });
    element.click();
  }
  function openDialog() {
    return queryVisible([
      "dialog[open]",
      '[role="dialog"][aria-modal="true"]',
      "[data-project-dialog][data-open='true']"
    ])[0] || null;
  }
  function textControl(surface, purposes) {
    const selectors = purposes.flatMap((purpose) => [
      `[data-project-field="${purpose}"]`,
      `input[name="${purpose}"]`,
      `textarea[name="${purpose}"]`
    ]);
    return queryVisible([
      ...selectors,
      "input[type='text']",
      "input:not([type])",
      "textarea"
    ], surface)[0] || null;
  }
  function setControlValue(control, value) {
    assert(control, "Settings control is missing");
    if (control.type === "checkbox" || control.type === "radio") {
      control.checked = Boolean(value);
    } else {
      control.value = String(value);
    }
    control.dispatchEvent(new Event("input", { bubbles: true }));
    control.dispatchEvent(new Event("change", { bubbles: true }));
  }
  function confirmDialog(surface, labels = ["OK", "Save", "Add", "Create"]) {
    const button = queryVisible([
      '[data-project-action="confirm"]',
      '[data-project-action="accept-settings"]',
      '[data-project-action="submit"]',
      'button[type="submit"]',
      "button"
    ], surface).find((candidate) => {
      if (candidate.matches(
          '[data-project-action="confirm"],' +
          '[data-project-action="accept-settings"],' +
          '[data-project-action="submit"],button[type="submit"]')) {
        return true;
      }
      const text = normalize(candidate.textContent);
      return labels.map(normalize).some((label) => text === label);
    });
    click(button, `dialog confirmation (${labels.join("/")})`);
  }
  function cancelOpenDialog() {
    const dialog = openDialog();
    if (!dialog) return;
    const cancel = byText(
      ["button", '[role="button"]'],
      ["Cancel", "Close"],
      dialog);
    if (cancel) {
      cancel.click();
    } else if (typeof dialog.close === "function") {
      dialog.close();
    }
  }
  async function active() {
    const response = await fetch("/v1/projects/active", {
      headers: { Accept: "application/json" },
      cache: "no-store"
    });
    assert(response.ok, "Active-project read failed", {
      status: response.status
    });
    return response.json();
  }
  async function assertProjectInspection(label) {
    const response = await fetch("/v1/projects/active/inspection", {
      headers: { Accept: "application/json" },
      cache: "no-store"
    });
    const body = await response.text();
    assert(response.ok, `Project inspection failed after ${label}`, {
      status: response.status,
      body
    });
    return JSON.parse(body);
  }
  async function waitActive(predicate, label) {
    return waitFor(async () => {
      const snapshot = await active();
      return predicate(snapshot) ? snapshot : null;
    }, label);
  }
  function tabElements() {
    return unique([
      ...document.querySelectorAll('[role="tab"]'),
      ...document.querySelectorAll("[data-pamguard-tab-kind]"),
      ...document.querySelectorAll("[data-project-tab-kind]")
    ]);
  }
  function tabs() {
    return tabElements().map((element) => ({
      element,
      kind:
        element.getAttribute("data-pamguard-tab-kind") ||
        element.getAttribute("data-project-tab-kind") ||
        "",
      owner:
        element.getAttribute("data-owner-controlled-unit-id") ||
        element.getAttribute("data-owner-unit-id") ||
        "",
      name:
        element.getAttribute("aria-label") ||
        element.textContent.trim(),
      active:
        element.getAttribute("aria-selected") === "true" ||
        element.classList.contains("active")
    }));
  }
  function requestHistory() {
    requests = readRequests();
    return requests.slice();
  }
  function assertNoParallelWorkflow() {
    const forbidden = requestHistory().filter((request) => {
      const path = pathOf(request.url);
      return /^\/sessions(?:\/|$)/.test(path) ||
        /^\/workspaces(?:\/|$)/.test(path) ||
        /^\/module-graph(?:\/|$)/.test(path);
    });
    assert(
      forbidden.length === 0,
      "Normal project shell called a legacy/parallel workflow",
      forbidden);
  }
  function assertBlankShell(snapshot) {
    const currentTabs = tabs();
    const activeTabs = currentTabs.filter((tab) => tab.active);
    const phantomPanels = Array.from(document.querySelectorAll([
      "#tab-workspace",
      "#tab-spectrogram",
      "#tab-clicks",
      "#tab-detections",
      "#tab-archive",
      "#tab-console"
    ].join(","))).map((element) => element.id);
    const workspaceSurfaces = [
      "#workspaceAudioSource",
      "#workspaceArrangement",
      "#operatorGrid",
      "#operatorTabs",
      '[data-project-surface="workspace"]'
    ].filter((selector) => document.querySelector(selector));
    const displayRoots = document.querySelectorAll([
      "[data-pamguard-display-instance-id]",
      "[data-project-display-id]",
      "[data-display-instance-id]"
    ].join(","));
    assert(
      currentTabs.length === 1 &&
      normalize(currentTabs[0].name) === "data model" &&
      currentTabs[0].kind === "data-model" &&
      activeTabs.length === 1 &&
      activeTabs[0] === currentTabs[0],
      "Blank project is not Data Model-only",
      currentTabs.map(({ element, ...tab }) => tab));
    assert(
      phantomPanels.length === 0 &&
      workspaceSurfaces.length === 0 &&
      displayRoots.length === 0,
      "Blank shell contains a phantom legacy/display surface",
      { phantomPanels, workspaceSurfaces, displays: displayRoots.length });
    assert(
      snapshot.project.controlledUnits.length === 0 &&
      snapshot.project.displayTabs.length === 0,
      "Blank API authority contains units or display tabs",
      snapshot.project);
    const menubar = document.querySelector(".app-menubar");
    const projectBar = document.querySelector(".project-bar");
    const shellMain = document.querySelector(".shell-main");
    const statusbar = document.querySelector(".statusbar");
    const conflictBanner = document.getElementById("conflictBanner");
    const applicationMenus =
      document.querySelector(".application-menus");
    const emptyState = document.getElementById("canvasEmpty");
    const rectangles = [
      menubar,
      projectBar,
      shellMain,
      statusbar
    ].map((element) => element?.getBoundingClientRect());
    const layoutEvidence = rectangles.map((rectangle) => ({
      top: rectangle?.top,
      bottom: rectangle?.bottom,
      height: rectangle?.height
    }));
    assert(
      rectangles.every(Boolean) &&
      rectangles[0].bottom <= rectangles[1].top + 1 &&
      rectangles[1].bottom <= rectangles[2].top + 1 &&
      rectangles[2].bottom <= rectangles[3].top + 1 &&
      rectangles[2].height > 300 &&
      rectangles[3].height < 45 &&
      rectangles[3].bottom <= window.innerHeight + 1 &&
      getComputedStyle(applicationMenus).display === "flex" &&
      getComputedStyle(conflictBanner).display === "none" &&
      getComputedStyle(emptyState).pointerEvents !== "none",
      "Blank shell chrome collapsed, overlapped, or exposed hidden UI",
      layoutEvidence);
    const menuNames = queryVisible([
      "[data-project-menu] > button",
      'button[aria-haspopup="menu"]',
      ".menu > button"
    ]).map((element) => normalize(
      element.getAttribute("aria-label") || element.textContent));
    for (const required of [
      "file",
      "add modules",
      "settings",
      "display",
      "help"
    ]) {
      assert(
        menuNames.some((name) => name === required),
        `Project shell omitted the ${required} menu`,
        menuNames);
    }
    assert(
      Boolean(byText(
        ["button", '[role="button"]'],
        ["Start", "Stop"],
        document,
        { exact: true })),
      "Project shell omitted global Start/Stop");
    const ceremony = byText(
      ["button", '[role="button"]'],
      ["Validate", "Apply"],
      document,
      { exact: true });
    assert(
      !ceremony,
      "Operator shell still exposes Validate/Apply draft deployment");
    assert(
      document.querySelector(
        '[data-global-settings-type="pamguard.array-manager"]') &&
      document.querySelector(
        '[data-configuration-template-action="pamguard.click-monitoring"]') &&
      document.getElementById("emptyClickMonitoringTemplate"),
      "Blank project omitted Array Manager or Click monitoring template");
    assertNoParallelWorkflow();
  }
  function menuButton(kind, name) {
    return queryVisible([
      `[data-project-action="open-${kind}"]`,
      `[data-project-menu="${kind}"] > button`,
      `[data-project-menu="${kind}"]`,
      'button[aria-haspopup="menu"]',
      ".menu > button"
    ]).find((element) => {
      const semantic =
        element.getAttribute("data-project-action") ===
          `open-${kind}` ||
        element.getAttribute("data-project-menu") === kind ||
        element.parentElement?.getAttribute(
          "data-project-menu") === kind;
      return semantic ||
        normalize(
          element.getAttribute("aria-label") ||
          element.textContent) === normalize(name);
    }) || null;
  }
  async function openMenu(kind, name) {
    click(menuButton(kind, name), `${name} menu`);
    await sleep(50);
  }
  function unitNode(unitId, unitName = "") {
    const semantic = queryVisible([
      `[data-unit-id="${CSS.escape(unitId)}"]`,
      `[data-controlled-unit-id="${CSS.escape(unitId)}"]`
    ]).find((element) =>
      element.matches(
        "[data-project-node-kind='controlled-unit']," +
        "[data-pamguard-node-kind='controlled-unit']," +
        ".graph-node") ||
      !element.parentElement?.closest(
        `[data-unit-id="${CSS.escape(unitId)}"],` +
        `[data-controlled-unit-id="${CSS.escape(unitId)}"]`));
    if (semantic) return semantic;
    if (!unitName) return null;
    return byText([
      "[data-project-node-kind='controlled-unit']",
      "[data-pamguard-node-kind='controlled-unit']",
      ".graph-node"
    ], [unitName], document, { exact: false });
  }
  function graphPort(unitId, direction, roleId) {
    return document.querySelector(
      `.port-${CSS.escape(direction)}` +
      `[data-unit-id="${CSS.escape(unitId)}"]` +
      `[data-role-id="${CSS.escape(roleId)}"]`);
  }
  function graphWire(sourceUnitId, outputRole, targetUnitId, inputRole) {
    return document.querySelector(
      `.model-wire[data-source-unit-id="${CSS.escape(sourceUnitId)}"]` +
      `[data-source-output-role="${CSS.escape(outputRole)}"]` +
      `[data-target-unit-id="${CSS.escape(targetUnitId)}"]` +
      `[data-target-input-role="${CSS.escape(inputRole)}"]`);
  }
  async function reconnectGraph(source, target) {
    const dataModelTab = tabs().find(
      (tab) => tab.kind === "data-model");
    assert(dataModelTab, "Project shell omitted its Data Model tab");
    dataModelTab.element.click();
    const output = await waitFor(
      () => graphPort(source.unitId, "output", source.outputRole),
      `${source.unitName} ${source.outputRole} graph output`);
    const input = await waitFor(
      () => graphPort(target.unitId, "input", target.inputRole),
      `${target.unitName} ${target.inputRole} graph input`);
    const before = await active();
    output.click();
    await waitFor(
      () =>
        input.getAttribute("data-connection-state") === "compatible" &&
        input.classList.contains("is-compatible") &&
        input.getAttribute("aria-disabled") === "false" &&
        output.getAttribute("aria-pressed") === "true",
      "compatible graph reconnect target");
    input.click();
    const snapshot = await waitActive(
      (candidate) => {
        const unit = candidate.project.controlledUnits.find(
          (entry) => entry.id === target.unitId);
        const binding = unit?.bindings?.find(
          (entry) => entry.inputRole === target.inputRole);
        return candidate.workingRevision > before.workingRevision &&
          binding?.sources?.some((entry) =>
            entry.unitId === source.unitId &&
            entry.outputRole === source.outputRole);
      },
      "graph-port setBinding mutation");
    await waitFor(
      () => graphWire(
        source.unitId,
        source.outputRole,
        target.unitId,
        target.inputRole),
      "authoritative graph wire");
    return snapshot.project.controlledUnits.find(
      (unit) => unit.id === target.unitId);
  }
  async function assertGraphIncompatible(source, target) {
    const output = await waitFor(
      () => graphPort(source.unitId, "output", source.outputRole),
      `${source.unitName} incompatible-source graph output`);
    const input = await waitFor(
      () => graphPort(target.unitId, "input", target.inputRole),
      `${target.unitName} incompatible-target graph input`);
    output.click();
    await waitFor(
      () =>
        input.getAttribute("data-connection-state") === "incompatible" &&
        input.classList.contains("is-incompatible") &&
        input.getAttribute("aria-disabled") === "true" &&
        normalize(input.getAttribute("aria-label"))
          .includes("incompatible"),
      "incompatible graph visual/ARIA state");
    document.dispatchEvent(new KeyboardEvent("keydown", {
      key: "Escape",
      bubbles: true,
      cancelable: true
    }));
    await waitFor(
      () =>
        !input.hasAttribute("data-connection-state") &&
        output.getAttribute("aria-pressed") === "false",
      "graph connection cancellation");
  }
  async function assertDialogSource(unit, source) {
    await invokeUnitAction(
      unit,
      "configure",
      ["Configure", "Settings"]);
    const dialog = await waitFor(
      () => openDialog(),
      `${unit.name} source settings`);
    const select = dialog.querySelector(
      `select[data-input-role="${CSS.escape(source.inputRole)}"]`);
    assert(
      select &&
      (select.value ===
        `${source.unitId}|${source.outputRole}` ||
       select.selectedOptions[0]?.value.includes(source.unitId)),
      "Graph binding was not the selected source in module settings",
      {
        expected: source,
        value: select?.value,
        text: select?.selectedOptions[0]?.textContent
      });
    cancelOpenDialog();
    await waitFor(
      () => !openDialog(),
      `${unit.name} source settings cancellation`);
  }
  async function invokeUnitAction(unit, action, labels) {
    if (!unitNode(unit.id, unit.name)) {
      const dataModelTab = tabs().find(
        (tab) => tab.kind === "data-model");
      assert(
        dataModelTab,
        "Project shell omitted its permanent Data Model tab");
      dataModelTab.element.click();
      await sleep(50);
    }
    const node = await waitFor(
      () => unitNode(unit.id, unit.name),
      `${unit.name} controlled-unit node`);
    const direct = queryVisible([
      `[data-project-unit-action="${action}"]`,
      `[data-unit-action="${action}"]`
    ], node)[0];
    if (direct) {
      direct.click();
      await sleep(50);
      return;
    }
    const rectangle = node.getBoundingClientRect();
    node.dispatchEvent(new MouseEvent("contextmenu", {
      bubbles: true,
      cancelable: true,
      clientX: rectangle.left + Math.min(30, rectangle.width / 2),
      clientY: rectangle.top + Math.min(30, rectangle.height / 2)
    }));
    const item = await waitFor(() =>
      queryVisible([
        `[data-project-unit-action="${action}"]`,
        `[data-unit-action="${action}"]`,
        '[role="menuitem"]',
        "button"
      ]).find((candidate) => {
        if (candidate.closest(
            `[data-unit-id="${CSS.escape(unit.id)}"],` +
            `[data-controlled-unit-id="${CSS.escape(unit.id)}"]`) &&
            !candidate.matches(
              `[data-project-unit-action="${action}"],` +
              `[data-unit-action="${action}"]`)) {
          return false;
        }
        const semantic = candidate.getAttribute(
          "data-project-unit-action") === action ||
          candidate.getAttribute("data-unit-action") === action;
        const text = normalize(candidate.textContent);
        return semantic || labels.map(normalize).some(
          (label) => text === label || text.startsWith(label));
      }), `${unit.name} ${action} menu action`);
    const contextMenu = item.closest(
      "#unitContextMenu, .unit-context-menu");
    if (contextMenu) {
      const menuRectangle = contextMenu.getBoundingClientRect();
      assert(
        getComputedStyle(contextMenu).position === "fixed" &&
        menuRectangle.left >= 0 &&
        menuRectangle.top >= 0 &&
        menuRectangle.right <= window.innerWidth + 1 &&
        menuRectangle.bottom <= window.innerHeight + 1,
        "Controlled-unit context menu rendered outside the viewport",
        {
          left: menuRectangle.left,
          top: menuRectangle.top,
          right: menuRectangle.right,
          bottom: menuRectangle.bottom
        });
    }
    item.click();
    await sleep(50);
  }
  async function addUnit(typeId, paletteName, requestedName) {
    const before = await active();
    const existing = new Set(
      before.project.controlledUnits.map((unit) => unit.id));
    await openMenu("add-modules", "Add Modules");
    const item = await waitFor(() => {
      const semantic = queryVisible([
        `[data-project-add-unit-type="${CSS.escape(typeId)}"]`,
        `[data-controlled-unit-type-id="${CSS.escape(typeId)}"]` +
          '[data-project-action="add-unit"]',
        `[data-unit-type-id="${CSS.escape(typeId)}"]` +
          '[role="menuitem"]'
      ])[0];
      return semantic || byText(
        ['[role="menuitem"]', "button", '[role="button"]'],
        [paletteName],
        document,
        { exact: false, excludeNodes: true });
    }, `${paletteName} palette action`);
    item.click();
    await sleep(100);
    const dialog = openDialog();
    if (dialog) {
      const nameInput = textControl(
        dialog,
        ["unit-name", "unitName", "name"]);
      assert(
        nameInput,
        `${paletteName} add dialog omitted its instance name`);
      setControlValue(nameInput, requestedName);
      confirmDialog(dialog, ["Add", "Create", "OK"]);
    }
    const after = await waitActive(
      (snapshot) => snapshot.project.controlledUnits.some(
        (unit) =>
          unit.typeId === typeId &&
          !existing.has(unit.id)),
      `${paletteName} project mutation`);
    const created = after.project.controlledUnits.find(
      (unit) =>
        unit.typeId === typeId &&
        !existing.has(unit.id));
    assert(
      created.name === requestedName,
      `${paletteName} did not use the requested unique name`,
      created);
    await waitFor(
      () => unitNode(created.id, created.name),
      `${paletteName} rendered node`);
    return created;
  }
  async function suggestedUnitName(typeId, paletteName) {
    await openMenu("add-modules", "Add Modules");
    const item = await waitFor(() =>
      queryVisible([
        `[data-project-add-unit-type="${CSS.escape(typeId)}"]`,
        `[data-controlled-unit-type-id="${CSS.escape(typeId)}"]` +
          '[data-project-action="add-unit"]',
        `[data-unit-type-id="${CSS.escape(typeId)}"]` +
          '[role="menuitem"]'
      ])[0] || byText(
        ['[role="menuitem"]', "button", '[role="button"]'],
        [paletteName],
        document,
        { exact: false, excludeNodes: true }),
    `${paletteName} unique-name action`);
    item.click();
    const dialog = await waitFor(
      () => openDialog(),
      `${paletteName} unique-name dialog`);
    const input = textControl(
      dialog,
      ["unit-name", "unitName", "name"]);
    assert(input, `${paletteName} unique-name input is missing`);
    const suggestion = input.value;
    cancelOpenDialog();
    await waitFor(
      () => !openDialog(),
      `${paletteName} unique-name cancellation`);
    return suggestion;
  }
  async function assertDisplayMenuContributions(expected) {
    await openMenu("display", "Dynamic Display");
    const buttons = Array.from(document.querySelectorAll(
      "#displayMenu [data-display-provider-type-id]"));
    const actual = buttons.map((button) => ({
      ownerUnitId: button.dataset.displayOwnerUnitId,
      providerTypeId: button.dataset.displayProviderTypeId,
      disabled: button.disabled,
      ariaDisabled: button.getAttribute("aria-disabled")
    })).sort((left, right) =>
      `${left.ownerUnitId}|${left.providerTypeId}`.localeCompare(
        `${right.ownerUnitId}|${right.providerTypeId}`));
    const wanted = expected.map((entry) => ({
      ownerUnitId: entry.ownerUnitId,
      providerTypeId: entry.providerTypeId,
      disabled: entry.disabled,
      ariaDisabled: entry.disabled ? "true" : "false"
    })).sort((left, right) =>
      `${left.ownerUnitId}|${left.providerTypeId}`.localeCompare(
        `${right.ownerUnitId}|${right.providerTypeId}`));
    assert(
      JSON.stringify(actual) === JSON.stringify(wanted),
      "Dynamic Display menu contributions or maximum-instance state differ",
      { actual, expected: wanted });
    document.body.click();
    return actual;
  }
  function normalizedPointer(control) {
    const explicit =
      control.getAttribute("data-setting-pointer") ||
      control.getAttribute("data-json-pointer") ||
      control.getAttribute("data-click-setting");
    if (explicit) return explicit;
    const path = control.getAttribute("data-setting-path");
    if (path) {
      try {
        const parsed = JSON.parse(path);
        if (Array.isArray(parsed)) {
          return "/" + parsed.join("/");
        }
      } catch {
        return path;
      }
    }
    return "";
  }
  function settingControl(surface, pointer) {
    const key = pointer.split("/").filter(Boolean).at(-1);
    for (const candidate of queryVisible([
      "[data-setting-pointer]",
      "[data-json-pointer]",
      "[data-click-setting]",
      "[data-setting-path]",
      "input[name]",
      "select[name]",
      "textarea[name]"
    ], surface)) {
      const candidatePointer = normalizedPointer(candidate);
      const name = candidate.getAttribute("name") || "";
      if (candidatePointer === pointer ||
          name === pointer ||
          name === key ||
          name.endsWith(`.${key}`)) {
        if (candidate.matches("input,select,textarea")) {
          return candidate;
        }
        return candidate.querySelector("input,select,textarea");
      }
    }
    return null;
  }
  function selectSource(
    surface,
    inputRole,
    sourceUnitId,
    sourceUnitName,
    required = true) {
    const direct = queryVisible([
      `[data-input-role="${CSS.escape(inputRole)}"]` +
        `[data-source-unit-id="${CSS.escape(sourceUnitId)}"]`,
      `[data-project-source-unit-id="${CSS.escape(sourceUnitId)}"]` +
        `[data-project-input-role="${CSS.escape(inputRole)}"]`
    ], surface)[0];
    if (direct && !direct.matches("select")) {
      direct.click();
      return true;
    }
    const select = queryVisible([
      `select[data-input-role="${CSS.escape(inputRole)}"]`,
      `select[data-project-input-role="${CSS.escape(inputRole)}"]`,
      'select[name="source"]',
      'select[name="sourceId"]',
      'select[name="sourceName"]'
    ], surface)[0];
    if (!select) {
      assert(!required, `Missing ${inputRole} source selector`);
      return false;
    }
    const option = Array.from(select.options).find((candidate) =>
      candidate.getAttribute("data-source-unit-id") === sourceUnitId ||
      candidate.value === sourceUnitId ||
      candidate.value.includes(sourceUnitId) ||
      normalize(candidate.textContent).includes(
        normalize(sourceUnitName)));
    assert(option, `No compatible ${sourceUnitName} option`, {
      options: Array.from(select.options).map((item) => ({
        value: item.value,
        text: item.textContent
      }))
    });
    select.value = option.value;
    select.dispatchEvent(new Event("input", { bubbles: true }));
    select.dispatchEvent(new Event("change", { bubbles: true }));
    return true;
  }
  async function configureUnit(
    unit,
    changes,
    source = null) {
    const before = await active();
    await invokeUnitAction(
      unit,
      "configure",
      ["Configure", "Settings"]);
    const dialog = await waitFor(
      () => openDialog(),
      `${unit.name} settings dialog`);
    for (const selectedSource of (
        source
          ? (Array.isArray(source) ? source : [source])
          : [])) {
      const alreadyBound = before.project.controlledUnits
        .find((candidate) => candidate.id === unit.id)
        ?.bindings?.some((binding) =>
          binding.inputRole === selectedSource.role &&
          binding.sources?.some((candidate) =>
            candidate.unitId === selectedSource.unitId));
      selectSource(
        dialog,
        selectedSource.role,
        selectedSource.unitId,
        selectedSource.unitName,
        !alreadyBound);
    }
    if (unit.typeId === "pamguard.acquisition") {
      const tabs = Array.from(dialog.querySelectorAll(
        "[data-settings-tab]"));
      assert(
        tabs.length === 3 &&
        tabs.map((tab) => normalize(tab.textContent)).join("|") ===
          "data source|sampling & channels|calibration & dc",
        "Acquisition settings did not expose the PAMGuard workflow",
        tabs.map((tab) => tab.textContent));
      assert(
        dialog.querySelector("[data-acquisition-host-binding-note]") &&
        dialog.querySelectorAll("textarea").length === 0,
        "Acquisition editor leaked host selection or generic JSON",
        {
          note: dialog.querySelector(
            "[data-acquisition-host-binding-note]")?.textContent,
          textareas: dialog.querySelectorAll("textarea").length
        });
      for (const [tabId, pointers] of [
        ["acquisition-source", [
          "/daqSystemType"
        ]],
        ["acquisition-channels", [
          "/sampleRate",
          "/nChannels",
          "/hardwareChannelList/0",
          "/hydrophoneList/0"
        ]],
        ["acquisition-calibration", [
          "/voltsPeak2Peak",
          "/preamplifier/gainDb",
          "/preamplifier/bandwidthHz/0",
          "/preamplifier/bandwidthHz/1",
          "/subtractDC",
          "/dcTimeConstantSeconds"
        ]]
      ]) {
        const tab = tabs.find(
          (candidate) => candidate.dataset.settingsTab === tabId);
        tab.click();
        for (const pointer of pointers) {
          assert(
            settingControl(dialog, pointer),
            `Acquisition ${tabId} section omitted ${pointer}`);
        }
      }
      tabs.find(
        (candidate) =>
          candidate.dataset.settingsTab === "acquisition-source").click();
    }
    else if (unit.typeId === "pamguard.fft") {
      const tabs = Array.from(dialog.querySelectorAll(
        "[data-settings-tab]"));
      const expectedSourceSampleRate = Number(
        source?.sampleRateHz || 96000);
      assert(
        tabs.length === 3 &&
        tabs.map((tab) => normalize(tab.textContent)).join("|") ===
          "fft|click removal|spectral noise removal",
        "FFT settings did not expose the three authoritative sections",
        tabs.map((tab) => tab.textContent));
      const derived = dialog.querySelector("[data-fft-derived]");
      assert(
        derived &&
        normalize(
          derived.querySelector(
            "[data-derived-value='sample-rate']")?.textContent)
          .includes(
            `${expectedSourceSampleRate.toLocaleString()} hz`),
        "FFT settings did not derive the selected source sample rate",
        {
          expectedSourceSampleRate,
          derived: derived?.textContent
        });
      for (const [tabId, pointer] of [
        ["click-removal", "/fft/clickRemoval"],
        ["spectral-noise", "/spectralNoise/medianFilter"]
      ]) {
        const tab = tabs.find(
          (candidate) => candidate.dataset.settingsTab === tabId);
        tab.click();
        assert(
          settingControl(dialog, pointer),
          `FFT ${tabId} section omitted ${pointer}`);
      }
      tabs.find(
        (candidate) => candidate.dataset.settingsTab === "fft").click();
    }
    else if (unit.typeId === "pamguard.sound-output") {
      const tabs = Array.from(dialog.querySelectorAll(
        "[data-settings-tab]"));
      assert(
        tabs.length === 2 &&
        tabs.map((tab) => normalize(tab.textContent)).join("|") ===
          "playback|side bar",
        "Sound Output settings did not expose PAMGuard Playback/Side Bar",
        tabs.map((tab) => tab.textContent));
      assert(
        dialog.querySelector("[data-sound-output-host-note]") &&
        dialog.querySelector("[data-sound-output-device]") &&
        dialog.querySelector(
          "[data-sound-output-action='listen']") &&
        dialog.querySelector(
          "[data-sound-output-action='stop']") &&
        dialog.querySelector("[data-sound-output-status]") &&
        dialog.querySelectorAll("textarea").length === 0,
        "Sound Output did not separate structured project settings from " +
          "browser-local listening controls");
      await waitFor(
        () => dialog.querySelectorAll(
          "[data-sound-output-channel]").length >= 2,
        "Sound Output channel metadata from selected data block");
      for (const pointer of [
        "/defaultSampleRate",
        "/playbackRateHz",
        "/channelBitmap/0",
        "/channelBitmap/1"
      ]) {
        assert(
          settingControl(dialog, pointer),
          `Sound Output Playback section omitted ${pointer}`);
      }
      tabs.find(
        (candidate) =>
          candidate.dataset.settingsTab ===
            "sound-output-sidebar").click();
      for (const pointer of [
        "/playbackSpeed",
        "/playbackGainDb",
        "/hpFilter"
      ]) {
        assert(
          settingControl(dialog, pointer),
          `Sound Output Side Bar omitted ${pointer}`);
      }
      tabs.find(
        (candidate) =>
          candidate.dataset.settingsTab ===
            "sound-output-playback").click();
    }
    else if (unit.typeId === "pamguard.click-detector") {
      const primary = Array.from(dialog.querySelectorAll(
        ".click-settings-tab"));
      const actions = Array.from(dialog.querySelectorAll(
        ".click-settings-action"));
      assert(
        primary.map((tab) => normalize(tab.textContent)).join("|") ===
          "source|trigger|click length|delays|echoes|noise" &&
        actions.map((tab) => normalize(tab.textContent)).join("|") ===
          "digital pre-filter|digital trigger filter|angle vetoes|" +
          "classification|train id|train localisation" &&
        dialog.querySelector("[data-pamguard-click-settings-editor]") &&
        normalize(dialog.textContent).includes(
          "manual tracked events / target motion"),
        "Click Detector did not expose its PAMGuard primary/actions layout",
        {
          primary: primary.map((tab) => tab.textContent),
          actions: actions.map((tab) => tab.textContent)
        });
    }
    else if (
        unit.typeId === "pamguard.matched-template-classifier") {
      const editor = dialog.querySelector(
        "[data-pamguard-matched-template-settings-editor]");
      const status = await waitFor(() => {
        const candidate = dialog.querySelector(
          "[data-matched-template-preset-status]");
        return candidate?.getAttribute(
          "data-matched-template-preset-status") === "ready"
          ? candidate
          : null;
      }, "Matched Template PAMGuard preset library");
      const classifierTabs = Array.from(editor?.querySelectorAll(
        "[data-matched-template-tab]") || []);
      const classifierPanels = Array.from(editor?.querySelectorAll(
        "[data-matched-template-panel]") || []);
      const activePanel = classifierPanels.find(
        (panel) => !panel.hidden);
      const matchPane = activePanel?.querySelector(
        '[data-matched-template-role="matchTemplate"]');
      const rejectPane = activePanel?.querySelector(
        '[data-matched-template-role="rejectTemplate"]');
      const matchPreset = matchPane?.querySelector(
        '[data-matched-template-action="choose-preset"]');
      const rejectPreset = rejectPane?.querySelector(
        '[data-matched-template-action="choose-preset"]');
      const duration = editor?.querySelector(
        "[data-matched-template-restricted-duration]");
      await waitFor(
        () => /42\.67 ms/i.test(duration?.textContent || "")
          ? duration
          : null,
        "Matched Template 48 kHz source-derived click duration");
      const presetNames = (select) =>
        Array.from(select?.options || []).map(
          (option) => normalize(option.textContent));
      const waveformPreviews = Array.from(
        activePanel?.querySelectorAll(
          ".matched-template-waveform svg") || []);
      const unavailableMatActions = Array.from(
        editor?.querySelectorAll(
          '[data-matched-template-action="mat-unavailable"]') || []);
      assert(
        editor &&
        isVisible(editor) &&
        normalize(status.textContent).includes(
          "pamguard template presets available") &&
        classifierTabs.length === 1 &&
        classifierPanels.length === 1 &&
        activePanel &&
        isVisible(activePanel) &&
        normalize(editor.querySelector(
          ".matched-template-settings-section h4")?.textContent) ===
            "general classifier settings" &&
        settingControl(editor, "/channelClassification") &&
        settingControl(editor, "/clickType") &&
        settingControl(editor, "/peakSearch") &&
        settingControl(editor, "/restrictedBins") &&
        settingControl(editor, "/lengthDb") &&
        settingControl(editor, "/peakSmoothing") &&
        settingControl(editor, "/normalisationType") &&
        settingControl(editor, "/classifiers/0/thresholdToAccept") &&
        settingControl(
          editor,
          "/classifiers/0/matchTemplate/name")?.value ===
            "Beaked Whale" &&
        Number(settingControl(
          editor,
          "/classifiers/0/matchTemplate/sampleRateHz")?.value) ===
            192000 &&
        settingControl(
          editor,
          "/classifiers/0/rejectTemplate/name")?.value ===
            "Dolphin" &&
        Number(settingControl(
          editor,
          "/classifiers/0/rejectTemplate/sampleRateHz")?.value) ===
            192000 &&
        matchPane &&
        rejectPane &&
        isVisible(matchPane) &&
        isVisible(rejectPane) &&
        waveformPreviews.length === 2 &&
        waveformPreviews.every(isVisible) &&
        presetNames(matchPreset).includes("beaked whale click") &&
        presetNames(rejectPreset).includes("dolphin click") &&
        matchPane.querySelector(
          '[data-matched-template-action="import-csv"]') &&
        rejectPane.querySelector(
          '[data-matched-template-action="import-csv"]') &&
        unavailableMatActions.length === 2 &&
        unavailableMatActions
          .every((button) => button.disabled) &&
        editor.querySelectorAll("textarea").length === 0,
        "Matched Template did not expose its dedicated PAMGuard-ordered " +
          "editor, exact default templates, waveform previews, or 48 kHz " +
          "source derivation",
        {
          status: status.textContent,
          duration: duration?.textContent,
          tabs: classifierTabs.map((tab) => tab.textContent),
          panels: classifierPanels.length,
          matchPresets: presetNames(matchPreset),
          rejectPresets: presetNames(rejectPreset),
          editorText: editor?.textContent
        });
    }
    else if (unit.typeId === "pamguard.mht-click-train") {
      const editor = dialog.querySelector(
        "[data-pamguard-mht-click-train-settings-editor]");
      const tabs = Array.from(editor?.querySelectorAll(
        "[data-mht-click-train-tab]") || []);
      assert(
        editor &&
        tabs.map((tab) => normalize(tab.textContent)).join("|") ===
          "detector|pre classifier|species classifiers" &&
        settingControl(editor, "/channelGroups/1") &&
        settingControl(editor, "/kernel/nHold") &&
        settingControl(editor, "/chi2/maximumIciSeconds") &&
        editor.querySelector(
          '[data-setting-pointer="/classifier/preClassifier/chi2Threshold"]') &&
        editor.querySelector(
          '[data-setting-pointer="/classifier/runClassifier"]') &&
        settingControl(editor, "/localisation/enabled")?.disabled &&
        normalize(editor.querySelector(
          "[data-mht-click-train-source-summary]")?.textContent)
          .includes("source channel") &&
        editor.querySelectorAll("textarea").length === 0,
        "MHT Click Train did not expose its dedicated Detector, Pre " +
          "Classifier, and Species Classifiers workflow",
        {
          tabs: tabs.map((tab) => tab.textContent),
          source: editor?.querySelector(
            "[data-mht-click-train-source-summary]")?.textContent,
          text: editor?.textContent
        });
    }
    else if (unit.typeId === "pamguard.ishmael-energy-sum" ||
        unit.typeId === "pamguard.ishmael-sgram-corr" ||
        unit.typeId === "pamguard.ishmael-match-filter") {
      const editor = dialog.querySelector(
        `[data-pamguard-ishmael-settings-editor="${CSS.escape(
          unit.typeId)}"]`);
      const sections = Array.from(editor?.querySelectorAll(
        "[data-ishmael-section]") || []);
      assert(
        editor &&
        sections.map((section) =>
          section.getAttribute("data-ishmael-section")).join("|") ===
            "source|detector|peak" &&
        settingControl(editor, "/groupingType") &&
        settingControl(editor, "/threshold") &&
        settingControl(editor, "/minTimeSeconds") &&
        settingControl(editor, "/maxTimeSeconds") &&
        settingControl(editor, "/refractoryTimeSeconds") &&
        normalize(editor.querySelector(
          "[data-ishmael-source-summary]")?.textContent)
          .includes("binding exposes") &&
        editor.querySelectorAll("textarea").length === 0,
        "Ishmael editor did not expose its source, detector, and shared " +
          "peak-picker sections",
        {
          typeId: unit.typeId,
          sections: sections.map((section) =>
            section.getAttribute("data-ishmael-section")),
          source: editor?.querySelector(
            "[data-ishmael-source-summary]")?.textContent,
          text: editor?.textContent
        });
      if (unit.typeId === "pamguard.ishmael-energy-sum") {
        assert(
          editor.querySelector(
            '[data-ishmael-detector="energy-sum"]') &&
          settingControl(editor, "/f0Hz") &&
          settingControl(editor, "/f1Hz") &&
          settingControl(editor, "/useRatio") &&
          settingControl(editor, "/adaptiveThreshold"),
          "Ishmael Energy Sum omitted its Java detector controls",
          editor.textContent);
      }
      else if (unit.typeId === "pamguard.ishmael-sgram-corr") {
        assert(
          editor.querySelector(
            '[data-ishmael-detector="spectrogram-correlation"]') &&
          settingControl(editor, "/spreadHz") &&
          editor.querySelector("[data-ishmael-contour-preview]") &&
          editor.querySelector(
            '[data-ishmael-action="add-segment"]'),
          "Ishmael Spectrogram Correlation omitted its structured " +
            "segment editor and preview",
          editor.textContent);
        if (!editor.querySelector("[data-ishmael-segment-row]")) {
          editor.querySelector(
            '[data-ishmael-action="add-segment"]').click();
        }
      }
      else {
        const fileInput = editor.querySelector(
          "[data-ishmael-kernel-file]");
        assert(
          editor.querySelector(
            '[data-ishmael-detector="matched-filter"]') &&
          fileInput &&
          editor.querySelector("[data-ishmael-kernel-history]") &&
          editor.querySelector("[data-ishmael-kernel-waveform]"),
          "Ishmael Matched Filter omitted its kernel import, history, " +
            "or waveform surface",
          editor.textContent);
        if (!editor.querySelector(
            '[data-setting-pointer="/kernelFilenameList/0"]')) {
          const sampleCount = 16;
          const bytes = new ArrayBuffer(44 + sampleCount * 2);
          const view = new DataView(bytes);
          const text = (offset, value) => {
            for (let index = 0; index < value.length; index += 1) {
              view.setUint8(offset + index, value.charCodeAt(index));
            }
          };
          text(0, "RIFF");
          view.setUint32(4, 36 + sampleCount * 2, true);
          text(8, "WAVE");
          text(12, "fmt ");
          view.setUint32(16, 16, true);
          view.setUint16(20, 1, true);
          view.setUint16(22, 1, true);
          view.setUint32(24, 48000, true);
          view.setUint32(28, 96000, true);
          view.setUint16(32, 2, true);
          view.setUint16(34, 16, true);
          text(36, "data");
          view.setUint32(40, sampleCount * 2, true);
          for (let index = 0; index < sampleCount; index += 1) {
            view.setInt16(
              44 + index * 2,
              Math.round(24000 * Math.sin(index * Math.PI / 4)),
              true);
          }
          const transfer = new DataTransfer();
          transfer.items.add(new File(
            [bytes],
            "browser-ishmael-kernel.wav",
            { type: "audio/wav" }));
          fileInput.files = transfer.files;
          fileInput.dispatchEvent(
            new Event("change", { bubbles: true }));
          await waitFor(
            () => editor.querySelector(
              '[data-setting-pointer="/kernelFilenameList/0"]'),
            "Ishmael Matched Filter WAV first-channel import");
        }
      }
    }
    else if (unit.typeId === "pamguard.fft-noise-monitor" ||
        unit.typeId === "pamguard.noise-band-monitor" ||
        unit.typeId === "pamguard.ltsa") {
      const editor = dialog.querySelector(
        `[data-pamguard-noise-ltsa-settings-editor="${CSS.escape(
          unit.typeId)}"]`);
      const requiredPointers = unit.typeId ===
          "pamguard.fft-noise-monitor"
        ? [
            "/channelBitmap/0",
            "/measurementIntervalSeconds",
            "/nMeasures",
            "/useAll"
          ]
        : unit.typeId === "pamguard.noise-band-monitor"
          ? [
              "/channelBitmap/0",
              "/outputIntervalSeconds",
              "/bandType",
              "/filterType",
              "/minimumFrequencyHz",
              "/maximumFrequencyHz"
            ]
          : [
              "/channelBitmap/0",
              "/intervalSeconds",
              "/longerFactor"
            ];
      assert(
        editor &&
        requiredPointers.every(
          (pointer) => settingControl(editor, pointer)) &&
        editor.querySelectorAll("textarea").length === 0,
        `${unit.name} did not expose its dedicated PAMGuard settings`,
        {
          requiredPointers,
          text: editor?.textContent
        });
      if (unit.typeId === "pamguard.fft-noise-monitor") {
        const standardBands = editor.querySelector(
          '[data-noise-standard-band="thirdOctave"]');
        assert(
          standardBands,
          "Noise Monitor omitted Java standard band creation");
        standardBands.checked = true;
        standardBands.dispatchEvent(
          new Event("change", { bubbles: true }));
      }
    }
    else if (unit.typeId === "pamguard.whistles-moans") {
      const editor = dialog.querySelector(
        "[data-pamguard-whistle-moan-settings-editor]");
      const tabs = Array.from(editor?.querySelectorAll(
        "[data-whistle-tab]") || []);
      assert(
        editor &&
        tabs.map((tab) => normalize(tab.textContent)).join("|") ===
          "detection|noise and thresholding" &&
        settingControl(editor, "/channelBitmap/0") &&
        settingControl(editor, "/minFrequencyHz") &&
        editor.querySelector(
          '[data-setting-pointer="/noiseReduction/medianFilter"]') &&
        editor.querySelectorAll("textarea").length === 0,
        "Whistle and Moan Detector did not expose its dedicated " +
          "Detection and Noise and Thresholding workflow",
        {
          tabs: tabs.map((tab) => tab.textContent),
          text: editor?.textContent
        });
    }
    else if (unit.typeId === "pamguard.amplifier") {
      assert(
        dialog.querySelector(
          '[data-signal-routing-editor="amplifier"]') &&
        settingControl(dialog, "/channelSettings/0/gainDb") &&
        settingControl(dialog, "/channelSettings/0/invert") &&
        settingControl(dialog, "/channelSettings/1/gainDb") &&
        dialog.querySelectorAll("textarea").length === 0,
        "Signal Amplifier did not expose active-source Java channel rows",
        dialog.textContent);
    }
    else if (unit.typeId === "pamguard.patch-panel") {
      assert(
        dialog.querySelector(
          '[data-signal-routing-editor="patch-panel"]') &&
        settingControl(dialog, "/routingMatrix/0/0") &&
        settingControl(dialog, "/routingMatrix/0/31") &&
        settingControl(dialog, "/routingMatrix/1/31") &&
        dialog.querySelector(
          '[data-setting-pointer="/advancedGainMatrix/enabled"]') &&
        normalize(dialog.textContent).includes("c++ extension") &&
        dialog.querySelectorAll("textarea").length === 0,
        "Patch Panel did not expose the Java matrix and labelled Advanced " +
          "extension",
        dialog.textContent);
    }
    else if (unit.typeId === "pamguard.filter") {
      assert(
        dialog.querySelector(
          '[data-pamguard-filter-settings-editor="pamguard.filter"]') &&
        settingControl(dialog, "/channelBitmap/0") &&
        settingControl(dialog, "/channelBitmap/1") &&
        settingControl(dialog, "/type") &&
        settingControl(dialog, "/band") &&
        settingControl(dialog, "/order") &&
        settingControl(dialog, "/lowPassFreqHz") &&
        settingControl(dialog, "/highPassFreqHz") &&
        dialog.querySelector("[data-filter-response-preview]") &&
        dialog.querySelectorAll("textarea").length === 0,
        "Filter did not expose PAMGuard source channels, type, response, " +
          "parameters, and response preview",
        dialog.textContent);
    }
    else if (unit.typeId === "pamguard.decimator") {
      assert(
        dialog.querySelector(
          '[data-pamguard-filter-settings-editor="pamguard.decimator"]') &&
        dialog.querySelector("[data-decimator-source-rate]") &&
        settingControl(dialog, "/channelBitmap/0") &&
        settingControl(dialog, "/outputSampleRateHz") &&
        settingControl(dialog, "/interpolation") &&
        settingControl(dialog, "/filter/type") &&
        settingControl(dialog, "/filter/band") &&
        settingControl(dialog, "/filter/order") &&
        dialog.querySelector(
          '[data-filter-action="decimator-default-filter"]') &&
        dialog.querySelectorAll("textarea").length === 0,
        "Decimator did not expose PAMGuard rate, channel, interpolation, " +
          "and anti-alias filter settings",
        dialog.textContent);
    }
    else if (unit.typeId === "pamguard.level-meter") {
      assert(
        dialog.querySelector(
          "[data-pamguard-level-meter-settings-editor]") &&
        settingControl(dialog, "/minLevel") &&
        settingControl(dialog, "/scaleReference") &&
        settingControl(dialog, "/scaleType") &&
        normalize(dialog.textContent).includes("scale selection") &&
        normalize(dialog.textContent).includes(
          "relative to full scale") &&
        normalize(dialog.textContent).includes("micropascal") &&
        dialog.querySelectorAll("textarea").length === 0,
        "Level Meter did not expose its PAMGuard source, peak/RMS, " +
          "reference, and scale-range settings",
        dialog.textContent);
    }
    for (const change of changes) {
      const direct = dialog.querySelector(
        `[data-setting-pointer="${CSS.escape(change.pointer)}"],` +
        `[data-click-setting="${CSS.escape(change.pointer)}"]`);
      const panelId = direct?.closest("[data-settings-panel]")
        ?.getAttribute("data-settings-panel");
      dialog.querySelector(
        `[data-settings-tab="${CSS.escape(panelId || "")}"]`)
        ?.click();
      const rolePanel = direct?.closest("[role='tabpanel']");
      if (rolePanel?.id) {
        dialog.querySelector(
          `[role='tab'][aria-controls="${CSS.escape(rolePanel.id)}"]`)
          ?.click();
      }
      const control = settingControl(dialog, change.pointer);
      assert(
        control,
        `${unit.name} settings omitted ${change.pointer}`,
        {
          dialogTitle:
            dialog.querySelector("h1,h2,h3")?.textContent || "",
          controls: Array.from(dialog.querySelectorAll(
            "input,select,textarea")).map((candidate) => ({
              tag: candidate.tagName,
              type: candidate.type || "",
              name: candidate.name || "",
              pointer:
                candidate.getAttribute("data-setting-pointer") || "",
              path:
                candidate.getAttribute("data-setting-path") || "",
              visible: isVisible(candidate)
            }))
        });
      setControlValue(control, change.value);
    }
    if (unit.typeId === "pamguard.fft" && source?.sampleRateHz) {
      const sampleRate = Number(source.sampleRateHz);
      const fftLength = Number(
        settingControl(dialog, "/fft/fftLength")?.value);
      const fftHop = Number(
        settingControl(dialog, "/fft/fftHop")?.value);
      const derived = dialog.querySelector("[data-fft-derived]");
      const sampleRateText = normalize(derived?.querySelector(
        "[data-derived-value='sample-rate']")?.textContent);
      const binWidthText = normalize(derived?.querySelector(
        "[data-derived-value='bin-width']")?.textContent);
      const timeStepText = normalize(derived?.querySelector(
        "[data-derived-value='time-step']")?.textContent);
      assert(
        sampleRateText.includes(`${sampleRate.toLocaleString()} hz`) &&
        binWidthText === `${(sampleRate / fftLength).toFixed(3)} hz` &&
        timeStepText ===
          `${(fftHop / sampleRate * 1000).toFixed(3)} ms`,
        "FFT derived resolution did not follow its selected source rate",
        {
          sampleRate,
          fftLength,
          fftHop,
          sampleRateText,
          binWidthText,
          timeStepText
        });
    }
    if (unit.typeId === "pamguard.matched-template-classifier") {
      const invalidControls = Array.from(dialog.querySelectorAll(
        "input,select,textarea")).filter(
          (control) =>
            typeof control.checkValidity === "function" &&
            !control.checkValidity());
      assert(
        invalidControls.length === 0,
        "Matched Template dialog contained browser-invalid controls",
        invalidControls.map((control) => ({
          pointer: control.getAttribute("data-setting-pointer"),
          type: control.type,
          value: control.value,
          min: control.min,
          max: control.max,
          step: control.step,
          validationMessage: control.validationMessage
        })));
    }
    confirmDialog(dialog, ["OK", "Save", "Apply"]);
    let after;
    try {
      after = await waitActive(
        (snapshot) => {
          const errors = queryVisible([
            ".toast-error",
            "[role='alert']"
          ]).map((candidate) => normalize(candidate.textContent));
          if (errors.length) {
            throw new Error(
              `${unit.name} settings reported: ${errors.join(" | ")}`);
          }
          return snapshot.workingRevision > before.workingRevision;
        },
        `${unit.name} immediate settings mutation`);
    }
    catch (error) {
      const snapshot = await active();
      const current = snapshot.project.controlledUnits.find(
        (candidate) => candidate.id === unit.id);
      const errors = queryVisible([
        ".toast-error",
        "[role='alert']"
      ]).map((candidate) => normalize(candidate.textContent));
      throw new Error(
        `${error.message}; revision ${before.workingRevision} -> ` +
        `${snapshot.workingRevision}; alerts=${JSON.stringify(errors)}; ` +
        `unit=${JSON.stringify(current)}`);
    }
    return after.project.controlledUnits.find(
      (candidate) => candidate.id === unit.id);
  }
  async function configureClickClassifierTypes(unit) {
    const before = await active();
    await invokeUnitAction(
      unit,
      "configure",
      ["Configure", "Settings"]);
    const dialog = await waitFor(
      () => openDialog(),
      `${unit.name} structured classifier settings`);
    const classificationAction = Array.from(
      dialog.querySelectorAll(".click-settings-action"))
      .find((candidate) =>
        normalize(candidate.textContent) === "classification");
    assert(
      classificationAction,
      "Click Detector omitted its Classification action");
    classificationAction.click();
    const mode = settingControl(
      dialog,
      "/classification/mode");
    const runOnline = settingControl(
      dialog,
      "/classification/runOnline");
    const checkAll = settingControl(
      dialog,
      "/classification/checkAllClassifiers");
    assert(
      mode && runOnline && checkAll &&
      dialog.querySelectorAll("textarea").length === 0,
      "Click Classification leaked generic JSON or omitted its policies");

    const listByTitle = (title) =>
      Array.from(dialog.querySelectorAll(
        ".click-settings-list-editor")).find((candidate) =>
        normalize(candidate.querySelector("h4")?.textContent) ===
          normalize(title));
    const addOne = (title) => {
      const list = listByTitle(title);
      assert(list, `Click Classification omitted ${title}`);
      const add = list.querySelector(".click-settings-add");
      assert(add && !add.disabled, `${title} Add action is unavailable`);
      click(add, `${title} Add`);
      return list;
    };

    setControlValue(mode, "basic");
    const basic = addOne("Basic classifier types");
    const basicName = basic.querySelector(
      "[data-click-setting='/classification/basicTypes/*/name']");
    const basicCode = basic.querySelector(
      "[data-click-setting='/classification/basicTypes/*/speciesCode']");
    const basicEnabled = basic.querySelector(
      "[data-click-setting='/classification/basicTypes/*/enabled']");
    assert(
      basicName && basicCode && basicEnabled &&
      basic.querySelector("[data-click-criterion='energy']") &&
      basic.querySelector("[data-click-criterion='peak']"),
      "Basic classifier editor omitted structured scientific fields");
    setControlValue(basicName, "Browser Basic Type");
    setControlValue(basicCode, 21);
    setControlValue(basicEnabled, false);

    setControlValue(mode, "sweep");
    const sweep = addOne("Sweep classifier types");
    const sweepName = sweep.querySelector(
      "[data-click-setting='/classification/sweepTypes/*/name']");
    const sweepCode = sweep.querySelector(
      "[data-click-setting='/classification/sweepTypes/*/speciesCode']");
    const sweepEnabled = sweep.querySelector(
      "[data-click-setting='/classification/sweepTypes/*/enabled']");
    assert(
      sweepName && sweepCode && sweepEnabled &&
      sweep.querySelector("[data-click-criterion='energy']") &&
      sweep.querySelector("[data-click-criterion='fft-filter']") &&
      sweep.querySelector("[data-click-criterion='zero-crossings']"),
      "Sweep classifier editor omitted structured scientific fields");
    setControlValue(sweepName, "Browser Sweep Type");
    setControlValue(sweepCode, 22);
    setControlValue(sweepEnabled, true);

    setControlValue(mode, "basic");
    setControlValue(runOnline, true);
    setControlValue(checkAll, true);
    confirmDialog(dialog, ["OK"]);
    const after = await waitActive(
      (snapshot) =>
        snapshot.workingRevision > before.workingRevision,
      `${unit.name} structured classifier mutation`);
    const configured = after.project.controlledUnits.find(
      (candidate) => candidate.id === unit.id);
    assert(
      configured.settings.classification.runOnline === true &&
      configured.settings.classification.mode === "basic" &&
      configured.settings.classification.checkAllClassifiers === true &&
      configured.settings.classification.basicTypes.length === 1 &&
      configured.settings.classification.basicTypes[0].name ===
        "Browser Basic Type" &&
      configured.settings.classification.basicTypes[0].speciesCode === 21 &&
      configured.settings.classification.basicTypes[0].enabled === false &&
      configured.settings.classification.sweepTypes.length === 1 &&
      configured.settings.classification.sweepTypes[0].name ===
        "Browser Sweep Type" &&
      configured.settings.classification.sweepTypes[0].speciesCode === 22,
      "Structured Click classifier types did not round-trip",
      configured.settings.classification);
    return configured;
  }
  function arrayManagerComponent(snapshot) {
    return snapshot.project.globalSettings.components.find(
      (component) =>
        component.typeId === "pamguard.array-manager") || null;
  }
  async function configureArrayManager(arrayName, cancel = false) {
    const before = await active();
    const original = arrayManagerComponent(before);
    assert(original, "Blank project omitted Array Manager settings");
    await openMenu("settings", "Settings");
    const action = queryVisible([
      '[data-global-settings-type="pamguard.array-manager"]'
    ])[0];
    click(action, "Array Manager settings");
    const dialog = await waitFor(
      () => openDialog(),
      "Array Manager settings dialog");
    const tabs = Array.from(dialog.querySelectorAll(
      "[data-array-settings-tab]"));
    assert(
      tabs.map((tab) => normalize(tab.textContent)).join("|") ===
        "instrument identity|environment|streamers|" +
        "hydrophone elements|channel mapping" &&
      dialog.querySelectorAll("textarea").length === 0 &&
      dialog.querySelector("[data-array-streamers]") &&
      dialog.querySelector("[data-array-hydrophones]") &&
      dialog.querySelector("[data-array-channel-map]"),
      "Array Manager did not expose its dedicated five-section editor",
      tabs.map((tab) => tab.textContent));
    const input = dialog.querySelector(
      '[data-array-setting="arrayName"]');
    assert(input, "Array Manager omitted its array name");
    setControlValue(input, arrayName);
    if (cancel) {
      cancelOpenDialog();
      await sleep(80);
      const unchanged = await active();
      assert(
        unchanged.workingRevision === before.workingRevision &&
        arrayManagerComponent(unchanged).settings.arrayName ===
          original.settings.arrayName,
        "Cancelling Array Manager settings changed the project",
        {
          beforeRevision: before.workingRevision,
          afterRevision: unchanged.workingRevision,
          beforeName: original.settings.arrayName,
          afterName:
            arrayManagerComponent(unchanged).settings.arrayName
        });
      return arrayManagerComponent(unchanged);
    }
    confirmDialog(dialog, ["OK"]);
    const after = await waitActive(
      (snapshot) =>
        snapshot.workingRevision > before.workingRevision &&
        arrayManagerComponent(snapshot)?.settings?.arrayName ===
          arrayName,
      "Array Manager immediate settings mutation");
    return arrayManagerComponent(after);
  }
  async function acquisitionInventory(unitId) {
    const response = await fetch(
      "/v1/projects/active/acquisitions",
      {
        headers: { Accept: "application/json" },
        cache: "no-store"
      });
    assert(
      response.ok,
      "Active Acquisition inventory read failed",
      { status: response.status });
    const body = await response.json();
    return body.acquisitions.find(
      (candidate) => candidate.unitId === unitId) || null;
  }
  async function configureAcquisitionHost(
    unit,
    streamUrl,
    cancel = false) {
    const before = await acquisitionInventory(unit.id);
    assert(before, `${unit.name} omitted from Acquisition inventory`);
    await openMenu("settings", "Settings");
    const hostAction = await waitFor(() => {
      const candidate = queryVisible([
        `[data-acquisition-host-action="${CSS.escape(unit.id)}"]`
      ])[0];
      return candidate && !candidate.disabled ? candidate : null;
    }, `${unit.name} enabled host input action`);
    click(hostAction, `${unit.name} host input`);
    const dialog = await waitFor(
      () => openDialog(),
      `${unit.name} host input dialog`);
    const kind = dialog.querySelector("[data-acquisition-host-kind]");
    const url = dialog.querySelector("[data-acquisition-host-url]");
    assert(
      dialog.querySelector(
        `[data-acquisition-host-editor="${CSS.escape(unit.id)}"]`) &&
      dialog.querySelector("[data-acquisition-configuration-status]") &&
      dialog.querySelector("[data-acquisition-capture-status]") &&
      dialog.querySelector(
        "[data-acquisition-capture-action='start']") &&
      dialog.querySelector(
        "[data-acquisition-capture-action='stop']") &&
      kind && url,
      "Acquisition host dialog omitted binding/capture ownership");
    setControlValue(kind, "url");
    setControlValue(url, streamUrl);
    if (cancel) {
      cancelOpenDialog();
      await sleep(80);
      const unchanged = await acquisitionInventory(unit.id);
      assert(
        unchanged.configurationStatus === before.configurationStatus &&
        unchanged.hostBindingRevision === before.hostBindingRevision,
        "Cancelling host input changed deployment state",
        { before, unchanged });
      return unchanged;
    }
    confirmDialog(dialog, ["Apply binding"]);
    return waitFor(async () => {
      const configured = await acquisitionInventory(unit.id);
      return configured?.configurationStatus === "configured" &&
        configured.hostBindingRevision !== null
        ? configured
        : null;
    }, `${unit.name} host binding`, 10000);
  }
  async function clickMonitoringTemplate(create = false) {
    const before = await active();
    const shortcut =
      document.getElementById("emptyClickMonitoringTemplate");
    if (shortcut && isVisible(shortcut)) {
      click(shortcut, "blank-project Click monitoring template");
    }
    else {
      await openMenu("add-modules", "Add Modules");
      click(queryVisible([
        '[data-configuration-template-action="pamguard.click-monitoring"]'
      ])[0], "Click monitoring configuration template");
    }
    const dialog = await waitFor(
      () => openDialog(),
      "Click monitoring template preview");
    const preview = dialog.querySelector(
      '[data-configuration-template-preview="pamguard.click-monitoring"]');
    const modules = Array.from(dialog.querySelectorAll(
      "[data-template-module]")).map(
        (element) => element.dataset.templateModule);
    assert(
      preview &&
      ["pamguard.acquisition", "pamguard.fft",
       "pamguard.user-display", "pamguard.click-detector",
       "pamguard.sound-output"].every(
        (typeId) => modules.includes(typeId)) &&
      dialog.querySelector(
        '[data-template-display="pamguard.click-display"]') &&
      dialog.querySelectorAll("[data-template-branch]").length === 3,
      "Click monitoring preview omitted an independent branch",
      modules);
    if (!create) {
      cancelOpenDialog();
      await sleep(80);
      const unchanged = await active();
      assert(
        unchanged.workingRevision === before.workingRevision &&
        unchanged.project.controlledUnits.length ===
          before.project.controlledUnits.length,
        "Cancelling Click monitoring preview changed the project",
        unchanged);
      return unchanged;
    }
    confirmDialog(dialog, ["Create configuration"]);
    const after = await waitActive(
      (snapshot) =>
        snapshot.workingRevision > before.workingRevision &&
        snapshot.project.controlledUnits.filter(
          (unit) => [
            "pamguard.acquisition",
            "pamguard.fft",
            "pamguard.user-display",
            "pamguard.click-detector",
            "pamguard.sound-output"
          ].includes(unit.typeId)).length === 5,
      "atomic Click monitoring template mutation",
      10000);
    const units = Object.fromEntries(
      after.project.controlledUnits.map(
        (unit) => [unit.typeId, unit]));
    const sourceId = units["pamguard.acquisition"].id;
    for (const [typeId, role] of [
      ["pamguard.fft", "rawAudio"],
      ["pamguard.click-detector", "rawAudio"],
      ["pamguard.sound-output", "audio"]
    ]) {
      const binding = units[typeId].bindings.find(
        (candidate) => candidate.inputRole === role);
      assert(
        binding?.sources?.length === 1 &&
        binding.sources[0].unitId === sourceId &&
        binding.sources[0].outputRole === "rawAudio",
        `Template ${typeId} branch is not bound to Acquisition`,
        binding);
    }
    const userTab = after.project.displayTabs.find(
      (tab) =>
        tab.owner.unitId === units["pamguard.user-display"].id);
    const clickTab = after.project.displayTabs.find(
      (tab) =>
        tab.owner.unitId === units["pamguard.click-detector"].id);
    assert(
      userTab?.displays?.some(
        (display) =>
          display.providerTypeId ===
            "pamguard.spectrogram-display" &&
          display.source?.unitId === units["pamguard.fft"].id) &&
      clickTab?.displays?.some(
        (display) =>
          display.providerTypeId === "pamguard.click-display"),
      "Template displays are not owned/bound by their controlled units",
      after.project.displayTabs);
    return after;
  }
  async function createBlankProject(name) {
    const before = await active();
    await openMenu("file", "File");
    click(
      queryVisible(["#fileNew", '[data-project-action="new"]'])[0],
      "New project");
    const dialog = await waitFor(
      () => openDialog(),
      "New project dialog");
    const nameInput = dialog.querySelector(
      "input:not([type='checkbox'])");
    assert(nameInput, "New project dialog omitted project name");
    setControlValue(nameInput, name);
    const discard = dialog.querySelector("input[type='checkbox']");
    if (discard) setControlValue(discard, true);
    confirmDialog(dialog, ["Create project"]);
    const after = await waitActive(
      (snapshot) =>
        snapshot.project.projectId !== before.project.projectId &&
        snapshot.workingRevision === 0 &&
        snapshot.project.controlledUnits.length === 0,
      "fresh blank project after template test",
      10000);
    assertBlankShell(after);
    return after;
  }
  function ownedTab(ownerUnitId) {
    return tabs().find((tab) =>
      tab.kind === "display" &&
      tab.owner === ownerUnitId) || null;
  }
  function displayRoot(displayId) {
    return queryVisible([
      `[data-pamguard-display-instance-id="${CSS.escape(displayId)}"]`,
      `[data-project-display-id="${CSS.escape(displayId)}"]`,
      `[data-display-instance-id="${CSS.escape(displayId)}"]`
    ])[0] || null;
  }
  async function addDisplay(
    ownerUnit,
    providerTypeId,
    providerName,
    source) {
    const before = await active();
    const tab = await waitFor(
      () => ownedTab(ownerUnit.id),
      `${ownerUnit.name} owned display tab`);
    tab.element.click();
    await openMenu("display", "Display");
    const item = await waitFor(() => {
      const semantic = queryVisible([
        `[data-project-add-display-provider="${CSS.escape(providerTypeId)}"]`,
        `[data-display-provider-type-id="${CSS.escape(providerTypeId)}"]` +
          '[data-project-action="add-display"]',
        `[data-provider-type-id="${CSS.escape(providerTypeId)}"]` +
          '[role="menuitem"]'
      ])[0];
      return semantic || byText(
        ['[role="menuitem"]', "button", '[role="button"]'],
        [providerName, "Add Spectrogram", "Spectrogram"],
        document,
        { exact: false });
    }, `${providerName} display action`);
    item.click();
    await sleep(100);
    const dialog = openDialog();
    if (dialog) {
      selectSource(
        dialog,
        source.role,
        source.unitId,
        source.unitName,
        false);
      confirmDialog(dialog, ["Add", "Create", "OK"]);
    }
    const after = await waitActive((snapshot) => {
      const displays = snapshot.project.displayTabs.flatMap(
        (candidate) => candidate.displays);
      const oldIds = new Set(before.project.displayTabs.flatMap(
        (candidate) => candidate.displays.map(
          (display) => display.id)));
      return displays.some((display) =>
        display.providerTypeId === providerTypeId &&
        !oldIds.has(display.id));
    }, `${providerName} project mutation`);
    const oldIds = new Set(before.project.displayTabs.flatMap(
      (candidate) => candidate.displays.map(
        (display) => display.id)));
    return after.project.displayTabs.flatMap(
      (candidate) => candidate.displays).find((display) =>
        display.providerTypeId === providerTypeId &&
        !oldIds.has(display.id));
  }
  async function invokeDisplayAction(display, action, labels) {
    const root = await waitFor(
      () => displayRoot(display.id),
      `${display.providerTypeId} rendered display`);
    const direct = queryVisible([
      `[data-project-display-action="${action}"]`,
      `[data-display-action="${action}"]`
    ], root)[0];
    if (direct) {
      direct.click();
      await sleep(50);
      return;
    }
    const rectangle = root.getBoundingClientRect();
    root.dispatchEvent(new MouseEvent("contextmenu", {
      bubbles: true,
      cancelable: true,
      clientX: rectangle.left + Math.min(30, rectangle.width / 2),
      clientY: rectangle.top + Math.min(30, rectangle.height / 2)
    }));
    const item = await waitFor(() =>
      queryVisible([
        `[data-project-display-action="${action}"]`,
        `[data-display-action="${action}"]`,
        '[role="menuitem"]',
        "button"
      ]).find((candidate) => {
        const semantic =
          candidate.getAttribute("data-project-display-action") ===
            action ||
          candidate.getAttribute("data-display-action") === action;
        const text = normalize(candidate.textContent);
        return semantic || labels.map(normalize).some(
          (label) => text === label || text.startsWith(label));
      }), `display ${action} action`);
    item.click();
    await sleep(50);
  }
  async function configureDisplay(display, changes, source) {
    const before = await active();
    await invokeDisplayAction(
      display,
      "configure",
      ["Configure", "Settings"]);
    const dialog = await waitFor(
      () => openDialog(),
      "Spectrogram settings dialog");
    const editor = dialog.querySelector("[data-spectrogram-editor]");
    const tabs = Array.from(
      dialog.querySelectorAll("[data-settings-tab]"));
    assert(
      editor &&
      tabs.map((tab) => normalize(tab.textContent)).join("|") ===
        "data source|scales|plug ins|mark observers" &&
      !editor.querySelector("textarea") &&
      !settingControl(editor, "/sourceName"),
      "Spectrogram did not expose its dedicated PAMGuard-ordered editor",
      {
        tabs: tabs.map((tab) => tab.textContent),
        textareas: editor?.querySelectorAll("textarea").length,
        sourceName: Boolean(settingControl(editor, "/sourceName"))
      });
    selectSource(
      dialog,
      source.role,
      source.unitId,
      source.unitName,
      true);
    for (const change of changes) {
      const hiddenControl = dialog.querySelector(
        `[data-setting-pointer="${CSS.escape(change.pointer)}"]`);
      const panelId = hiddenControl?.closest(
        "[data-settings-panel]")?.getAttribute("data-settings-panel");
      dialog.querySelector(
        `[data-settings-tab="${CSS.escape(panelId || "")}"]`)
        ?.click();
      const control = settingControl(dialog, change.pointer);
      assert(
        control,
        `Spectrogram settings omitted ${change.pointer}`);
      setControlValue(control, change.value);
    }
    if (source.sampleRateHz) {
      const summary = normalize(dialog.querySelector(
        "[data-spectrogram-source-summary]")?.textContent);
      assert(
        summary.includes(
          `${Number(source.sampleRateHz).toLocaleString()} hz`) &&
        (!source.fftLength ||
          summary.includes(`${Number(source.fftLength)}-point fft`)),
        "Spectrogram did not derive rate/FFT metadata from its selected " +
          "source",
        { expected: source, summary });
    }
    if (source.nyquistHz) {
      const highFrequency = Number(
        settingControl(dialog, "/frequencyLimits/1")?.value);
      assert(
        Number.isFinite(highFrequency) &&
        highFrequency <= Number(source.nyquistHz),
        "Spectrogram frequency range exceeded the selected source Nyquist",
        {
          highFrequency,
          nyquistHz: Number(source.nyquistHz)
        });
    }
    confirmDialog(dialog, ["OK", "Save", "Apply"]);
    const after = await waitActive(
      (snapshot) =>
        snapshot.workingRevision > before.workingRevision,
      "Spectrogram immediate settings mutation");
    return after.project.displayTabs.flatMap(
      (tab) => tab.displays).find(
      (candidate) => candidate.id === display.id);
  }
  async function runtimeStatus() {
    const response = await fetch("/module-runtime/status", {
      headers: { Accept: "application/json" },
      cache: "no-store"
    });
    assert(response.ok, "Runtime status read failed", {
      status: response.status
    });
    return response.json();
  }
  async function startRuntime() {
    let button;
    try {
      button = await waitFor(() => {
        const candidate = document.getElementById("runtimeStart");
        return candidate && !candidate.disabled ? candidate : null;
      }, "global Start to become enabled", 10000);
    }
    catch (error) {
      const [project, ready, inventory] = await Promise.all([
        active(),
        fetch("/ready", {
          headers: { Accept: "application/json" },
          cache: "no-store"
        }).then(async (response) => ({
          status: response.status,
          body: await response.json()
        })),
        fetch("/v1/projects/active/acquisitions", {
          headers: { Accept: "application/json" },
          cache: "no-store"
        }).then((response) => response.json())
      ]);
      throw new Error(`${error.message}; UI diagnostics ${
        JSON.stringify({
          projectProjection: project.projection,
          ready,
          inventory,
          projectStatus:
            document.getElementById("projectStatus")?.textContent,
          serviceState:
            document.getElementById("serviceState")?.textContent
        })}`);
    }
    click(button, "global Start");
    try {
      return await waitFor(async () => {
        const status = await runtimeStatus();
        return status.running ? status : null;
      }, "project runtime start", 10000);
    }
    catch (error) {
      throw new Error(`${error.message}; UI diagnostics ${
        JSON.stringify({
          projectStatus:
            document.getElementById("projectStatus")?.textContent,
          serviceState:
            document.getElementById("serviceState")?.textContent,
          toasts: Array.from(document.querySelectorAll(".toast"))
            .map((toast) => toast.textContent)
        })}`);
    }
  }
  async function stopRuntime() {
    click(document.getElementById("runtimeStop"), "global Stop");
    return waitFor(async () => {
      const status = await runtimeStatus();
      return !status.running ? status : null;
    }, "project runtime stop", 10000);
  }
  async function soundRecorderTransportWorkflow(
    unit,
    acquisition) {
    const snapshot = await active();
    const expectedRevision = snapshot.workingRevision;
    const statusPath =
      `/v1/projects/active/sound-recorders/${
        encodeURIComponent(unit.id)}/status`;
    const transportPath =
      `/v1/projects/active/sound-recorders/${
        encodeURIComponent(unit.id)}/transport`;
    const inspection = await assertProjectInspection(
      "Sound Recorder transport workflow");
    const runtimeIds = inspection.projection.runtimeChildren
      .filter((child) => child.ownerUnitId === unit.id)
      .map((child) => child.runtimeNodeId)
      .filter(Boolean);
    assert(
      runtimeIds.length > 0,
      "Sound Recorder omitted its private projected runtime child",
      inspection.projection.runtimeChildren);
    const requestStart = requestHistory().length;

    const openRunningDialog = async () => {
      await invokeUnitAction(
        unit,
        "configure",
        ["Configure", "Settings"]);
      const dialog = await waitFor(
        () => openDialog(),
        `${unit.name} running settings dialog`);
      const off = dialog.querySelector(
        "[data-sound-recorder-action='off']");
      const continuous = dialog.querySelector(
        "[data-sound-recorder-action='continuous']");
      const status = dialog.querySelector(
        "[data-sound-recorder-runtime-status]");
      const portableControls = Array.from(dialog.querySelectorAll(
        "[data-input-role], [data-setting-pointer]"));
      assert(
        portableControls.length > 0 &&
        portableControls.every(
          (control) => control.disabled || control.readOnly) &&
        off && !off.disabled &&
        continuous && !continuous.disabled,
        "Running Sound Recorder dialog did not keep portable settings " +
          "read-only and Off/Continuous enabled",
        {
          controls: portableControls.map((control) => ({
            pointer:
              control.getAttribute("data-setting-pointer") ||
              control.getAttribute("data-input-role"),
            disabled: control.disabled,
            readOnly: control.readOnly
          })),
          offDisabled: off?.disabled,
          continuousDisabled: continuous?.disabled
        });
      return { dialog, off, continuous, status };
    };
    const closeRunningDialog = async (dialog) => {
      confirmDialog(dialog, ["Close"]);
      await waitFor(
        () => !document.getElementById("formDialog").open,
        `${unit.name} running settings close`);
    };

    let view = await openRunningDialog();
    await waitFor(
      () => view.status.getAttribute(
        "data-sound-recorder-runtime-status") === "off"
        ? view.status
        : null,
      `${unit.name} initial stable status`);
    const initialText = String(view.status.textContent || "");
    assert(
      /off/i.test(initialText) &&
      !/[A-Za-z]:[\\/]/.test(initialText) &&
      runtimeIds.every((runtimeId) => !initialText.includes(runtimeId)),
      "Sound Recorder exposed an unsafe initial status",
      { initialText, runtimeIds });

    click(view.continuous, `${unit.name} Continuous`);
    await waitFor(
      () => view.status.getAttribute(
        "data-sound-recorder-runtime-status") === "continuous"
        ? view.status
        : null,
      `${unit.name} Continuous transport`);
    await injectAcquisitionAudio(acquisition.id);
    await closeRunningDialog(view.dialog);

    view = await openRunningDialog();
    await waitFor(
      () => /continuous/i.test(
        view.status.getAttribute(
          "data-sound-recorder-runtime-status") || "") &&
        /\.wav\b/i.test(view.status.textContent || "")
        ? view.status
        : null,
      `${unit.name} safe current filename`);
    const writingText = String(view.status.textContent || "");
    assert(
      /writing [^\\/]+\.wav\b/i.test(writingText) &&
      !/[A-Za-z]:[\\/]/.test(writingText) &&
      !/[\\/](?:tmp|home|var|users)[\\/]/i.test(writingText) &&
      runtimeIds.every((runtimeId) => !writingText.includes(runtimeId)),
      "Sound Recorder status did not reduce its file to a safe basename",
      { writingText, runtimeIds });

    click(view.off, `${unit.name} Off`);
    try {
      await waitFor(
        () => view.status.getAttribute(
          "data-sound-recorder-runtime-status") === "off"
          ? view.status
          : null,
        `${unit.name} Off transport`);
    }
    catch (error) {
      throw new Error(
        `${error.message}; recorder evidence ${
          JSON.stringify({
            status:
              view.status.getAttribute(
                "data-sound-recorder-runtime-status"),
            statusText: view.status.textContent,
            offDisabled: view.off.disabled,
            requests: requestHistory().slice(requestStart)
              .filter((request) =>
                pathOf(request.url).startsWith(
                  "/v1/projects/active/sound-recorders/")),
            toasts: Array.from(
              document.querySelectorAll(".toast"))
              .map((toast) => toast.textContent)
          })}`);
    }
    await closeRunningDialog(view.dialog);

    const workflowRequests = requestHistory().slice(requestStart);
    const recorderRequests = workflowRequests
      .filter((request) =>
        pathOf(request.url).startsWith(
          "/v1/projects/active/sound-recorders/"));
    const statusReads = recorderRequests.filter(
      (request) =>
        request.method === "GET" &&
        pathOf(request.url) === statusPath &&
        request.status === 200);
    const transportWrites = recorderRequests.filter(
      (request) =>
        request.method === "PUT" &&
        pathOf(request.url) === transportPath &&
        request.status === 200);
    const exactTransportBody = (request, transport) =>
      request.body &&
      Object.keys(request.body).sort().join("|") ===
        "expectedWorkingRevision|transport" &&
      request.body.expectedWorkingRevision === expectedRevision &&
      request.body.transport === transport;
    assert(
      statusReads.length >= 2 &&
      workflowRequests.every((request) =>
        runtimeIds.every(
          (runtimeId) => !pathOf(request.url).includes(runtimeId))) &&
      recorderRequests.every((request) => {
        const path = pathOf(request.url);
        return path === statusPath || path === transportPath;
      }) &&
      transportWrites.some((request) =>
        exactTransportBody(request, "continuous")) &&
      transportWrites.some((request) =>
        exactTransportBody(request, "off")),
      "Sound Recorder browser wiring did not use the stable strict " +
        "controlled-unit HTTP contract",
      {
        expectedRevision,
        statusPath,
        transportPath,
        runtimeIds,
        recorderRequests
      });
    return {
      expectedRevision,
      statusReads: statusReads.length,
      transportWrites: transportWrites.length,
      writingText
    };
  }
  async function startSoundOutputListen(unit) {
    await invokeUnitAction(
      unit,
      "configure",
      ["Configure", "Settings"]);
    const dialog = await waitFor(
      () => openDialog(),
      `${unit.name} running Sound Output dialog`);
    const listen = dialog.querySelector(
      "[data-sound-output-action='listen']");
    const status = dialog.querySelector("[data-sound-output-status]");
    assert(
      listen && status,
      "Running Sound Output dialog omitted Listen/status controls");
    const enabledListen = await waitFor(
      () => !listen.disabled ? listen : null,
      `${unit.name} enabled Listen action`);
    click(enabledListen, `${unit.name} Listen`);
    await waitFor(
      () => status.dataset.phase === "live" ? status : null,
      `${unit.name} live browser audio stream`,
      10000);
    return { dialog, status };
  }
  async function stopSoundOutputListen(session) {
    const stop = session.dialog.querySelector(
      "[data-sound-output-action='stop']");
    assert(stop, "Running Sound Output dialog omitted Stop");
    const enabledStop = await waitFor(
      () => !stop.disabled ? stop : null,
      "enabled Sound Output Stop action");
    click(enabledStop, "Sound Output Stop");
    await waitFor(
      () => session.status.dataset.phase === "stopped"
        ? session.status
        : null,
      "stopped browser audio stream");
    confirmDialog(session.dialog, ["Close"]);
    await waitFor(
      () => !document.getElementById("formDialog").open,
      "Sound Output dialog close");
  }
  async function injectAcquisitionAudio(
    acquisitionUnitId,
    withClicks = false) {
    const snapshot = await active();
    const acquisition = snapshot.project.controlledUnits.find(
      (unit) => unit.id === acquisitionUnitId);
    assert(acquisition, "Acquisition unit vanished before PCM injection");
    const inspectionResponse = await fetch(
      "/v1/projects/active/inspection",
      {
        headers: { Accept: "application/json" },
        cache: "no-store"
      });
    const inspectionText = await inspectionResponse.text();
    assert(
      inspectionResponse.ok,
      "Project inspection read failed before FFT stream injection",
      {
        status: inspectionResponse.status,
        body: inspectionText
      });
    const inspection = JSON.parse(inspectionText);
    const child = inspection.projection.runtimeChildren.find(
      (candidate) =>
        candidate.ownerUnitId === acquisitionUnitId &&
        candidate.childRole === "acquisition");
    assert(
      child,
      "Acquisition controlled unit omitted its projected runtime child",
      inspection.projection.runtimeChildren);
    const sampleRate = acquisition.settings.sampleRate;
    const channels = acquisition.settings.nChannels;
    const frames = 16384;
    const pcm = new Float32Array(frames * channels);
    const clickStarts = new Set([
      2048, 4096, 6144, 8192, 10240, 12288, 14336
    ]);
    for (let frame = 0; frame < frames; frame++) {
      const backgroundScale = withClicks ? 0.015 : 1;
      const first =
        Math.sin(2 * Math.PI * 6000 * frame / sampleRate) *
        0.5 * backgroundScale;
      const second =
        Math.sin(2 * Math.PI * 12000 * frame / sampleRate) *
        0.35 * backgroundScale;
      for (let channel = 0; channel < channels; channel++) {
        const delayedFrame = frame - Math.min(channel, 1) * 2;
        const pulse = withClicks && clickStarts.has(delayedFrame)
          ? (channel % 2 === 0 ? 0.98 : 0.92)
          : 0;
        const background = channel % 2 === 0 ? first : second;
        pcm[frame * channels + channel] = background + pulse;
      }
    }
    const response = await fetch(
      `/v1/projects/active/acquisitions/${encodeURIComponent(
        acquisitionUnitId)}/pcm-f32le?expectedProjectId=${encodeURIComponent(
          snapshot.project.projectId)}&expectedWorkingRevision=${
          snapshot.workingRevision}&startSample=0&timeMs=${Date.now()}`,
      {
        method: "POST",
        headers: { "Content-Type": "application/octet-stream" },
        body: pcm.buffer
      });
    assert(
      response.ok,
      "Acquisition PCM injection failed",
      {
        status: response.status,
        body: await response.text()
      });
  }
  async function trackedClickEventWorkflow(clickDetectorUnitId) {
    const inspectionResponse = await fetch(
      "/v1/projects/active/inspection",
      {
        headers: { Accept: "application/json" },
        cache: "no-store"
      });
    assert(
      inspectionResponse.ok,
      "Project inspection read failed before tracked-click workflow",
      { status: inspectionResponse.status });
    const inspection = await inspectionResponse.json();
    const output = inspection.projection.publicOutputs.find(
      (candidate) =>
        candidate.unitId === clickDetectorUnitId &&
        candidate.outputRole === "clicks");
    assert(
      output?.blockId,
      "Click Detector omitted its public retained-click block",
      inspection.projection.publicOutputs);
    const history = await waitFor(async () => {
      const response = await fetch(
        `/data-blocks/${encodeURIComponent(
          output.blockId)}/history?limit=16`,
        {
          headers: { Accept: "application/json" },
          cache: "no-store"
        });
      if (!response.ok) return null;
      const body = await response.json();
      return Array.isArray(body.units) && body.units.length >= 3
        ? body.units
        : null;
    }, "retained clicks for manual event workflow", 10000);
    const locators = history.slice(-3).map((unit) => ({
      uid: unit.uid,
      startSample: unit.startSample,
      channelBitmap: unit.channelBitmap
    }));
    const base =
      `/v1/projects/active/click-detectors/${encodeURIComponent(
        clickDetectorUnitId)}`;
    const request = async (path, options = {}) => {
      const response = await fetch(`${base}${path}`, {
        ...options,
        headers: {
          Accept: "application/json",
          ...(options.body
            ? { "Content-Type": "application/json" }
            : {})
        }
      });
      let body = {};
      try {
        body = await response.json();
      } catch {}
      return { response, body };
    };
    const first = await request("/tracked-events:assign", {
      method: "POST",
      body: JSON.stringify({
        clicks: locators.slice(0, 2),
        eventId: null
      })
    });
    assert(
      first.response.status === 201 &&
      first.body.eventId === 1 &&
      first.body.clickCount === 2 &&
      first.body.comment === "Manual Click Train Detection",
      "New manual Click Train did not match PAMGuard membership semantics",
      first);
    const second = await request("/tracked-events:assign", {
      method: "POST",
      body: JSON.stringify({
        clicks: locators.slice(2),
        eventId: null
      })
    });
    assert(
      second.response.status === 201 &&
      second.body.eventId === 2,
      "Second manual Click Train did not use max ID plus one",
      second);
    const reassigned = await request(
      "/tracked-events/1:reassign",
      {
        method: "POST",
        body: JSON.stringify({ targetEventId: 2 })
      });
    assert(
      reassigned.response.ok &&
      reassigned.body.eventId === 2 &&
      reassigned.body.clickCount === 3,
      "Whole Click Train reassignment did not move all clicks",
      reassigned);
    const localisation = await request(
      "/tracked-events/2:localise",
      { method: "POST", body: "{}" });
    assert(
      localisation.response.status === 409 &&
      [
        "missing_click_bearing",
        "moving_array_origin_unavailable"
      ].includes(localisation.body.code),
      "Tracked event localisation did not report its exact scientific prerequisite",
      localisation);
    const removed = await request(
      `/tracked-clicks/${encodeURIComponent(locators[0].uid)}`,
      { method: "DELETE" });
    assert(
      removed.response.ok && removed.body.removed,
      "Removing a click from its manual train failed",
      removed);
    const listed = await request("/tracked-events");
    assert(
      listed.response.ok &&
      listed.body.events.length === 1 &&
      listed.body.events[0].eventId === 2 &&
      listed.body.events[0].clickCount === 2,
      "Manual tracked-event readback did not preserve reassignment/removal",
      listed);
    const deleted = await request(
      "/tracked-events/2",
      { method: "DELETE" });
    assert(
      deleted.response.ok && deleted.body.deleted,
      "Manual tracked-event cleanup failed",
      deleted);
  }
  async function assertMatchedTemplateAnnotations(
      matchedTemplateUnitId) {
    const inspectionResponse = await fetch(
      "/v1/projects/active/inspection",
      {
        headers: { Accept: "application/json" },
        cache: "no-store"
      });
    assert(
      inspectionResponse.ok,
      "Project inspection read failed before Matched Template evidence",
      { status: inspectionResponse.status });
    const inspection = await inspectionResponse.json();
    const annotatedOutput = inspection.projection.publicOutputs.find(
      (candidate) =>
        candidate.unitId === matchedTemplateUnitId &&
        candidate.outputRole === "annotatedClicks");
    const classificationOutput =
      inspection.projection.publicOutputs.find(
        (candidate) =>
          candidate.unitId === matchedTemplateUnitId &&
          candidate.outputRole === "classifications");
    assert(
      annotatedOutput?.blockId &&
      classificationOutput?.blockId &&
      annotatedOutput.runtimeNodeId ===
        classificationOutput.runtimeNodeId,
      "Matched Template omitted its joined annotated-click and " +
        "classification outputs",
      inspection.projection.publicOutputs);
    const history = async (blockId) => {
      const response = await fetch(
        `/data-blocks/${encodeURIComponent(blockId)}/history?limit=32`,
        {
          headers: { Accept: "application/json" },
          cache: "no-store"
        });
      if (!response.ok) return null;
      return response.json();
    };
    return waitFor(async () => {
      const [annotatedHistory, classificationHistory] =
        await Promise.all([
          history(annotatedOutput.blockId),
          history(classificationOutput.blockId)
        ]);
      const annotatedUnits = annotatedHistory?.units || [];
      const classificationUnits =
        classificationHistory?.units || [];
      for (const annotatedUnit of annotatedUnits) {
        const annotation =
          annotatedUnit.payload?.matchedTemplateAnnotations?.find(
            (candidate) =>
              candidate.classifierInstanceId ===
                annotatedOutput.runtimeNodeId);
        if (!annotation ||
            annotation.clickType !== 101 ||
            !Array.isArray(annotation.bestResults) ||
            annotation.bestResults.length !== 1) {
          continue;
        }
        const classificationUnit = classificationUnits.find(
          (candidate) =>
            candidate.payload?.classifierInstanceId ===
              annotatedOutput.runtimeNodeId &&
            candidate.payload?.clickStartSample ===
              annotatedUnit.payload?.startSample);
        if (!classificationUnit ||
            classificationUnit.payload.clickType !== 101 ||
            classificationUnit.payload.classified !==
              annotation.classified ||
            !Array.isArray(
              classificationUnit.payload.bestResults) ||
            classificationUnit.payload.bestResults.length !== 1) {
          continue;
        }
        const annotationResult = annotation.bestResults[0];
        const classificationResult =
          classificationUnit.payload.bestResults[0];
        if (
          annotationResult.threshold !==
            classificationResult.threshold ||
          annotationResult.matchCorrelation !==
            classificationResult.matchCorrelation ||
          annotationResult.rejectCorrelation !==
            classificationResult.rejectCorrelation) {
          continue;
        }
        return {
          runtimeNodeId: annotatedOutput.runtimeNodeId,
          annotatedBlockId: annotatedOutput.blockId,
          classificationBlockId: classificationOutput.blockId,
          clickStartSample: annotatedUnit.payload.startSample,
          clickType: annotation.clickType,
          classified: annotation.classified,
          bestResult: annotationResult,
          annotatedHistoryCount: annotatedUnits.length,
          classificationHistoryCount: classificationUnits.length
        };
      }
      return null;
    }, "joined Matched Template click annotation", 10000);
  }
  async function assertLiveSpectrogram(displayId) {
    let last = null;
    try {
      return await waitFor(() => {
        const root = displayRoot(displayId);
        const canvas = root?.querySelector(".project-spectrogram-canvas");
        const status = root?.querySelector(
          `[data-display-stream-status="${CSS.escape(displayId)}"]`);
        if (!canvas) {
          last = { canvas: false, status: status?.textContent || "" };
          return null;
        }
        const context = canvas.getContext("2d");
        const pixels = context.getImageData(
          0,
          0,
          canvas.width,
          canvas.height).data;
        const colours = new Set();
        const stride = Math.max(
          4,
          Math.floor(pixels.length / 2048 / 4) * 4);
        for (let index = 0; index < pixels.length; index += stride) {
          colours.add(
            `${pixels[index]},${pixels[index + 1]},${pixels[index + 2]}`);
          if (colours.size > 3) break;
        }
        last = {
          canvas: true,
          status: status?.textContent || "",
          width: canvas.width,
          height: canvas.height,
          colours: colours.size
        };
        return normalize(status?.textContent).includes("frames") &&
          canvas.width >= 200 &&
          canvas.height >= 100 &&
          colours.size > 3
          ? last
          : null;
      }, "live project-owned Spectrogram frames", 10000);
    }
    catch (error) {
      throw new Error(
        `${error.message}; last display state ${JSON.stringify(last)}`);
    }
  }
  async function saveAs(name) {
    const before = await active();
    await openMenu("file", "File");
    const item = await waitFor(() => {
      const semantic = queryVisible([
        '[data-project-action="save-as"]',
        '[role="menuitem"][data-project-command="save-as"]'
      ])[0];
      return semantic || byText(
        ['[role="menuitem"]', "button"],
        ["Save As", "Save As…", "Save As..."]);
    }, "File > Save As");
    item.click();
    const dialog = await waitFor(
      () => openDialog(),
      "Save As dialog");
    const input = textControl(
      dialog,
      ["project-name", "projectName", "name"]);
    assert(input, "Save As dialog omitted project name");
    setControlValue(input, name);
    confirmDialog(dialog, ["Save As", "Save", "OK"]);
    return waitActive(
      (snapshot) =>
        snapshot.project.projectId !==
          before.project.projectId &&
        snapshot.project.metadata.name === name &&
        snapshot.dirty === false,
      "durable Save As");
  }
  async function openSavedProject(projectId) {
    const before = await active();
    await openMenu("file", "File");
    const item = await waitFor(() => {
      const semantic = queryVisible([
        '[data-project-action="open"]',
        '[role="menuitem"][data-project-command="open"]'
      ])[0];
      return semantic || byText(
        ['[role="menuitem"]', "button"],
        ["Open", "Openâ€¦", "Open..."]);
    }, "File > Open");
    item.click();
    const dialog = await waitFor(
      () => openDialog(),
      "Open project dialog");
    const select = dialog.querySelector("select");
    assert(select, "Open project dialog omitted the saved-project list");
    const option = Array.from(select.options).find(
      (candidate) => candidate.value === projectId);
    assert(option, "Saved project was absent from File > Open", {
      projectId,
      options: Array.from(select.options).map((candidate) => ({
        value: candidate.value,
        text: candidate.textContent
      }))
    });
    select.value = option.value;
    select.dispatchEvent(new Event("change", { bubbles: true }));
    const discard = dialog.querySelector("input[type='checkbox']");
    if (discard) {
      discard.checked = true;
      discard.dispatchEvent(new Event("change", { bubbles: true }));
    }
    confirmDialog(dialog, ["Open project", "Open", "OK"]);
    return waitActive(
      (snapshot) =>
        snapshot.project.projectId === projectId &&
        snapshot.project.projectId !== before.project.projectId &&
        snapshot.dirty === false,
      "durable project open");
  }
  async function save() {
    const before = await active();
    await openMenu("file", "File");
    const item = await waitFor(() => {
      const semantic = queryVisible([
        '[data-project-action="save"]',
        '[role="menuitem"][data-project-command="save"]'
      ])[0];
      return semantic || byText(
        ['[role="menuitem"]', "button"],
        ["Save"]);
    }, "File > Save");
    item.click();
    return waitActive(
      (snapshot) =>
        snapshot.authorityRevision >
          before.authorityRevision &&
        snapshot.dirty === false,
      "durable Save");
  }
  async function removeUnit(unit, leaveDependants = false) {
    const before = await active();
    await invokeUnitAction(unit, "remove", ["Remove", "Delete"]);
    await sleep(50);
    const dialog = openDialog();
    if (dialog) {
      if (leaveDependants) {
        const leave = dialog.querySelector(
          "input[type='checkbox']");
        assert(
          leave,
          `${unit.name} removal omitted leave-unbound policy`);
        leave.checked = true;
        leave.dispatchEvent(new Event("change", { bubbles: true }));
      }
      confirmDialog(dialog, ["Remove", "Delete", "OK"]);
    }
    return waitActive(
      (snapshot) =>
        snapshot.workingRevision > before.workingRevision &&
        !snapshot.project.controlledUnits.some(
          (candidate) => candidate.id === unit.id),
      `${unit.name} removal`);
  }
  async function renameUnit(unit, name) {
    const before = await active();
    await invokeUnitAction(unit, "rename", ["Rename"]);
    const dialog = await waitFor(
      () => openDialog(),
      `${unit.name} rename dialog`);
    const input = textControl(
      dialog,
      ["unit-name", "unitName", "name"]);
    assert(input, "Rename dialog omitted unit name");
    setControlValue(input, name);
    confirmDialog(dialog, ["Rename", "OK", "Save"]);
    const snapshot = await waitActive(
      (candidate) =>
        candidate.workingRevision > before.workingRevision &&
        candidate.project.controlledUnits.some(
          (entry) => entry.id === unit.id && entry.name === name),
      `${unit.name} rename`);
    return snapshot.project.controlledUnits.find(
      (entry) => entry.id === unit.id);
  }
  async function renameUnitExpectConflict(
    unit,
    attemptedName,
    staleEtag) {
    const beforeRequests = requestHistory().length;
    await invokeUnitAction(unit, "rename", ["Rename"]);
    const dialog = await waitFor(
      () => openDialog(),
      `${unit.name} rename dialog`);
    const input = textControl(
      dialog,
      ["unit-name", "unitName", "name"]);
    assert(input, "Rename dialog omitted unit name");
    setControlValue(input, attemptedName);
    globalThis.__pamguardForceNextProjectIfMatch = staleEtag;
    confirmDialog(dialog, ["Rename", "OK", "Save"]);
    const conflictRequest = await waitFor(() =>
      requestHistory().slice(beforeRequests).find((request) =>
        request.method === "POST" &&
        pathOf(request.url) ===
          "/v1/projects/active/mutations" &&
        request.status === 412 &&
        request.forcedIfMatch === staleEtag),
      "stale browser HTTP 412");
    const conflictSurface = await waitFor(() =>
      queryVisible([
        "[data-project-conflict]",
        '[data-project-status="conflict"]',
        '[role="alert"]',
        ".toast",
        ".notification",
        "dialog[open]"
      ]).find((element) => {
        const text = normalize(element.textContent);
        return text.includes("conflict") ||
          text.includes("changed") ||
          text.includes("stale") ||
          text.includes("reload");
      }), "visible stale-project conflict");
    const snapshot = await active();
    const current = snapshot.project.controlledUnits.find(
      (candidate) => candidate.id === unit.id);
    assert(
      current && current.name !== attemptedName,
      "Stale browser edit was silently retried/applied",
      current);
    cancelOpenDialog();
    return {
      request: conflictRequest,
      message: conflictSurface.textContent.replace(/\s+/g, " ").trim(),
      currentName: current.name
    };
  }

  Object.defineProperty(
    globalThis,
    "__pamguardProjectShellSmoke",
    {
      configurable: false,
      enumerable: false,
      writable: false,
      value: Object.freeze({
        active,
        acquisitionInventory,
        addDisplay,
        addUnit,
        assert,
        assertDialogSource,
        assertDisplayMenuContributions,
        assertGraphIncompatible,
        assertProjectInspection,
        assertBlankShell,
        assertMatchedTemplateAnnotations,
        assertNoParallelWorkflow,
        clickMonitoringTemplate,
        configureAcquisitionHost,
        configureArrayManager,
        configureClickClassifierTypes,
        configureDisplay,
        configureUnit,
        createBlankProject,
        displayRoot,
        injectAcquisitionAudio,
        ownedTab,
        reconnectGraph,
        removeUnit,
        renameUnit,
        renameUnitExpectConflict,
        requestHistory,
        assertLiveSpectrogram,
        openSavedProject,
        save,
        saveAs,
        soundRecorderTransportWorkflow,
        startSoundOutputListen,
        startRuntime,
        stopSoundOutputListen,
        stopRuntime,
        suggestedUnitName,
        tabs,
        trackedClickEventWorkflow,
        unitNode,
        waitActive,
        waitFor
      })
    });
})()
'@

$phaseAExpression = @'
(async () => {
  const h = globalThis.__pamguardProjectShellSmoke;
  h.assert(h, "Project-shell smoke bootstrap was not installed");
  const blank = await h.waitActive(
    (snapshot) =>
      snapshot.schemaVersion === 1 &&
      snapshot.workingRevision === 0,
    "blank active project");
  h.assertBlankShell(blank);
  await h.configureArrayManager("Cancelled Browser Array", true);
  const arrayManager = await h.configureArrayManager(
    "Browser Test Array");
  h.assert(
    arrayManager.settings.arrayName === "Browser Test Array" &&
    arrayManager.settings.streamers.length === 1 &&
    arrayManager.settings.hydrophones.length === 2,
    "Array Manager did not preserve its canonical geometry",
    arrayManager);
  await h.clickMonitoringTemplate(false);
  const templateProject = await h.clickMonitoringTemplate(true);
  h.assert(
    templateProject.project.controlledUnits.length === 5,
    "Click monitoring template was not one atomic five-unit project edit",
    templateProject.project.controlledUnits);
  const templateClick = templateProject.project.controlledUnits.find(
    (unit) => unit.typeId === "pamguard.click-detector");
  const configuredTemplateClick = await h.configureUnit(
    templateClick,
    [
      { pointer: "/detector/thresholdDb", value: 12.5 },
      { pointer: "/detector/echo/runOnline", value: true },
      { pointer: "/localisation/delayMeasurement/upSample", value: 2 },
      { pointer: "/train/enabled", value: true },
      { pointer: "/display/timeWindowSeconds", value: 45 }
    ]);
  h.assert(
    configuredTemplateClick.settings.detector.thresholdDb === 12.5 &&
    configuredTemplateClick.settings.detector.echo.runOnline === true &&
    configuredTemplateClick.settings.localisation
      .delayMeasurement.upSample === 2 &&
    configuredTemplateClick.settings.train.enabled === true &&
    configuredTemplateClick.settings.display.timeWindowSeconds === 45,
    "Dedicated Click settings did not round-trip through project authority",
    configuredTemplateClick.settings);
  await h.configureClickClassifierTypes(templateClick);
  const templateAcquisition =
    templateProject.project.controlledUnits.find(
      (unit) => unit.typeId === "pamguard.acquisition");
  const templateSoundOutput =
    templateProject.project.controlledUnits.find(
      (unit) => unit.typeId === "pamguard.sound-output");
  const templateFft =
    templateProject.project.controlledUnits.find(
      (unit) => unit.typeId === "pamguard.fft");
  const configuredTemplateSoundOutput = await h.configureUnit(
    templateSoundOutput,
    [
      { pointer: "/channelBitmap/0", value: true },
      { pointer: "/channelBitmap/1", value: true }
    ]);
  h.assert(
    configuredTemplateSoundOutput.settings.channelBitmap === 3,
    "Template Sound Output did not retain the explicit playback channels",
    configuredTemplateSoundOutput.settings);
  const templateUserDisplay =
    templateProject.project.controlledUnits.find(
      (unit) => unit.typeId === "pamguard.user-display");
  const templateUserTab = templateProject.project.displayTabs.find(
    (tab) => tab.owner.unitId === templateUserDisplay.id);
  const templateSpectrogram = templateUserTab.displays.find(
    (display) =>
      display.providerTypeId === "pamguard.spectrogram-display");
  const templateClickTab = templateProject.project.displayTabs.find(
    (tab) => tab.owner.unitId === templateClick.id);
  const templateClickDisplay = templateClickTab.displays.find(
    (display) =>
      display.providerTypeId === "pamguard.click-display");
  await h.assertDisplayMenuContributions([
    {
      ownerUnitId: templateClick.id,
      providerTypeId: "pamguard.click-display",
      disabled: true
    },
    {
      ownerUnitId: templateUserDisplay.id,
      providerTypeId: "pamguard.spectrogram-display",
      disabled: false
    }
  ]);
  h.assert(
    templateProject.project.controlledUnits.length === 5 &&
    templateProject.project.controlledUnits.every(
      (unit) =>
        unit.typeId !==
          "pamguard.matched-template-classifier"),
    "Matched Template must not be hidden inside the five-unit Click " +
      "monitoring template",
    templateProject.project.controlledUnits);
  const templateMatchedTemplate = await h.addUnit(
    "pamguard.matched-template-classifier",
    "Matched Template Click Classifer",
    "Browser Matched Template");
  const defaultClassifier =
    templateMatchedTemplate.settings.classifiers?.[0];
  const defaultMatchWaveform =
    defaultClassifier?.matchTemplate?.waveform;
  const defaultRejectWaveform =
    defaultClassifier?.rejectTemplate?.waveform;
  const presetLibraryResponse = await fetch(
    "/assets/matched-template-default-templates.json",
    {
      headers: { Accept: "application/json" },
      cache: "no-store"
    });
  h.assert(
    presetLibraryResponse.ok,
    "Could not read the PAMGuard Matched Template preset library",
    { status: presetLibraryResponse.status });
  const presetLibrary = await presetLibraryResponse.json();
  const beakedPreset = presetLibrary.templates?.find(
    (template) => template.name === "Beaked Whale Click");
  const dolphinPreset = presetLibrary.templates?.find(
    (template) => template.name === "Dolphin Click");
  h.assert(
    templateAcquisition.settings.sampleRate === 48000 &&
    templateMatchedTemplate.settings.clickType === 101 &&
    templateMatchedTemplate.settings.normalisationType === 1 &&
    templateMatchedTemplate.settings.peakSearch === true &&
    templateMatchedTemplate.settings.peakSmoothing === 5 &&
    templateMatchedTemplate.settings.lengthDb === 6 &&
    templateMatchedTemplate.settings.restrictedBins === 2048 &&
    templateMatchedTemplate.settings.channelClassification === 0 &&
    templateMatchedTemplate.settings.classifiers.length === 1 &&
    defaultClassifier.thresholdToAccept === 0.01 &&
    defaultClassifier.normalisation === 0 &&
    defaultClassifier.matchTemplate.name === "Beaked Whale" &&
    defaultClassifier.matchTemplate.sampleRateHz === 192000 &&
    defaultMatchWaveform.length === 192 &&
    beakedPreset?.sampleRateHz === 192000 &&
    JSON.stringify(defaultMatchWaveform) ===
      JSON.stringify(beakedPreset.waveform) &&
    defaultClassifier.rejectTemplate.name === "Dolphin" &&
    defaultClassifier.rejectTemplate.sampleRateHz === 192000 &&
    defaultRejectWaveform.length === 192 &&
    dolphinPreset?.sampleRateHz === 192000 &&
    JSON.stringify(defaultRejectWaveform) ===
      JSON.stringify(dolphinPreset.waveform),
    "Fresh Matched Template did not contain PAMGuard's exact 192 kHz " +
      "Beaked Whale/Dolphin defaults against the 48 kHz Acquisition",
    {
      acquisitionSampleRate:
        templateAcquisition.settings.sampleRate,
      settings: templateMatchedTemplate.settings
    });
  const configuredTemplateMatchedTemplate = await h.configureUnit(
    templateMatchedTemplate,
    [{
      pointer: "/classifiers/0/thresholdToAccept",
      value: 0.02
    }],
    {
      role: "clicks",
      unitId: templateClick.id,
      unitName: templateClick.name
    });
  const matchedTemplateBinding =
    configuredTemplateMatchedTemplate.bindings.find(
      (binding) => binding.inputRole === "clicks");
  const configuredClassifier =
    configuredTemplateMatchedTemplate.settings.classifiers[0];
  h.assert(
    matchedTemplateBinding?.sources?.length === 1 &&
    matchedTemplateBinding.sources[0].unitId === templateClick.id &&
    matchedTemplateBinding.sources[0].outputRole === "clicks" &&
    configuredTemplateMatchedTemplate.settings.normalisationType === 1 &&
    configuredClassifier.normalisation === 1 &&
    configuredClassifier.thresholdToAccept === 0.02 &&
    configuredClassifier.matchTemplate.name === "Beaked Whale" &&
    configuredClassifier.matchTemplate.sampleRateHz === 192000 &&
    JSON.stringify(configuredClassifier.matchTemplate.waveform) ===
      JSON.stringify(defaultMatchWaveform) &&
    configuredClassifier.rejectTemplate.name === "Dolphin" &&
    configuredClassifier.rejectTemplate.sampleRateHz === 192000 &&
    JSON.stringify(configuredClassifier.rejectTemplate.waveform) ===
      JSON.stringify(defaultRejectWaveform),
    "Matched Template editor changed the default waveforms or failed to " +
      "bind clicks to the template Click Detector",
    configuredTemplateMatchedTemplate);
  const templateMht = await h.addUnit(
    "pamguard.mht-click-train",
    "Click Train Detector",
    "Browser MHT Click Trains");
  const configuredTemplateMht = await h.configureUnit(
    templateMht,
    [
      { pointer: "/channelGroups/1", value: false },
      { pointer: "/channelGroups/3", value: true },
      { pointer: "/kernel/maxCoast", value: 4 },
      {
        pointer: "/classifier/preClassifier/chi2Threshold",
        value: 1400
      }
    ],
    [
      {
        role: "clicks",
        unitId: templateClick.id,
        unitName: templateClick.name
      },
      {
        role: "features",
        unitId: templateClick.id,
        unitName: templateClick.name
      },
      {
        role: "localisations",
        unitId: templateClick.id,
        unitName: templateClick.name
      },
      {
        role: "bearings",
        unitId: templateClick.id,
        unitName: templateClick.name
      }
    ]);
  h.assert(
    configuredTemplateMht.settings.channelGroups.join(",") === "3" &&
    configuredTemplateMht.settings.kernel.maxCoast === 4 &&
    configuredTemplateMht.settings.classifier.preClassifier
      .chi2Threshold === 1400 &&
    ["clicks", "features", "localisations", "bearings"].every(
      (role) =>
        configuredTemplateMht.bindings.find(
          (binding) => binding.inputRole === role)
          ?.sources?.[0]?.unitId === templateClick.id),
    "MHT editor did not preserve source group selection, settings, or " +
      "the four scientifically required Click Detector inputs",
    configuredTemplateMht);
  await h.assertProjectInspection("MHT Click Train configuration");

  const templateIshmaelEnergy = await h.addUnit(
    "pamguard.ishmael-energy-sum",
    "Ishmael energy sum",
    "Browser Ishmael Energy");
  const configuredTemplateIshmaelEnergy = await h.configureUnit(
    templateIshmaelEnergy,
    [
      { pointer: "/channelBitmap/0", value: true },
      { pointer: "/f0Hz", value: 500 },
      { pointer: "/f1Hz", value: 6000 },
      { pointer: "/threshold", value: 1.5 }
    ],
    {
      role: "fft",
      unitId: templateFft.id,
      unitName: templateFft.name
    });
  h.assert(
    configuredTemplateIshmaelEnergy.settings.channelBitmap === 1 &&
    configuredTemplateIshmaelEnergy.settings.f0Hz === 500 &&
    configuredTemplateIshmaelEnergy.settings.f1Hz === 6000 &&
    configuredTemplateIshmaelEnergy.settings.threshold === 1.5,
    "Ishmael Energy Sum settings did not round-trip",
    configuredTemplateIshmaelEnergy);
  await h.assertProjectInspection("Ishmael Energy Sum configuration");

  const templateIshmaelSgram = await h.addUnit(
    "pamguard.ishmael-sgram-corr",
    "Ishmael spectrogram correlation",
    "Browser Ishmael Correlation");
  const configuredTemplateIshmaelSgram = await h.configureUnit(
    templateIshmaelSgram,
    [
      { pointer: "/channelBitmap/0", value: true },
      { pointer: "/segments/0/0", value: 0 },
      { pointer: "/segments/0/1", value: 1000 },
      { pointer: "/segments/0/2", value: 0.08 },
      { pointer: "/segments/0/3", value: 4000 },
      { pointer: "/spreadHz", value: 120 }
    ],
    {
      role: "fft",
      unitId: templateFft.id,
      unitName: templateFft.name
    });
  h.assert(
    configuredTemplateIshmaelSgram.settings.channelBitmap === 1 &&
    JSON.stringify(
      configuredTemplateIshmaelSgram.settings.segments) ===
        JSON.stringify([[0, 1000, 0.08, 4000]]) &&
    configuredTemplateIshmaelSgram.settings.spreadHz === 120,
    "Ishmael Spectrogram Correlation settings did not round-trip",
    configuredTemplateIshmaelSgram);
  await h.assertProjectInspection(
    "Ishmael Spectrogram Correlation configuration");

  const templateIshmaelMatch = await h.addUnit(
    "pamguard.ishmael-match-filter",
    "Ishmael matched filtering",
    "Browser Ishmael Matched Filter");
  const configuredTemplateIshmaelMatch = await h.configureUnit(
    templateIshmaelMatch,
    [
      { pointer: "/channelBitmap/0", value: true },
      { pointer: "/threshold", value: 1.25 }
    ],
    {
      role: "rawAudio",
      unitId: templateAcquisition.id,
      unitName: templateAcquisition.name
    });
  h.assert(
    configuredTemplateIshmaelMatch.settings.channelBitmap === 1 &&
    configuredTemplateIshmaelMatch.settings.threshold === 1.25 &&
    configuredTemplateIshmaelMatch.settings.kernelFilenameList[0] ===
      "browser-ishmael-kernel.wav" &&
    configuredTemplateIshmaelMatch.settings.kernelSamples.length === 16,
    "Ishmael Matched Filter WAV import and settings did not round-trip",
    configuredTemplateIshmaelMatch);
  await h.assertProjectInspection(
    "Ishmael Matched Filter configuration");

  const templateNoiseMonitor = await h.addUnit(
    "pamguard.fft-noise-monitor",
    "Noise Monitor",
    "Browser Noise Monitor");
  const configuredTemplateNoiseMonitor = await h.configureUnit(
    templateNoiseMonitor,
    [
      { pointer: "/channelBitmap/0", value: true },
      { pointer: "/useAll", value: false },
      { pointer: "/measurementIntervalSeconds", value: 10 },
      { pointer: "/nMeasures", value: 12 }
    ],
    {
      role: "fft",
      unitId: templateFft.id,
      unitName: templateFft.name
    });
  h.assert(
    configuredTemplateNoiseMonitor.settings.channelBitmap === 1 &&
    configuredTemplateNoiseMonitor.settings.useAll === false &&
    configuredTemplateNoiseMonitor.settings
      .measurementIntervalSeconds === 10 &&
    configuredTemplateNoiseMonitor.settings.nMeasures === 12 &&
    configuredTemplateNoiseMonitor.settings.bands.length === 17,
    "Noise Monitor dedicated settings did not round-trip",
    configuredTemplateNoiseMonitor);

  const templateNoiseBand = await h.addUnit(
    "pamguard.noise-band-monitor",
    "Noise Band Monitor",
    "Browser Noise Bands");
  const configuredTemplateNoiseBand = await h.configureUnit(
    templateNoiseBand,
    [
      { pointer: "/channelBitmap/0", value: true },
      { pointer: "/outputIntervalSeconds", value: 5 },
      { pointer: "/maximumFrequencyHz", value: 12000 }
    ],
    {
      role: "rawAudio",
      unitId: templateAcquisition.id,
      unitName: templateAcquisition.name
    });
  h.assert(
    configuredTemplateNoiseBand.settings.channelBitmap === 1 &&
    configuredTemplateNoiseBand.settings.outputIntervalSeconds === 5 &&
    configuredTemplateNoiseBand.settings.maximumFrequencyHz === 12000,
    "Noise Band Monitor dedicated settings did not round-trip",
    configuredTemplateNoiseBand);

  const templateLtsa = await h.addUnit(
    "pamguard.ltsa",
    "Long Term Spectral Average",
    "Browser LTSA");
  const configuredTemplateLtsa = await h.configureUnit(
    templateLtsa,
    [
      { pointer: "/channelBitmap/0", value: true },
      { pointer: "/intervalSeconds", value: 30 },
      { pointer: "/longerFactor", value: 6 }
    ],
    {
      role: "fft",
      unitId: templateFft.id,
      unitName: templateFft.name
    });
  h.assert(
    configuredTemplateLtsa.settings.channelBitmap === 1 &&
    configuredTemplateLtsa.settings.intervalSeconds === 30 &&
    configuredTemplateLtsa.settings.longerFactor === 6,
    "LTSA dedicated settings did not round-trip",
    configuredTemplateLtsa);

  const templateWhistles = await h.addUnit(
    "pamguard.whistles-moans",
    "Whistle and Moan Detector",
    "Browser Whistles");
  const configuredTemplateWhistles = await h.configureUnit(
    templateWhistles,
    [
      { pointer: "/channelBitmap/0", value: true },
      { pointer: "/minFrequencyHz", value: 2000 },
      { pointer: "/maxFrequencyHz", value: 18000 },
      {
        pointer: "/noiseReduction/medianFilter",
        value: true
      },
      {
        pointer: "/noiseReduction/averageSubtraction",
        value: true
      },
      {
        pointer: "/noiseReduction/threshold",
        value: true
      },
      {
        pointer: "/noiseReduction/thresholdDb",
        value: 9
      }
    ],
    {
      role: "fft",
      unitId: templateFft.id,
      unitName: templateFft.name
    });
  h.assert(
    configuredTemplateWhistles.settings.channelBitmap === 1 &&
    configuredTemplateWhistles.settings.minFrequencyHz === 2000 &&
    configuredTemplateWhistles.settings.maxFrequencyHz === 18000 &&
    configuredTemplateWhistles.settings.noiseReduction.medianFilter &&
    configuredTemplateWhistles.settings.noiseReduction
      .averageSubtraction &&
    configuredTemplateWhistles.settings.noiseReduction.threshold &&
    configuredTemplateWhistles.settings.noiseReduction.thresholdDb === 9,
    "Whistle and Moan dedicated settings did not round-trip",
    configuredTemplateWhistles);
  await h.assertProjectInspection(
    "Noise, LTSA, and Whistle configuration");
  await h.configureAcquisitionHost(
    templateAcquisition,
    "https://cancelled.invalid/template-audio",
    true);
  const configuredHost = await h.configureAcquisitionHost(
    templateAcquisition,
    "https://fixture.invalid/template-audio");
  h.assert(
    configuredHost.configurationStatus === "configured" &&
    configuredHost.hostBindingRevision !== null,
    "Template Acquisition did not become host-configured",
    configuredHost);
  await h.startRuntime();
  h.ownedTab(templateUserDisplay.id).element.click();
  await h.waitFor(() => {
    const status = h.displayRoot(templateSpectrogram.id)?.querySelector(
      `[data-display-stream-status="${CSS.escape(
        templateSpectrogram.id)}"]`);
    return String(status?.textContent || "").trim().toLowerCase() ===
      "live" ? status : null;
  }, "template Spectrogram stream subscription");
  h.ownedTab(templateClick.id).element.click();
  await h.waitFor(() => {
    const status = h.displayRoot(templateClickDisplay.id)?.querySelector(
      `[data-display-stream-status="${CSS.escape(
        templateClickDisplay.id)}"]`);
    return /^live\b/i.test(String(status?.textContent || "").trim())
      ? status
      : null;
  }, "template Click display stream subscription");
  const trackedControls = h.displayRoot(
    templateClickDisplay.id)?.querySelector(
      `[data-tracked-click-controls="${CSS.escape(
        templateClick.id)}"]`);
  h.assert(
    trackedControls &&
    trackedControls.querySelector(
      `[data-tracked-click-action="new-event"]`) &&
    trackedControls.querySelector(
      `[data-tracked-click-action="assign"]`) &&
    trackedControls.querySelector(
      `[data-tracked-click-action="remove"]`) &&
    trackedControls.querySelector(
      `[data-tracked-click-action="reassign"]`) &&
    trackedControls.querySelector(
      `[data-tracked-click-action="localise"]`),
    "Template Click display omitted PAMGuard manual tracked-event controls",
    trackedControls?.outerHTML);
  const listening = await h.startSoundOutputListen(
    templateSoundOutput);
  await h.injectAcquisitionAudio(templateAcquisition.id, true);
  await h.waitFor(
    () => /\b[1-9]\d* received frames\b/i.test(
      listening.status.textContent || "")
      ? listening.status
      : null,
    "template Sound Output received audio frames",
    10000);
  await h.stopSoundOutputListen(listening);
  h.ownedTab(templateUserDisplay.id).element.click();
  await h.assertLiveSpectrogram(templateSpectrogram.id);
  h.ownedTab(templateClick.id).element.click();
  const liveClickStatus = await h.waitFor(() => {
    const status = h.displayRoot(templateClickDisplay.id)?.querySelector(
      `[data-display-stream-status="${CSS.escape(
        templateClickDisplay.id)}"]`);
    const match = String(status?.textContent || "").match(
      /\b([1-9]\d*) retained\b/i);
    return match ? status : null;
  }, "continuous template Click display detections", 10000);
  h.assert(
    /retained/i.test(liveClickStatus.textContent),
    "Template Click display did not retain continuous detections",
    liveClickStatus.textContent);
  const matchedTemplateEvidence =
    await h.assertMatchedTemplateAnnotations(
      templateMatchedTemplate.id);
  h.assert(
    matchedTemplateEvidence.clickType === 101 &&
    matchedTemplateEvidence.annotatedHistoryCount > 0 &&
    matchedTemplateEvidence.classificationHistoryCount > 0,
    "Matched Template did not publish joined annotation and " +
      "classification evidence",
    matchedTemplateEvidence);
  await h.trackedClickEventWorkflow(templateClick.id);
  await h.stopRuntime();
  const savedTemplate = await h.saveAs(
    "Browser Click Monitoring Template");
  h.assert(
    savedTemplate.project.controlledUnits.some(
      (unit) => unit.id === templateAcquisition.id) &&
    savedTemplate.project.controlledUnits.some(
      (unit) => unit.id === templateSoundOutput.id) &&
    savedTemplate.project.controlledUnits.some(
      (unit) => unit.id === templateClick.id) &&
    savedTemplate.project.controlledUnits.some(
      (unit) => unit.id === templateMatchedTemplate.id) &&
    savedTemplate.project.controlledUnits.some(
      (unit) => unit.id === templateMht.id) &&
    savedTemplate.project.controlledUnits.some(
      (unit) => unit.id === templateIshmaelEnergy.id) &&
    savedTemplate.project.controlledUnits.some(
      (unit) => unit.id === templateIshmaelSgram.id) &&
    savedTemplate.project.controlledUnits.some(
      (unit) => unit.id === templateIshmaelMatch.id) &&
    savedTemplate.project.controlledUnits.some(
      (unit) => unit.id === templateNoiseMonitor.id) &&
    savedTemplate.project.controlledUnits.some(
      (unit) => unit.id === templateNoiseBand.id) &&
    savedTemplate.project.controlledUnits.some(
      (unit) => unit.id === templateLtsa.id) &&
    savedTemplate.project.controlledUnits.some(
      (unit) => unit.id === templateWhistles.id) &&
    savedTemplate.project.displayTabs.some(
      (tab) => tab.id === templateUserTab.id &&
        tab.displays.some(
          (display) => display.id === templateSpectrogram.id)) &&
    savedTemplate.project.displayTabs.some(
      (tab) => tab.id === templateClickTab.id &&
        tab.displays.some(
          (display) => display.id === templateClickDisplay.id)),
    "Saving the template branch changed its controlled-unit/display IDs",
    savedTemplate.project);
  await h.createBlankProject("Browser Manual Workflow");

  const acquisition = await h.addUnit(
    "pamguard.acquisition",
    "Sound Acquisition",
    "Browser Acquisition");
  const configuredAcquisition = await h.configureUnit(
    acquisition,
    [
      { pointer: "/sampleRate", value: 96000 },
      { pointer: "/hardwareChannelList/0", value: 3 },
      { pointer: "/hardwareChannelList/1", value: 7 },
      { pointer: "/hydrophoneList/0", value: 1 },
      { pointer: "/hydrophoneList/1", value: 0 },
      { pointer: "/voltsPeak2Peak", value: 2.5 },
      { pointer: "/preamplifier/gainDb", value: 24 },
      { pointer: "/preamplifier/bandwidthHz/0", value: 100 },
      { pointer: "/preamplifier/bandwidthHz/1", value: 40000 },
      { pointer: "/dcTimeConstantSeconds", value: 2.5 }
    ]);
  h.assert(
    configuredAcquisition.settings.sampleRate === 96000 &&
    configuredAcquisition.settings.hardwareChannelList.join(",") === "3,7" &&
    configuredAcquisition.settings.hydrophoneList.join(",") === "1,0" &&
    configuredAcquisition.settings.voltsPeak2Peak === 2.5 &&
    configuredAcquisition.settings.preamplifier.gainDb === 24 &&
    configuredAcquisition.settings.preamplifier.bandwidthHz.join(",") ===
      "100,40000" &&
    configuredAcquisition.settings.subtractDC === true &&
    configuredAcquisition.settings.dcTimeConstantSeconds === 2.5 &&
    !Object.hasOwn(configuredAcquisition.settings, "sourceId"),
    "Acquisition portable settings did not apply immediately",
    configuredAcquisition.settings);

  const soundRecorder = await h.addUnit(
    "pamguard.sound-recorder",
    "Sound Recorder",
    "Browser Sound Recorder");
  const configuredSoundRecorder = await h.configureUnit(
    soundRecorder,
    [
      { pointer: "/fileInitials", value: "WEB" }
    ],
    {
      role: "rawAudio",
      unitId: acquisition.id,
      unitName: acquisition.name
    });
  const recorderBinding = configuredSoundRecorder.bindings.find(
    (binding) => binding.inputRole === "rawAudio");
  h.assert(
    recorderBinding?.sources?.length === 1 &&
    recorderBinding.sources[0].unitId === acquisition.id &&
    recorderBinding.sources[0].outputRole === "rawAudio" &&
    configuredSoundRecorder.settings.fileInitials === "WEB" &&
    !Object.hasOwn(configuredSoundRecorder.settings, "outputFolder") &&
    !Object.hasOwn(configuredSoundRecorder.settings, "directory"),
    "Sound Recorder did not preserve its portable source/settings boundary",
    configuredSoundRecorder);

  const soundOutput = await h.addUnit(
    "pamguard.sound-output",
    "Sound Output",
    "Browser Sound Output");
  const configuredSoundOutput = await h.configureUnit(
    soundOutput,
    [
      { pointer: "/channelBitmap/0", value: true },
      { pointer: "/channelBitmap/1", value: true },
      { pointer: "/playbackSpeed", value: 1 },
      { pointer: "/playbackGainDb", value: 3 },
      { pointer: "/hpFilter", value: 0.01 }
    ],
    {
      role: "audio",
      unitId: acquisition.id,
      unitName: acquisition.name
    });
  const soundBinding = configuredSoundOutput.bindings.find(
    (binding) => binding.inputRole === "audio");
  h.assert(
    configuredSoundOutput.settings.channelBitmap === 3 &&
    configuredSoundOutput.settings.defaultSampleRate === true &&
    configuredSoundOutput.settings.playbackRateHz === 48000 &&
    configuredSoundOutput.settings.playbackSpeed === 1 &&
    configuredSoundOutput.settings.playbackGainDb === 3 &&
    configuredSoundOutput.settings.hpFilter === 0.01 &&
    soundBinding?.sources?.length === 1 &&
    soundBinding.sources[0].unitId === acquisition.id &&
    soundBinding.sources[0].outputRole === "rawAudio" &&
    !Object.hasOwn(configuredSoundOutput.settings, "deviceId") &&
    !Object.hasOwn(configuredSoundOutput.settings, "deviceNumber") &&
    !Object.hasOwn(configuredSoundOutput.settings, "deviceType"),
    "Sound Output did not preserve portable settings/binding/device " +
      "separation",
    configuredSoundOutput);
  h.assert(
    h.requestHistory().some((request) =>
      request.method === "GET" &&
      new URL(request.url, location.href).pathname === "/data-blocks"),
    "Sound Output editor did not consult runtime data-block metadata",
    h.requestHistory());

  const levelMeter = await h.addUnit(
    "pamguard.level-meter",
    "Level Meter",
    "Browser Level Meter");
  const configuredLevelMeter = await h.configureUnit(
    levelMeter,
    [
      { pointer: "/minLevel", value: 65 },
      { pointer: "/scaleReference", value: 1 }
    ],
    {
      role: "rawAudio",
      unitId: acquisition.id,
      unitName: acquisition.name
    });
  const levelBinding = configuredLevelMeter.bindings.find(
    (binding) => binding.inputRole === "rawAudio");
  const levelSnapshot = await h.active();
  const levelTab = levelSnapshot.project.displayTabs.find(
    (tab) => tab.owner.unitId === levelMeter.id);
  const levelDisplay = levelTab?.displays.find(
    (display) =>
      display.providerTypeId === "pamguard.level-meter-display");
  h.assert(
    configuredLevelMeter.settings.minLevel === -65 &&
    configuredLevelMeter.settings.scaleReference === 1 &&
    configuredLevelMeter.settings.scaleType === 0 &&
    levelBinding?.sources?.[0]?.unitId === acquisition.id &&
    levelBinding.sources[0].outputRole === "rawAudio" &&
    levelTab?.owner.role === "levelMeter" &&
    levelTab.displays.length === 1 &&
    levelDisplay?.owner.unitId === levelMeter.id &&
    levelDisplay.owner.role === "levelMeter" &&
    levelDisplay.source.unitId === levelMeter.id &&
    levelDisplay.source.outputRole === "levels",
    "Level Meter settings, raw source, or static display ownership were " +
      "not project-authoritative",
    { unit: configuredLevelMeter, tab: levelTab });
  await h.assertDisplayMenuContributions([
    {
      ownerUnitId: levelMeter.id,
      providerTypeId: "pamguard.level-meter-display",
      disabled: true
    }
  ]);

  const amplifier = await h.addUnit(
    "pamguard.amplifier",
    "Signal Amplifier",
    "Browser Amplifier");
  const configuredAmplifier = await h.configureUnit(
    amplifier,
    [
      { pointer: "/channelSettings/0/gainDb", value: 6.25 },
      { pointer: "/channelSettings/0/invert", value: true },
      { pointer: "/channelSettings/1/gainDb", value: -3 }
    ],
    {
      role: "rawAudio",
      unitId: acquisition.id,
      unitName: acquisition.name
    });
  const amplifierBinding = configuredAmplifier.bindings.find(
    (binding) => binding.inputRole === "rawAudio");
  h.assert(
    configuredAmplifier.settings.channelSettings.length === 32 &&
    configuredAmplifier.settings.channelSettings[0].gainDb === 6.25 &&
    configuredAmplifier.settings.channelSettings[0].invert === true &&
    configuredAmplifier.settings.channelSettings[1].gainDb === -3 &&
    configuredAmplifier.settings.channelSettings[1].invert === false &&
    amplifierBinding?.sources?.[0]?.unitId === acquisition.id &&
    amplifierBinding.sources[0].outputRole === "rawAudio",
    "Signal Amplifier settings/source were not project-authoritative",
    configuredAmplifier);

  const patchPanel = await h.addUnit(
    "pamguard.patch-panel",
    "Patch Panel",
    "Browser Patch Panel");
  const unboundPatchProject = await h.active();
  const unboundPatch = unboundPatchProject.project.controlledUnits.find(
    (unit) => unit.id === patchPanel.id);
  h.assert(
    unboundPatchProject.projection.status === "needs-configuration" &&
    unboundPatch.bindings.find(
      (binding) => binding.inputRole === "rawAudio")
      ?.sources?.length === 0,
    "Adding a unit with multiple compatible raw sources did not leave an " +
      "explicit operator source choice",
    unboundPatchProject);
  const configuredPatchPanel = await h.configureUnit(
    patchPanel,
    [
      { pointer: "/routingMatrix/0/0", value: false },
      { pointer: "/routingMatrix/0/2", value: true },
      { pointer: "/routingMatrix/1/1", value: true }
    ],
    {
      role: "rawAudio",
      unitId: amplifier.id,
      unitName: amplifier.name
    });
  const patchBinding = configuredPatchPanel.bindings.find(
    (binding) => binding.inputRole === "rawAudio");
  h.assert(
    configuredPatchPanel.settings.routingMatrix.length === 32 &&
    configuredPatchPanel.settings.routingMatrix.every(
      (row) => row.length === 32) &&
    configuredPatchPanel.settings.routingMatrix[0][0] === false &&
    configuredPatchPanel.settings.routingMatrix[0][2] === true &&
    configuredPatchPanel.settings.routingMatrix[1][1] === true &&
    configuredPatchPanel.settings.advancedGainMatrix === null &&
    patchBinding?.sources?.[0]?.unitId === amplifier.id &&
    patchBinding.sources[0].outputRole === "amplifiedAudio",
    "Patch Panel matrix/source were not project-authoritative",
    configuredPatchPanel);

  const filter = await h.addUnit(
    "pamguard.filter",
    "Filters (IIR and FIR)",
    "Browser Filter");
  const configuredFilter = await h.configureUnit(
    filter,
    [
      { pointer: "/channelBitmap/0", value: true },
      { pointer: "/channelBitmap/1", value: true },
      { pointer: "/type", value: "butterworth" },
      { pointer: "/band", value: "bandPass" },
      { pointer: "/order", value: 4 },
      { pointer: "/highPassFreqHz", value: 2500 },
      { pointer: "/lowPassFreqHz", value: 18000 }
    ],
    {
      role: "rawAudio",
      unitId: acquisition.id,
      unitName: acquisition.name
    });
  const filterBinding = configuredFilter.bindings.find(
    (binding) => binding.inputRole === "rawAudio");
  h.assert(
    configuredFilter.settings.channelBitmap === 3 &&
    configuredFilter.settings.type === "butterworth" &&
    configuredFilter.settings.band === "bandPass" &&
    configuredFilter.settings.order === 4 &&
    configuredFilter.settings.highPassFreqHz === 2500 &&
    configuredFilter.settings.lowPassFreqHz === 18000 &&
    filterBinding?.sources?.[0]?.unitId === acquisition.id &&
    filterBinding.sources[0].outputRole === "rawAudio",
    "Filter settings/source were not project-authoritative",
    configuredFilter);

  const decimator = await h.addUnit(
    "pamguard.decimator",
    "Decimator",
    "Browser Decimator");
  const configuredDecimator = await h.configureUnit(
    decimator,
    [
      { pointer: "/channelBitmap/0", value: true },
      { pointer: "/channelBitmap/1", value: true },
      { pointer: "/outputSampleRateHz", value: 24000 },
      { pointer: "/interpolation", value: 0 },
      { pointer: "/filter/type", value: "butterworth" },
      { pointer: "/filter/band", value: "lowPass" },
      { pointer: "/filter/order", value: 6 },
      { pointer: "/filter/lowPassFreqHz", value: 12000 }
    ],
    {
      role: "rawAudio",
      unitId: filter.id,
      unitName: filter.name
    });
  const decimatorBinding = configuredDecimator.bindings.find(
    (binding) => binding.inputRole === "rawAudio");
  h.assert(
    configuredDecimator.settings.channelBitmap === 3 &&
    configuredDecimator.settings.outputSampleRateHz === 24000 &&
    configuredDecimator.settings.interpolation === 0 &&
    configuredDecimator.settings.filter.type === "butterworth" &&
    configuredDecimator.settings.filter.band === "lowPass" &&
    configuredDecimator.settings.filter.order === 6 &&
    configuredDecimator.settings.filter.lowPassFreqHz === 12000 &&
    decimatorBinding?.sources?.[0]?.unitId === filter.id &&
    decimatorBinding.sources[0].outputRole === "filteredAudio",
    "Decimator settings/source were not project-authoritative",
    configuredDecimator);

  const fft = await h.addUnit(
    "pamguard.fft",
    "FFT (Spectrogram) Engine",
    "Browser FFT");
  const configuredFft = await h.configureUnit(
    fft,
    [
      { pointer: "/fft/fftLength", value: 2048 },
      { pointer: "/fft/fftHop", value: 1024 }
    ],
    {
      role: "rawAudio",
      unitId: acquisition.id,
      unitName: acquisition.name,
      sampleRateHz: 96000
    });
  const rawBinding = configuredFft.bindings.find(
    (binding) => binding.inputRole === "rawAudio");
  h.assert(
    configuredFft.settings.fft.fftLength === 2048 &&
    configuredFft.settings.fft.fftHop === 1024 &&
    rawBinding?.sources?.length === 1 &&
    rawBinding.sources[0].unitId === acquisition.id &&
    rawBinding.sources[0].outputRole === "rawAudio",
    "FFT settings/source were not project-authoritative",
    configuredFft);

  const secondFft = await h.addUnit(
    "pamguard.fft",
    "FFT (Spectrogram) Engine",
    "Browser Decimated FFT");
  const configuredSecondFft = await h.configureUnit(
    secondFft,
    [
      { pointer: "/fft/fftLength", value: 1024 },
      { pointer: "/fft/fftHop", value: 256 }
    ],
    {
      role: "rawAudio",
      unitId: decimator.id,
      unitName: decimator.name,
      sampleRateHz: 24000
    });
  const secondRawBinding = configuredSecondFft.bindings.find(
    (binding) => binding.inputRole === "rawAudio");
  h.assert(
    configuredSecondFft.settings.fft.fftLength === 1024 &&
    configuredSecondFft.settings.fft.fftHop === 256 &&
    secondRawBinding?.sources?.[0]?.unitId === decimator.id &&
    secondRawBinding.sources[0].outputRole === "decimatedAudio",
    "Decimated FFT did not retain its independent Decimator source",
    configuredSecondFft);
  await h.assertGraphIncompatible(
    {
      unitId: fft.id,
      unitName: fft.name,
      outputRole: "fft"
    },
    {
      unitId: secondFft.id,
      unitName: secondFft.name,
      inputRole: "rawAudio"
    });
  const graphReconnectedSecondFft = await h.reconnectGraph(
    {
      unitId: acquisition.id,
      unitName: acquisition.name,
      outputRole: "rawAudio"
    },
    {
      unitId: secondFft.id,
      unitName: secondFft.name,
      inputRole: "rawAudio"
    });
  h.assert(
    graphReconnectedSecondFft.bindings.find(
      (binding) => binding.inputRole === "rawAudio")
      ?.sources?.[0]?.unitId === acquisition.id,
    "Graph reconnect did not replace the FFT's 1-cardinality binding",
    graphReconnectedSecondFft.bindings);
  await h.assertDialogSource(secondFft, {
    inputRole: "rawAudio",
    unitId: acquisition.id,
    outputRole: "rawAudio"
  });
  const dialogReconnectedSecondFft = await h.configureUnit(
    secondFft,
    [],
    {
      role: "rawAudio",
      unitId: decimator.id,
      unitName: decimator.name,
      sampleRateHz: 24000
    });
  h.assert(
    dialogReconnectedSecondFft.bindings.find(
      (binding) => binding.inputRole === "rawAudio")
      ?.sources?.[0]?.unitId === decimator.id,
    "Settings dialog did not restore the Decimator source",
    dialogReconnectedSecondFft.bindings);
  await h.waitFor(
    () => document.querySelector(
      `.model-wire[data-source-unit-id="${CSS.escape(decimator.id)}"]` +
      '[data-source-output-role="decimatedAudio"]' +
      `[data-target-unit-id="${CSS.escape(secondFft.id)}"]` +
      '[data-target-input-role="rawAudio"]'),
    "settings-to-authoritative-wire round trip");
  const fftBaseName = "FFT (Spectrogram) Engine";
  h.assert(
    await h.suggestedUnitName(
      "pamguard.fft",
      fftBaseName) === fftBaseName,
    "Unique-name suggestion did not offer the free PAMGuard base name");
  const nameProbeOne = await h.addUnit(
    "pamguard.fft",
    fftBaseName,
    fftBaseName);
  const nameProbeTwo = await h.addUnit(
    "pamguard.fft",
    fftBaseName,
    `${fftBaseName} 2`);
  const nameProbeThree = await h.addUnit(
    "pamguard.fft",
    fftBaseName,
    `${fftBaseName} 3`);
  const renamedNameProbeTwo = await h.renameUnit(
    nameProbeTwo,
    "Browser Renamed Name Probe");
  h.assert(
    await h.suggestedUnitName(
      "pamguard.fft",
      fftBaseName) === `${fftBaseName} 2`,
    "Rename did not release the first PAMGuard-style numeric suffix");
  const restoredNameProbeTwo = await h.renameUnit(
    renamedNameProbeTwo,
    `${fftBaseName} 2`);
  await h.removeUnit(restoredNameProbeTwo);
  h.assert(
    await h.suggestedUnitName(
      "pamguard.fft",
      fftBaseName) === `${fftBaseName} 2`,
    "Removal did not release the first PAMGuard-style numeric suffix");
  await h.removeUnit(nameProbeOne);
  await h.removeUnit(nameProbeThree);

  const userDisplay = await h.addUnit(
    "pamguard.user-display",
    "User Display",
    "Browser Displays");
  const emptyOwnedTab = await h.waitFor(
    () => h.ownedTab(userDisplay.id),
    "empty User Display owned tab");
  const beforeDisplay = await h.active();
  const ownedTabDocument = beforeDisplay.project.displayTabs.find(
    (tab) => tab.owner.unitId === userDisplay.id);
  h.assert(
    emptyOwnedTab.owner === userDisplay.id &&
    ownedTabDocument &&
    ownedTabDocument.owner.role === "main" &&
    ownedTabDocument.displays.length === 0,
    "User Display did not create one empty owned tab",
    beforeDisplay.project.displayTabs);

  const spectrogram = await h.addDisplay(
    userDisplay,
    "pamguard.spectrogram-display",
    "Spectrogram Display",
    {
      role: "fft",
      unitId: fft.id,
      unitName: fft.name
    });
  const configuredSpectrogram = await h.configureDisplay(
    spectrogram,
    [
      { pointer: "/nPanels", value: 2 },
      { pointer: "/channelList/0", value: 0 },
      { pointer: "/channelList/1", value: 1 },
      { pointer: "/frequencyLimits/0", value: 0 },
      { pointer: "/frequencyLimits/1", value: 18000 },
      { pointer: "/amplitudeLimits/0", value: 55 },
      { pointer: "/amplitudeLimits/1", value: 105 },
      { pointer: "/colourMap", value: "FIRE" },
      { pointer: "/timeScaleFixed", value: true },
      { pointer: "/displayLength", value: 30 },
      { pointer: "/wrapDisplay", value: false }
    ],
    {
      role: "fft",
      unitId: fft.id,
      unitName: fft.name
    });
  h.assert(
    configuredSpectrogram.owner.unitId === userDisplay.id &&
    configuredSpectrogram.source.unitId === fft.id &&
    configuredSpectrogram.source.outputRole === "fft" &&
    configuredSpectrogram.settings.nPanels === 2 &&
    configuredSpectrogram.settings.channelList.join(",") === "0,1" &&
    configuredSpectrogram.settings.frequencyLimits.join(",") ===
      "0,18000" &&
    configuredSpectrogram.settings.amplitudeLimits.join(",") ===
      "55,105" &&
    configuredSpectrogram.settings.colourMap === "FIRE" &&
    configuredSpectrogram.settings.timeScaleFixed === true &&
    configuredSpectrogram.settings.displayLength === 30 &&
    configuredSpectrogram.settings.wrapDisplay === false &&
    !Object.hasOwn(configuredSpectrogram.settings, "sourceName"),
    "Spectrogram settings/source/owner are not one project object",
    configuredSpectrogram);
  const secondSpectrogram = await h.addDisplay(
    userDisplay,
    "pamguard.spectrogram-display",
    "Spectrogram Display",
    {
      role: "fft",
      unitId: secondFft.id,
      unitName: secondFft.name
    });
  const configuredSecondSpectrogram = await h.configureDisplay(
    secondSpectrogram,
    [
      { pointer: "/channelList/0", value: 1 },
      { pointer: "/frequencyLimits/0", value: 2000 },
      { pointer: "/frequencyLimits/1", value: 12000 },
      { pointer: "/amplitudeLimits/0", value: 60 },
      { pointer: "/amplitudeLimits/1", value: 110 },
      { pointer: "/colourMap", value: "BLUE" },
      { pointer: "/timeScaleFixed", value: false },
      { pointer: "/pixelsPerSlics", value: 4 },
      { pointer: "/wrapDisplay", value: true }
    ],
    {
      role: "fft",
      unitId: secondFft.id,
      unitName: secondFft.name,
      sampleRateHz: 24000,
      fftLength: 1024,
      nyquistHz: 12000
    });
  h.assert(
    configuredSecondSpectrogram.source.unitId === secondFft.id &&
    configuredSecondSpectrogram.settings.nPanels === 1 &&
    configuredSecondSpectrogram.settings.channelList.join(",") === "1" &&
    configuredSecondSpectrogram.settings.frequencyLimits.join(",") ===
      "2000,12000" &&
    configuredSecondSpectrogram.settings.frequencyLimits[1] ===
      configuredDecimator.settings.outputSampleRateHz / 2 &&
    configuredSecondSpectrogram.settings.colourMap === "BLUE" &&
    configuredSecondSpectrogram.settings.timeScaleFixed === false &&
    configuredSecondSpectrogram.settings.pixelsPerSlics === 4 &&
    configuredSecondSpectrogram.settings.wrapDisplay === true &&
    configuredSecondSpectrogram.id !== configuredSpectrogram.id,
    "Decimated Spectrogram did not keep its source, channels, or Nyquist " +
      "frequency limit",
    configuredSecondSpectrogram);
  const displayElement = await h.waitFor(
    () => h.displayRoot(spectrogram.id),
    "owned Spectrogram DOM");
  h.assert(
    (displayElement.getAttribute(
      "data-owner-controlled-unit-id") ||
      displayElement.getAttribute("data-owner-unit-id")) ===
        userDisplay.id,
    "Spectrogram DOM omitted its controlled-unit owner");
  const activeOwnedTab = await h.waitFor(
    () => h.ownedTab(userDisplay.id),
    "current User Display owned tab");
  activeOwnedTab.element.click();
  const displayGeometry = await h.waitFor(() => {
    const element = h.displayRoot(spectrogram.id);
    const rectangle = element?.getBoundingClientRect();
    const visual = element?.querySelector(".spectrogram-foundation");
    const visualRectangle = visual?.getBoundingClientRect();
    if (!rectangle ||
        rectangle.height <= 200 ||
        !visualRectangle ||
        visualRectangle.height <= 100) return null;
    return {
      displayHeight: rectangle.height,
      visualHeight: visualRectangle.height,
      gridDisplay: getComputedStyle(
        element.closest(".display-grid")).display
    };
  }, "non-collapsed Spectrogram surface");
  h.assert(
    displayGeometry.gridDisplay === "grid" ||
    displayGeometry.gridDisplay === "block",
    "Spectrogram hierarchy ignored its persisted layout mode",
    displayGeometry);
  await h.startRuntime();
  await h.waitFor(() => {
    const status = h.displayRoot(spectrogram.id)?.querySelector(
      `[data-display-stream-status="${CSS.escape(spectrogram.id)}"]`);
    return String(status?.textContent || "").toLowerCase() === "live"
      ? status
      : null;
  }, "Spectrogram stream subscription");
  await h.waitFor(() => {
    const status = h.displayRoot(secondSpectrogram.id)?.querySelector(
      `[data-display-stream-status="${CSS.escape(
        secondSpectrogram.id)}"]`);
    return String(status?.textContent || "").toLowerCase() === "live"
      ? status
      : null;
  }, "second Spectrogram stream subscription");
  const soundRecorderEvidence =
    await h.soundRecorderTransportWorkflow(
      soundRecorder,
      acquisition);
  (await h.waitFor(
    () => h.ownedTab(userDisplay.id),
    "User Display tab after Sound Recorder transport"))
    .element.click();
  await h.injectAcquisitionAudio(acquisition.id);
  const liveSpectrogram = await h.assertLiveSpectrogram(spectrogram.id);
  const secondLiveSpectrogram =
    await h.assertLiveSpectrogram(secondSpectrogram.id);
  h.assert(
    liveSpectrogram.colours > 3 &&
    secondLiveSpectrogram.colours > 3,
    "Independent project-owned Spectrograms did not both render FFT data",
    { liveSpectrogram, secondLiveSpectrogram });
  await h.injectAcquisitionAudio(acquisition.id);
  const activeLevelTab = await h.waitFor(
    () => h.ownedTab(levelMeter.id),
    "Level Meter owned display tab");
  activeLevelTab.element.click();
  const liveLevel = await h.waitFor(() => {
    const root = h.displayRoot(levelDisplay.id);
    const status = root?.querySelector(
      `[data-display-stream-status="${CSS.escape(
        levelDisplay.id)}"]`);
    const rows = root?.querySelectorAll(
      "[data-level-meter-channel]");
    const scale = root?.querySelector(
      ".project-level-meter-scale strong");
    return /^live\b/i.test(String(status?.textContent || "").trim()) &&
      rows?.length === 2 &&
      /db re\. 1v peak/i.test(scale?.textContent || "")
      ? {
          status: status.textContent,
          rows: Array.from(rows).map((row) => row.textContent),
          scale: scale.textContent
        }
      : null;
  }, "project-owned live Level Meter", 10000);
  h.assert(
    liveLevel.rows.every((row) => /db/i.test(row)),
    "Level Meter did not render both calibrated channel values",
    liveLevel);
  await h.stopRuntime();
  h.tabs().find((tab) => tab.kind === "data-model")
    .element.click();

  let replacementSnapshot = await h.removeUnit(fft, true);
  const unboundOldDisplay = replacementSnapshot.project.displayTabs
    .flatMap((tab) => tab.displays)
    .find((display) => display.id === spectrogram.id);
  const retainedDecimatedDisplay = replacementSnapshot.project.displayTabs
    .flatMap((tab) => tab.displays)
    .find((display) => display.id === secondSpectrogram.id);
  h.assert(
    unboundOldDisplay?.source === null &&
    retainedDecimatedDisplay?.source?.unitId === secondFft.id &&
    !replacementSnapshot.project.controlledUnits.some(
      (unit) => unit.id === fft.id),
    "Removing the raw FFT did not isolate its display from the decimated " +
      "branch",
    replacementSnapshot.project);

  replacementSnapshot = await h.removeUnit(userDisplay);
  const removedDisplayIds = new Set(
    replacementSnapshot.project.displayTabs.flatMap(
      (tab) => tab.displays.map((display) => display.id)));
  const oldDisplaySurface = [spectrogram.id, secondSpectrogram.id]
    .map((displayId) => document.querySelector(
      `[data-pamguard-display-instance-id="${CSS.escape(displayId)}"],` +
      `[data-project-display-id="${CSS.escape(displayId)}"],` +
      `[data-display-instance-id="${CSS.escape(displayId)}"],` +
      `[data-display-stream-status="${CSS.escape(displayId)}"]`))
    .find(Boolean);
  h.assert(
    replacementSnapshot.project.displayTabs.every(
      (tab) => tab.owner.unitId !== userDisplay.id) &&
    !removedDisplayIds.has(spectrogram.id) &&
    !removedDisplayIds.has(secondSpectrogram.id) &&
    !oldDisplaySurface,
    "Removing the original User Display left an owned display or stream " +
      "surface behind",
    {
      tabs: replacementSnapshot.project.displayTabs,
      oldDisplaySurface: oldDisplaySurface?.outerHTML || null
    });

  const replacementFft = await h.addUnit(
    "pamguard.fft",
    "FFT (Spectrogram) Engine",
    "Browser FFT Re-added");
  const configuredReplacementFft = await h.configureUnit(
    replacementFft,
    [
      { pointer: "/fft/fftLength", value: 2048 },
      { pointer: "/fft/fftHop", value: 1024 }
    ],
    {
      role: "rawAudio",
      unitId: acquisition.id,
      unitName: acquisition.name,
      sampleRateHz: 96000
    });
  const replacementFftBinding =
    configuredReplacementFft.bindings.find(
      (binding) => binding.inputRole === "rawAudio");
  const replacementUserDisplay = await h.addUnit(
    "pamguard.user-display",
    "User Display",
    "Browser Displays Re-added");
  const replacementSpectrogram = await h.addDisplay(
    replacementUserDisplay,
    "pamguard.spectrogram-display",
    "Spectrogram Display",
    {
      role: "fft",
      unitId: replacementFft.id,
      unitName: replacementFft.name
    });
  const configuredReplacementSpectrogram = await h.configureDisplay(
    replacementSpectrogram,
    [
      { pointer: "/nPanels", value: 2 },
      { pointer: "/channelList/0", value: 0 },
      { pointer: "/channelList/1", value: 1 },
      { pointer: "/frequencyLimits/0", value: 0 },
      { pointer: "/frequencyLimits/1", value: 18000 },
      { pointer: "/amplitudeLimits/0", value: 55 },
      { pointer: "/amplitudeLimits/1", value: 105 },
      { pointer: "/colourMap", value: "FIRE" },
      { pointer: "/timeScaleFixed", value: true },
      { pointer: "/displayLength", value: 30 },
      { pointer: "/wrapDisplay", value: false }
    ],
    {
      role: "fft",
      unitId: replacementFft.id,
      unitName: replacementFft.name
    });
  const replacementDecimatedSpectrogram = await h.addDisplay(
    replacementUserDisplay,
    "pamguard.spectrogram-display",
    "Spectrogram Display",
    {
      role: "fft",
      unitId: secondFft.id,
      unitName: secondFft.name
    });
  const configuredReplacementDecimatedSpectrogram =
    await h.configureDisplay(
      replacementDecimatedSpectrogram,
      [
        { pointer: "/channelList/0", value: 1 },
        { pointer: "/frequencyLimits/0", value: 2000 },
        { pointer: "/frequencyLimits/1", value: 12000 },
        { pointer: "/amplitudeLimits/0", value: 60 },
        { pointer: "/amplitudeLimits/1", value: 110 },
        { pointer: "/colourMap", value: "BLUE" },
        { pointer: "/timeScaleFixed", value: false },
        { pointer: "/pixelsPerSlics", value: 4 },
        { pointer: "/wrapDisplay", value: true }
      ],
      {
        role: "fft",
        unitId: secondFft.id,
        unitName: secondFft.name,
        sampleRateHz: 24000,
        fftLength: 1024,
        nyquistHz: 12000
      });
  const replacementProject = await h.active();
  const replacementTab = replacementProject.project.displayTabs.find(
    (tab) => tab.owner.unitId === replacementUserDisplay.id);
  h.assert(
    replacementFft.id !== fft.id &&
    replacementUserDisplay.id !== userDisplay.id &&
    replacementSpectrogram.id !== spectrogram.id &&
    replacementDecimatedSpectrogram.id !== secondSpectrogram.id &&
    replacementFftBinding?.sources?.length === 1 &&
    replacementFftBinding.sources[0].unitId === acquisition.id &&
    replacementFftBinding.sources[0].outputRole === "rawAudio" &&
    configuredReplacementSpectrogram.owner.unitId ===
      replacementUserDisplay.id &&
    configuredReplacementSpectrogram.source.unitId ===
      replacementFft.id &&
    configuredReplacementDecimatedSpectrogram.owner.unitId ===
      replacementUserDisplay.id &&
    configuredReplacementDecimatedSpectrogram.source.unitId ===
      secondFft.id &&
    configuredReplacementDecimatedSpectrogram.settings
      .frequencyLimits[1] ===
        configuredDecimator.settings.outputSampleRateHz / 2 &&
    replacementTab?.displays?.length === 2 &&
    !replacementProject.project.controlledUnits.some(
      (unit) => unit.id === fft.id || unit.id === userDisplay.id) &&
    !replacementProject.project.displayTabs.flatMap(
      (tab) => tab.displays).some(
        (display) =>
          display.id === spectrogram.id ||
          display.id === secondSpectrogram.id),
    "Re-added FFT/User Display workflow did not receive new identities, " +
      "bindings, ownership, or a decimated Nyquist-constrained branch",
    replacementProject.project);

  replacementTab && (await h.waitFor(
    () => h.ownedTab(replacementUserDisplay.id),
    "re-added User Display tab")).element.click();
  await h.startRuntime();
  await h.injectAcquisitionAudio(acquisition.id);
  const replacementRawFrames = await h.assertLiveSpectrogram(
    replacementSpectrogram.id);
  const replacementDecimatedFrames = await h.assertLiveSpectrogram(
    replacementDecimatedSpectrogram.id);
  h.assert(
    replacementRawFrames.colours > 3 &&
    replacementDecimatedFrames.colours > 3 &&
    !document.querySelector(
      `[data-display-stream-status="${CSS.escape(spectrogram.id)}"],` +
      `[data-display-stream-status="${CSS.escape(secondSpectrogram.id)}"]`),
    "Re-added raw/decimated displays did not render independently or an " +
      "old stream surface returned",
    { replacementRawFrames, replacementDecimatedFrames });
  await h.stopRuntime();
  h.tabs().find((tab) => tab.kind === "data-model")
    .element.click();
  await h.removeUnit(soundRecorder);

  const saved = await h.saveAs("Project Shell Browser Smoke");
  const savedAcquisition = saved.project.controlledUnits.find(
    (unit) => unit.id === acquisition.id);
  const savedSoundOutput = saved.project.controlledUnits.find(
    (unit) => unit.id === soundOutput.id);
  const savedLevelMeter = saved.project.controlledUnits.find(
    (unit) => unit.id === levelMeter.id);
  const savedAmplifier = saved.project.controlledUnits.find(
    (unit) => unit.id === amplifier.id);
  const savedPatchPanel = saved.project.controlledUnits.find(
    (unit) => unit.id === patchPanel.id);
  const savedFilter = saved.project.controlledUnits.find(
    (unit) => unit.id === filter.id);
  const savedDecimator = saved.project.controlledUnits.find(
    (unit) => unit.id === decimator.id);
  const savedFft = saved.project.controlledUnits.find(
    (unit) => unit.id === replacementFft.id);
  const savedSecondFft = saved.project.controlledUnits.find(
    (unit) => unit.id === secondFft.id);
  const savedOwner = saved.project.controlledUnits.find(
    (unit) => unit.id === replacementUserDisplay.id);
  const savedDisplay = saved.project.displayTabs.flatMap(
    (tab) => tab.displays).find(
      (display) => display.id === replacementSpectrogram.id);
  const savedSecondDisplay = saved.project.displayTabs.flatMap(
    (tab) => tab.displays).find(
      (display) => display.id === replacementDecimatedSpectrogram.id);
  const savedLevelDisplay = saved.project.displayTabs.flatMap(
    (tab) => tab.displays).find(
      (display) => display.id === levelDisplay.id);
  h.assert(
    savedAcquisition && savedSoundOutput && savedLevelMeter &&
    savedAmplifier && savedPatchPanel && savedFilter && savedDecimator &&
    savedFft && savedSecondFft && savedOwner &&
    savedDisplay && savedSecondDisplay && savedLevelDisplay &&
    saved.dirty === false,
    "Save As changed or omitted project-owned identities",
    saved);
  const openProjectButton = await h.waitFor(() => {
    const button = document.getElementById("fileOpen");
    return button && !button.disabled ? button : null;
  }, "enabled Open project action");
  openProjectButton.click();
  const openProjectDialog = await h.waitFor(() => {
    const dialog = document.getElementById("formDialog");
    return dialog?.open &&
      /open project/i.test(
        document.getElementById("dialogTitle")?.textContent || "")
      ? dialog
      : null;
  }, "Open project saved-project list");
  const savedProjectOption = Array.from(
    openProjectDialog.querySelectorAll("option")).find(
      (option) => option.value === saved.project.projectId);
  h.assert(
    savedProjectOption &&
    savedProjectOption.textContent.includes(
      `revision ${saved.savedRevision}`) &&
    !savedProjectOption.textContent.includes("undefined"),
    "Open project did not render the durable saved revision",
    savedProjectOption?.textContent);
  openProjectDialog.querySelector(
    "button[value='cancel']")?.click();
  await h.waitFor(
    () => !document.getElementById("formDialog").open,
    "Open project dialog cancellation");
  const openApiLink = document.querySelector(
    'a[href="/openapi.yaml"]');
  h.assert(
    openApiLink,
    "Help did not expose the served OpenAPI route");
  h.assert(
    document.getElementById("connectionSettings")
      ?.closest("[data-project-menu]")
      ?.getAttribute("data-project-menu") === "help",
    "Engine connection settings were not isolated under Help");
  h.assertNoParallelWorkflow();
  return JSON.stringify({
    template: {
      projectId: savedTemplate.project.projectId,
      savedRevision: savedTemplate.savedRevision,
      controlledUnits: savedTemplate.project.controlledUnits,
      displayTabs: savedTemplate.project.displayTabs,
      dataModelLayout: savedTemplate.project.dataModelLayout,
      acquisitionId: templateAcquisition.id,
      userDisplayId: templateUserDisplay.id,
      spectrogramId: templateSpectrogram.id,
      clickId: templateClick.id,
      clickDisplayId: templateClickDisplay.id,
      matchedTemplateId: templateMatchedTemplate.id,
      matchedTemplateEvidence
    },
    projectId: saved.project.projectId,
    projectName: saved.project.metadata.name,
    acquisition: {
      id: acquisition.id,
      name: acquisition.name
    },
    soundOutput: {
      id: soundOutput.id,
      name: soundOutput.name
    },
    soundRecorderEvidence,
    levelMeter: {
      id: levelMeter.id,
      name: levelMeter.name
    },
    amplifier: {
      id: amplifier.id,
      name: amplifier.name
    },
    patchPanel: {
      id: patchPanel.id,
      name: patchPanel.name
    },
    filter: {
      id: filter.id,
      name: filter.name
    },
    decimator: {
      id: decimator.id,
      name: decimator.name
    },
    fft: {
      id: replacementFft.id,
      name: replacementFft.name
    },
    secondFft: {
      id: secondFft.id,
      name: secondFft.name
    },
    userDisplay: {
      id: replacementUserDisplay.id,
      name: replacementUserDisplay.name
    },
    displayId: replacementSpectrogram.id,
    secondDisplayId: replacementDecimatedSpectrogram.id,
    removedWorkflow: {
      fftId: fft.id,
      ownerId: userDisplay.id,
      displayIds: [spectrogram.id, secondSpectrogram.id]
    },
    replacementLiveFrames: {
      raw: replacementRawFrames,
      decimated: replacementDecimatedFrames
    },
    levelDisplayId: levelDisplay.id,
    levelDisplayTabId: levelTab.id,
    displayTabId: saved.project.displayTabs.find(
      (tab) => tab.owner.unitId === replacementUserDisplay.id).id,
    savedRevision: saved.savedRevision,
    savedEtag: saved.etag,
    requestCount: h.requestHistory().length
  });
})()
'@

$phaseBTemplate = @'
(async () => {
  const expected = JSON.parse(atob("__EXPECTED_BASE64__"));
  const h = globalThis.__pamguardProjectShellSmoke;
  const restored = await h.waitActive(
    (snapshot) =>
      snapshot.project.projectId === expected.projectId &&
      snapshot.dirty === false,
    "saved project after service restart");
  const acquisition = restored.project.controlledUnits.find(
    (unit) => unit.id === expected.acquisition.id);
  const soundOutput = restored.project.controlledUnits.find(
    (unit) => unit.id === expected.soundOutput.id);
  const levelMeter = restored.project.controlledUnits.find(
    (unit) => unit.id === expected.levelMeter.id);
  const amplifier = restored.project.controlledUnits.find(
    (unit) => unit.id === expected.amplifier.id);
  const patchPanel = restored.project.controlledUnits.find(
    (unit) => unit.id === expected.patchPanel.id);
  const filter = restored.project.controlledUnits.find(
    (unit) => unit.id === expected.filter.id);
  const decimator = restored.project.controlledUnits.find(
    (unit) => unit.id === expected.decimator.id);
  const fft = restored.project.controlledUnits.find(
    (unit) => unit.id === expected.fft.id);
  const secondFft = restored.project.controlledUnits.find(
    (unit) => unit.id === expected.secondFft.id);
  const userDisplay = restored.project.controlledUnits.find(
    (unit) => unit.id === expected.userDisplay.id);
  const tab = restored.project.displayTabs.find(
    (candidate) => candidate.id === expected.displayTabId);
  const display = tab?.displays.find(
    (candidate) => candidate.id === expected.displayId);
  const secondDisplay = tab?.displays.find(
    (candidate) => candidate.id === expected.secondDisplayId);
  const levelTab = restored.project.displayTabs.find(
    (candidate) => candidate.id === expected.levelDisplayTabId);
  const levelDisplay = levelTab?.displays.find(
    (candidate) => candidate.id === expected.levelDisplayId);
  h.assert(
    acquisition?.settings.sampleRate === 96000 &&
    acquisition?.settings.hardwareChannelList.join(",") === "3,7" &&
    acquisition?.settings.hydrophoneList.join(",") === "1,0" &&
    acquisition?.settings.preamplifier.gainDb === 24 &&
    acquisition?.settings.dcTimeConstantSeconds === 2.5 &&
    !Object.hasOwn(acquisition?.settings || {}, "sourceId") &&
    soundOutput?.settings.channelBitmap === 3 &&
    soundOutput?.settings.playbackGainDb === 3 &&
    soundOutput?.settings.hpFilter === 0.01 &&
    !Object.hasOwn(soundOutput?.settings || {}, "deviceId") &&
    soundOutput?.bindings.some((binding) =>
      binding.inputRole === "audio" &&
      binding.sources?.some((source) =>
        source.unitId === acquisition.id &&
        source.outputRole === "rawAudio")) &&
    levelMeter?.settings.minLevel === -65 &&
    levelMeter?.settings.scaleReference === 1 &&
    levelMeter?.settings.scaleType === 0 &&
    levelMeter?.bindings.some((binding) =>
      binding.inputRole === "rawAudio" &&
      binding.sources?.some((source) =>
        source.unitId === acquisition.id &&
        source.outputRole === "rawAudio")) &&
    amplifier?.settings.channelSettings[0].gainDb === 6.25 &&
    amplifier?.settings.channelSettings[0].invert === true &&
    amplifier?.bindings[0]?.sources[0]?.unitId === acquisition.id &&
    patchPanel?.settings.routingMatrix[0][2] === true &&
    patchPanel?.settings.advancedGainMatrix === null &&
    patchPanel?.bindings[0]?.sources[0]?.unitId === amplifier.id &&
    filter?.settings.channelBitmap === 3 &&
    filter?.settings.highPassFreqHz === 2500 &&
    filter?.settings.lowPassFreqHz === 18000 &&
    filter?.bindings[0]?.sources[0]?.unitId === acquisition.id &&
    decimator?.settings.outputSampleRateHz === 24000 &&
    decimator?.settings.filter.lowPassFreqHz === 12000 &&
    decimator?.bindings[0]?.sources[0]?.unitId === filter.id &&
    fft?.settings.fft.fftLength === 2048 &&
    fft?.settings.fft.fftHop === 1024 &&
    secondFft?.settings.fft.fftLength === 1024 &&
    secondFft?.settings.fft.fftHop === 256 &&
    secondFft?.bindings?.some((binding) =>
      binding.inputRole === "rawAudio" &&
      binding.sources?.some((source) =>
        source.unitId === decimator.id &&
        source.outputRole === "decimatedAudio")) &&
    userDisplay &&
    tab?.owner.unitId === userDisplay.id &&
    display?.owner.unitId === userDisplay.id &&
    display?.source.unitId === fft.id &&
    display?.settings.nPanels === 2 &&
    display?.settings.channelList.join(",") === "0,1" &&
    display?.settings.frequencyLimits.join(",") === "0,18000" &&
    display?.settings.colourMap === "FIRE" &&
    display?.settings.displayLength === 30 &&
    secondDisplay?.owner.unitId === userDisplay.id &&
    secondDisplay?.source.unitId === secondFft.id &&
    secondDisplay?.settings.channelList.join(",") === "1" &&
    secondDisplay?.settings.frequencyLimits.join(",") === "2000,12000" &&
    secondDisplay?.settings.frequencyLimits[1] ===
      decimator.settings.outputSampleRateHz / 2 &&
    secondDisplay?.settings.colourMap === "BLUE" &&
    secondDisplay?.settings.pixelsPerSlics === 4 &&
    levelTab?.owner.unitId === levelMeter.id &&
    levelTab?.owner.role === "levelMeter" &&
    levelDisplay?.owner.unitId === levelMeter.id &&
    levelDisplay?.source.unitId === levelMeter.id &&
    levelDisplay?.source.outputRole === "levels" &&
    !restored.project.controlledUnits.some((unit) =>
      unit.id === expected.removedWorkflow.fftId ||
      unit.id === expected.removedWorkflow.ownerId) &&
    !restored.project.displayTabs.flatMap((candidate) =>
      candidate.displays).some((candidate) =>
        expected.removedWorkflow.displayIds.includes(candidate.id)) &&
    !Object.hasOwn(display?.settings || {}, "sourceName") &&
    !Object.hasOwn(secondDisplay?.settings || {}, "sourceName"),
    "Service restart did not restore project settings/ownership/IDs",
    restored.project);
  await h.waitFor(
    () => h.unitNode(acquisition.id, acquisition.name),
    "restored Acquisition node");
  await h.waitFor(
    () => h.unitNode(soundOutput.id, soundOutput.name),
    "restored Sound Output node");
  await h.waitFor(
    () => h.unitNode(levelMeter.id, levelMeter.name),
    "restored Level Meter node");
  await h.waitFor(
    () => h.unitNode(fft.id, fft.name),
    "restored FFT node");
  await h.waitFor(
    () => h.unitNode(secondFft.id, secondFft.name),
    "restored second FFT node");
  await h.waitFor(
    () => h.unitNode(userDisplay.id, userDisplay.name),
    "restored User Display node");
  const ownedTab = await h.waitFor(
    () => h.ownedTab(userDisplay.id),
    "restored owned display tab");
  ownedTab.element.click();
  const displayElement = await h.waitFor(
    () => h.displayRoot(display.id),
    "restored Spectrogram display");
  h.assert(
    (displayElement.getAttribute(
      "data-owner-controlled-unit-id") ||
      displayElement.getAttribute("data-owner-unit-id")) ===
        userDisplay.id,
    "Restored display DOM lost owner identity");
  await h.waitFor(
    () => h.displayRoot(secondDisplay.id),
    "restored second Spectrogram display");
  h.assert(
    !expected.removedWorkflow.displayIds.some((displayId) =>
      document.querySelector(
        `[data-pamguard-display-instance-id="${CSS.escape(displayId)}"],` +
        `[data-project-display-id="${CSS.escape(displayId)}"],` +
        `[data-display-instance-id="${CSS.escape(displayId)}"],` +
        `[data-display-stream-status="${CSS.escape(displayId)}"]`)),
    "Service restart restored an orphan display or stream from the removed " +
      "workflow",
    expected.removedWorkflow);
  const restoredLevelTab = await h.waitFor(
    () => h.ownedTab(levelMeter.id),
    "restored Level Meter tab");
  restoredLevelTab.element.click();
  const restoredLevelDisplay = await h.waitFor(
    () => h.displayRoot(levelDisplay.id),
    "restored Level Meter display");
  h.assert(
    (restoredLevelDisplay.getAttribute(
      "data-owner-controlled-unit-id") ||
      restoredLevelDisplay.getAttribute("data-owner-unit-id")) ===
        levelMeter.id,
    "Restored Level Meter display lost owner identity");

  const staleEtag = restored.etag;
  const externalResponse = await fetch(
    "/v1/projects/active/mutations",
    {
      method: "POST",
      headers: {
        "Accept": "application/json",
        "Content-Type": "application/json",
        "If-Match": staleEtag
      },
      body: JSON.stringify({
        schemaVersion: 1,
        validateOnly: false,
        operations: [{
          op: "renameControlledUnit",
          unit: { id: acquisition.id },
          name: "External Acquisition Winner"
        }]
      })
    });
  h.assert(
    externalResponse.status === 200,
    "External winning project edit failed",
    { status: externalResponse.status });
  const externalResult = await externalResponse.json();
  const winningEtag = externalResult.active.etag;
  h.assert(
    winningEtag !== staleEtag,
    "External project edit did not advance ETag");

  const conflict = await h.renameUnitExpectConflict(
    fft,
    "Stale Browser Rename",
    staleEtag);
  const afterConflict = await h.active();
  h.assert(
    afterConflict.etag === winningEtag &&
    afterConflict.project.controlledUnits.find(
      (unit) => unit.id === acquisition.id).name ===
        "External Acquisition Winner" &&
    afterConflict.project.controlledUnits.find(
      (unit) => unit.id === fft.id).name === expected.fft.name,
    "Visible conflict did not preserve the winning project state",
    afterConflict);
  h.assertNoParallelWorkflow();
  return JSON.stringify({
    winningEtag,
    conflictMessage: conflict.message,
    conflictStatus: conflict.request.status,
    requestCount: h.requestHistory().length
  });
})()
'@

$phaseCTemplate = @'
(async () => {
  const expected = JSON.parse(atob("__EXPECTED_BASE64__"));
  const h = globalThis.__pamguardProjectShellSmoke;
  let snapshot = await h.waitActive(
    (candidate) =>
      candidate.project.projectId === expected.projectId &&
      candidate.project.controlledUnits.some(
        (unit) => unit.id === expected.userDisplay.id),
    "winning project after conflict reload");
  const userDisplay = snapshot.project.controlledUnits.find(
    (unit) => unit.id === expected.userDisplay.id);
  const fft = snapshot.project.controlledUnits.find(
    (unit) => unit.id === expected.fft.id);
  const secondFft = snapshot.project.controlledUnits.find(
    (unit) => unit.id === expected.secondFft.id);
  const acquisition = snapshot.project.controlledUnits.find(
    (unit) => unit.id === expected.acquisition.id);
  const soundOutput = snapshot.project.controlledUnits.find(
    (unit) => unit.id === expected.soundOutput.id);
  const levelMeter = snapshot.project.controlledUnits.find(
    (unit) => unit.id === expected.levelMeter.id);
  const amplifier = snapshot.project.controlledUnits.find(
    (unit) => unit.id === expected.amplifier.id);
  const patchPanel = snapshot.project.controlledUnits.find(
    (unit) => unit.id === expected.patchPanel.id);
  const filter = snapshot.project.controlledUnits.find(
    (unit) => unit.id === expected.filter.id);
  const decimator = snapshot.project.controlledUnits.find(
    (unit) => unit.id === expected.decimator.id);

  snapshot = await h.removeUnit(fft, true);
  const remainingTab = snapshot.project.displayTabs.find(
    (tab) => tab.owner.unitId === userDisplay.id);
  const unboundDisplay = remainingTab?.displays.find(
    (display) => display.id === expected.displayId);
  const stillBoundDisplay = remainingTab?.displays.find(
    (display) => display.id === expected.secondDisplayId);
  h.assert(
    unboundDisplay &&
    unboundDisplay.source === null &&
    stillBoundDisplay?.source.unitId === secondFft.id &&
    !snapshot.project.controlledUnits.some(
      (unit) => unit.id === fft.id),
    "Removing one FFT did not explicitly unbind only its Spectrogram",
    remainingTab);
  const ownedTab = await h.waitFor(
    () => h.ownedTab(userDisplay.id),
    "owned tab after FFT leave-unbound removal");
  ownedTab.element.click();
  const unboundRoot = await h.waitFor(
    () => h.displayRoot(expected.displayId),
    "explicitly unbound Spectrogram surface");
  const boundRoot = await h.waitFor(
    () => h.displayRoot(expected.secondDisplayId),
    "independent bound Spectrogram surface");
  h.assert(
    String(unboundRoot.querySelector("header small")?.textContent || "")
      .trim().toLowerCase() ===
      "unbound" &&
    /select an fft source/i.test(
      unboundRoot.querySelector(
        `[data-display-stream-status="${CSS.escape(
          expected.displayId)}"]`)?.textContent || "") &&
    String(boundRoot.querySelector("header small")?.textContent || "")
      .trim().toLowerCase()
      .includes(secondFft.name.trim().toLowerCase()),
    "Browser did not make the removed FFT binding explicitly unbound",
    {
      unbound: unboundRoot.textContent,
      bound: boundRoot.textContent
    });
  await h.startRuntime();
  await h.waitFor(() => {
    const status = h.displayRoot(expected.secondDisplayId)?.querySelector(
      `[data-display-stream-status="${CSS.escape(
        expected.secondDisplayId)}"]`);
    return String(status?.textContent || "").trim().toLowerCase() ===
      "live" ? status : null;
  }, "remaining Spectrogram stream after independent FFT removal");
  await h.injectAcquisitionAudio(acquisition.id);
  await h.assertLiveSpectrogram(expected.secondDisplayId);
  await h.stopRuntime();
  h.tabs().find((tab) => tab.kind === "data-model").element.click();

  snapshot = await h.removeUnit(userDisplay);
  h.assert(
    snapshot.project.displayTabs.every(
      (tab) => tab.owner.unitId !== userDisplay.id) &&
    !h.ownedTab(userDisplay.id) &&
    !h.displayRoot(expected.displayId),
    "Removing User Display left an orphan tab/Spectrogram",
    snapshot.project.displayTabs);
  snapshot = await h.removeUnit(secondFft);
  snapshot = await h.removeUnit(soundOutput);
  snapshot = await h.removeUnit(levelMeter);
  h.assert(
    snapshot.project.displayTabs.every(
      (tab) => tab.owner.unitId !== levelMeter.id) &&
    !h.ownedTab(levelMeter.id) &&
    !h.displayRoot(expected.levelDisplayId),
    "Removing Level Meter left its static display or owned tab",
    snapshot.project.displayTabs);
  snapshot = await h.removeUnit(decimator);
  snapshot = await h.removeUnit(filter);
  snapshot = await h.removeUnit(patchPanel);
  snapshot = await h.removeUnit(amplifier);
  snapshot = await h.removeUnit(acquisition);
  h.assert(
    snapshot.project.controlledUnits.length === 0 &&
    snapshot.project.displayTabs.length === 0,
    "First-slice controlled units did not remove cleanly",
    snapshot.project);
  const saved = await h.save();
  h.assertBlankShell(saved);
  h.assertNoParallelWorkflow();

  const restoredTemplate = await h.openSavedProject(
    expected.template.projectId);
  h.assert(
    JSON.stringify(restoredTemplate.project.controlledUnits) ===
      JSON.stringify(expected.template.controlledUnits) &&
    JSON.stringify(restoredTemplate.project.displayTabs) ===
      JSON.stringify(expected.template.displayTabs) &&
    JSON.stringify(restoredTemplate.project.dataModelLayout) ===
      JSON.stringify(expected.template.dataModelLayout),
    "Opening the template after service restart changed its units, " +
      "bindings, display ownership, settings, or graph layout",
    {
      expected: expected.template,
      actual: restoredTemplate.project
    });
  await h.waitFor(
    () => h.unitNode(
      expected.template.acquisitionId,
      expected.template.controlledUnits.find(
        (unit) => unit.id ===
          expected.template.acquisitionId).name),
    "restored template Acquisition node");
  const restoredUserTab = await h.waitFor(
    () => h.ownedTab(expected.template.userDisplayId),
    "restored template User Display tab");
  restoredUserTab.element.click();
  await h.waitFor(
    () => h.displayRoot(expected.template.spectrogramId),
    "restored template Spectrogram display");
  const restoredClickTab = await h.waitFor(
    () => h.ownedTab(expected.template.clickId),
    "restored template Click display tab");
  restoredClickTab.element.click();
  await h.waitFor(
    () => h.displayRoot(expected.template.clickDisplayId),
    "restored template Click display");
  const expectedMatchedTemplate =
    expected.template.controlledUnits.find(
      (unit) =>
        unit.id === expected.template.matchedTemplateId);
  const restoredMatchedTemplate =
    restoredTemplate.project.controlledUnits.find(
      (unit) =>
        unit.id === expected.template.matchedTemplateId);
  const restoredTemplateClick =
    restoredTemplate.project.controlledUnits.find(
      (unit) => unit.id === expected.template.clickId);
  const expectedClassifier =
    expectedMatchedTemplate?.settings?.classifiers?.[0];
  const restoredClassifier =
    restoredMatchedTemplate?.settings?.classifiers?.[0];
  const restoredMatchedTemplateBinding =
    restoredMatchedTemplate?.bindings?.find(
      (binding) => binding.inputRole === "clicks");
  h.assert(
    restoredMatchedTemplate &&
    restoredMatchedTemplate.name ===
      "Browser Matched Template" &&
    restoredTemplateClick &&
    restoredMatchedTemplateBinding?.sources?.length === 1 &&
    restoredMatchedTemplateBinding.sources[0].unitId ===
      restoredTemplateClick.id &&
    restoredMatchedTemplateBinding.sources[0].outputRole === "clicks" &&
    restoredClassifier?.matchTemplate?.name === "Beaked Whale" &&
    restoredClassifier.matchTemplate.sampleRateHz === 192000 &&
    restoredClassifier.matchTemplate.waveform.length === 192 &&
    restoredClassifier.rejectTemplate.name === "Dolphin" &&
    restoredClassifier.rejectTemplate.sampleRateHz === 192000 &&
    restoredClassifier.rejectTemplate.waveform.length === 192 &&
    JSON.stringify(restoredClassifier.matchTemplate.waveform) ===
      JSON.stringify(expectedClassifier.matchTemplate.waveform) &&
    JSON.stringify(restoredClassifier.rejectTemplate.waveform) ===
      JSON.stringify(expectedClassifier.rejectTemplate.waveform),
    "Restart/reload changed the Matched Template ID, Click Detector " +
      "binding, or exact default waveforms",
    {
      expected: expectedMatchedTemplate,
      restored: restoredMatchedTemplate
    });
  h.tabs().find((tab) => tab.kind === "data-model").element.click();
  await h.waitFor(
    () => h.unitNode(
      restoredMatchedTemplate.id,
      restoredMatchedTemplate.name),
    "restored Matched Template node");
  const restoredAcquisition = await h.acquisitionInventory(
    expected.template.acquisitionId);
  h.assert(
    restoredAcquisition?.configurationStatus ===
      "needsConfiguration" &&
    restoredAcquisition.hostBindingRevision === null,
    "Host input leaked into the portable saved template",
    restoredAcquisition);
  const reboundAcquisition = await h.configureAcquisitionHost(
    restoredTemplate.project.controlledUnits.find(
      (unit) =>
        unit.id === expected.template.acquisitionId),
    "https://fixture.invalid/restarted-template-audio");
  h.assert(
    reboundAcquisition.configurationStatus === "configured" &&
    reboundAcquisition.hostBindingRevision !== null,
    "Restored template Acquisition did not accept its new local host " +
      "binding",
    reboundAcquisition);
  await h.startRuntime();
  await h.injectAcquisitionAudio(
    expected.template.acquisitionId,
    true);
  const restartedMatchedTemplateEvidence =
    await h.assertMatchedTemplateAnnotations(
      restoredMatchedTemplate.id);
  h.assert(
    restartedMatchedTemplateEvidence.runtimeNodeId ===
      expected.template.matchedTemplateEvidence.runtimeNodeId &&
    restartedMatchedTemplateEvidence.clickType === 101,
    "Matched Template did not restart with its stable runtime identity " +
      "and annotations",
    {
      beforeRestart: expected.template.matchedTemplateEvidence,
      afterRestart: restartedMatchedTemplateEvidence
    });
  await h.stopRuntime();
  h.assertNoParallelWorkflow();

  const requests = h.requestHistory();
  const conflictRequests = requests.filter((request) =>
    request.status === 412 &&
    request.method === "POST" &&
    new URL(request.url, location.href).pathname ===
      "/v1/projects/active/mutations");
  const saveAsRequests = requests.filter((request) =>
    request.status === 201 &&
    new URL(request.url, location.href).pathname ===
      "/v1/projects/active/save-as");
  h.assert(
    conflictRequests.length === 1,
    "Expected exactly one stale-project 412",
    conflictRequests);
  h.assert(
    saveAsRequests.length === 2,
    "Expected exactly two successful Save As operations",
    saveAsRequests);
  return JSON.stringify({
    finalWorkingRevision: saved.workingRevision,
    finalSavedRevision: saved.savedRevision,
    finalEtag: saved.etag,
    requestCount: requests.length,
    projectRequests: requests.filter((request) =>
      new URL(request.url, location.href).pathname.startsWith(
        "/v1/projects")).length,
    conflictCount: conflictRequests.length,
    saveAsCount: saveAsRequests.length,
    restoredTemplateProjectId: restoredTemplate.project.projectId,
    restartedMatchedTemplateEvidence,
    tabs: h.tabs().map(({ element, ...tab }) => tab)
  });
})()
'@

try {
    Assert-TcpPortAvailable `
        -CandidatePort $Port `
        -Label "Service"
    Assert-TcpPortAvailable `
        -CandidatePort $DebugPort `
        -Label "Chromium debug"

    $env:PAMGUARD_CAPTURE_ENABLED = "0"
    $env:PAMGUARD_MODULE_GRAPH_FILE =
        Join-Path $testRoot "forbidden-low-level-graph.json"
    $env:PAMGUARD_PROJECT_DIR =
        [System.IO.Path]::GetFullPath($projectDirectory)
    $env:PAMGUARD_SESSION_CONFIG_DIR =
        [System.IO.Path]::GetFullPath($legacySessionDirectory)
    $env:PAMGUARD_WEB_ASSET_DIR = $webAssetDir
    $env:PAMGUARD_WEB_UI_FILE = $webUiFile
    $env:PAMGUARD_WORKSPACE_FILE =
        Join-Path $testRoot "forbidden-workspaces.json"

    $service = Start-SmokeService -Label "blank"
    $base = "http://127.0.0.1:$Port"
    $blank = Invoke-RestMethod `
        -Uri "$base/v1/projects/active" `
        -TimeoutSec 5
    if (@($blank.project.controlledUnits).Count -ne 0 -or
        @($blank.project.displayTabs).Count -ne 0) {
        throw (
            "Isolated project directory did not cold-boot blank: " +
            ($blank | ConvertTo-Json -Depth 12 -Compress))
    }

    $browser = Start-Process `
        -FilePath $browserExe `
        -ArgumentList @(
            "--headless=new",
            "--disable-background-networking",
            "--disable-component-update",
            "--disable-gpu",
            "--autoplay-policy=no-user-gesture-required",
            "--no-first-run",
            "--window-size=1800,1000",
            "--remote-debugging-port=$DebugPort",
            "--user-data-dir=$browserProfile",
            "about:blank"
        ) `
        -PassThru `
        -WindowStyle Hidden

    $page = $null
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($browser.HasExited) {
            throw (
                "Chromium exited before DevTools became ready with code " +
                $browser.ExitCode)
        }
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
    Invoke-Cdp `
        -Method "Page.addScriptToEvaluateOnNewDocument" `
        -Parameters @{ source = $bootstrapScript } |
        Out-Null
    Invoke-Cdp `
        -Method "Page.navigate" `
        -Parameters @{ url = "$base/" } |
        Out-Null
    Wait-BrowserDocument -Context "blank shell"

    $phaseA = (
        Invoke-BrowserExpression -Expression $phaseAExpression
    ) | ConvertFrom-Json
    $savedProjectFile = Join-Path `
        $projectDirectory `
        "$($phaseA.projectId).pamguard-project.json"
    if (-not (Test-Path -LiteralPath $savedProjectFile -PathType Leaf)) {
        throw "Browser Save As did not create $savedProjectFile"
    }
    $savedTemplateFile = Join-Path `
        $projectDirectory `
        "$($phaseA.template.projectId).pamguard-project.json"
    if (-not (Test-Path -LiteralPath $savedTemplateFile -PathType Leaf)) {
        throw "Template Save As did not create $savedTemplateFile"
    }

    Stop-SmokeService $service
    $service = $null
    $env:PAMGUARD_ACTIVE_PROJECT_ID = [string]$phaseA.projectId
    Assert-TcpPortAvailable `
        -CandidatePort $Port `
        -Label "Restarted service"
    $service = Start-SmokeService -Label "restored"
    Invoke-Cdp `
        -Method "Page.reload" `
        -Parameters @{ ignoreCache = $true } |
        Out-Null
    Wait-BrowserDocument -Context "restored project"

    $expectedJson = $phaseA | ConvertTo-Json -Depth 20 -Compress
    $expectedBase64 = [Convert]::ToBase64String(
        [System.Text.Encoding]::UTF8.GetBytes($expectedJson))
    $phaseBExpression = $phaseBTemplate.Replace(
        "__EXPECTED_BASE64__",
        $expectedBase64)
    $phaseB = (
        Invoke-BrowserExpression -Expression $phaseBExpression
    ) | ConvertFrom-Json

    Invoke-Cdp `
        -Method "Page.reload" `
        -Parameters @{ ignoreCache = $true } |
        Out-Null
    Wait-BrowserDocument -Context "conflict resolution reload"
    $phaseCExpression = $phaseCTemplate.Replace(
        "__EXPECTED_BASE64__",
        $expectedBase64)
    $phaseC = (
        Invoke-BrowserExpression -Expression $phaseCExpression
    ) | ConvertFrom-Json

    if ($ArtifactPath) {
        $artifactFullPath = if (
            [System.IO.Path]::IsPathRooted($ArtifactPath)) {
            [System.IO.Path]::GetFullPath($ArtifactPath)
        }
        else {
            [System.IO.Path]::GetFullPath(
                (Join-Path (Get-Location) $ArtifactPath))
        }
        $artifactParent = Split-Path -Parent $artifactFullPath
        if (-not (Test-Path -LiteralPath $artifactParent)) {
            New-Item `
                -ItemType Directory `
                -Path $artifactParent `
                -Force |
                Out-Null
        }
        $screenshot = Invoke-Cdp `
            -Method "Page.captureScreenshot" `
            -Parameters @{
                format = "png"
                captureBeyondViewport = $false
            }
        [System.IO.File]::WriteAllBytes(
            $artifactFullPath,
            [Convert]::FromBase64String(
                $screenshot.result.data))
    }

    Write-Host (
        "Project shell browser smoke passed: blank Data Model-only " +
        "startup; global Array Manager OK/Cancel; atomic Click monitoring " +
        "five-unit template plus a separately added Matched Template with " +
        "its dedicated editor, exact 192 kHz Beaked Whale/Dolphin defaults, " +
        "Click Detector binding, joined annotations, explicit host binding, " +
        "playback channels, live Spectrogram, continuous retained Clicks, " +
        "and browser audio frames; Matched Template restart/reload and " +
        "second-run annotation proof; " +
        "saved-template identity/layout restoration after service restart; " +
        "project-authoritative " +
        "Acquisition, stable-ID Sound Recorder transport with safe file " +
        "status, Sound Output, Signal Amplifier, Patch Panel, Filter, " +
        "Decimator, Noise Monitor, Noise Band Monitor, LTSA, Whistle and " +
        "Moan, three Ishmael detectors, MHT Click Train, two FFTs, User " +
        "Display, two independent Spectrograms, and a static live Level " +
        "Meter display; bidirectional graph/settings reconnect, provider " +
        "maximums, first-free PAMGuard names; durable " +
        "service restart; " +
        "visible stale-ETag conflict; selective source unbinding and " +
        "owner-cascade removal; no legacy " +
        "session/workspace/module-graph requests. Evidence: " +
        (@{
            projectId = $phaseA.projectId
            savedRevision = $phaseA.savedRevision
            conflictStatus = $phaseB.conflictStatus
            conflictMessage = $phaseB.conflictMessage
            finalSavedRevision = $phaseC.finalSavedRevision
            requests = $phaseC.requestCount
        } | ConvertTo-Json -Compress))
}
catch {
    $serviceLog = Get-ServiceLogText -Handle $service
    if ($serviceLog) {
        throw (
            $_.Exception.Message +
            "`nService log:`n" +
            $serviceLog)
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
        }
    }
    if ($browser -and -not $browser.HasExited) {
        Stop-Process -Id $browser.Id -Force -ErrorAction SilentlyContinue
        Wait-Process -Id $browser.Id -ErrorAction SilentlyContinue
    }
    Stop-SmokeService $service

    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $oldEnvironment[$name],
            "Process")
    }

    $resolvedRoot = [System.IO.Path]::GetFullPath($testRoot)
    if ($resolvedRoot.StartsWith(
            $tempBase,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        $resolvedRoot -ne $tempBase) {
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
