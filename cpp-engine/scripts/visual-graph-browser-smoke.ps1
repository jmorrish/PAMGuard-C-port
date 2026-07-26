param(
    [int]$Port = 18195,
    [int]$DebugPort = 19227,
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build"),
    [string]$ArtifactPath = ""
)

$ErrorActionPreference = "Stop"
$serviceExe = Join-Path $BuildDir "pamguard_engine_service.exe"
$browserExe =
    "C:\Program Files (x86)\Google\Chrome\Application\chrome.exe"
if (-not (Test-Path -LiteralPath $serviceExe)) {
    throw "Service executable not found: $serviceExe"
}
if (-not (Test-Path -LiteralPath $browserExe)) {
    throw "Google Chrome not found: $browserExe"
}

$tempBase = [System.IO.Path]::GetTempPath()
$testRoot = Join-Path $tempBase (
    "pamguard-visual-graph-" +
    [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$profile = Join-Path $testRoot "chrome-profile"
New-Item -ItemType Directory -Path $profile | Out-Null

$oldGraph = $env:PAMGUARD_MODULE_GRAPH_FILE
$oldLegacyModelCompat = $env:PAMGUARD_LEGACY_MODEL_COMPAT
$oldUi = $env:PAMGUARD_WEB_UI_FILE
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
            $stream.Write($buffer, 0, $received.Count)
        } while (-not $received.EndOfMessage)
        $message = (
            [System.Text.Encoding]::UTF8.GetString(
                $stream.ToArray()
            ) | ConvertFrom-Json
        )
        if ($message.id -eq $requestId) {
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
            $(if ($description) { ": $description" } else { "" })
        )
    }
    return $response.result.result.value
}

try {
    $env:PAMGUARD_MODULE_GRAPH_FILE =
        Join-Path $testRoot "module-graph.json"
    $env:PAMGUARD_LEGACY_MODEL_COMPAT = "1"
    $env:PAMGUARD_WEB_UI_FILE = (
        Resolve-Path (
            Join-Path $PSScriptRoot "..\..\web-ui\legacy-compat.html")
    ).Path
    $service = Start-Process `
        -FilePath $serviceExe `
        -ArgumentList "$Port" `
        -PassThru `
        -WindowStyle Hidden
    $base = "http://127.0.0.1:$Port"
    $healthy = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
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
        throw "Visual graph browser service did not become healthy"
    }

    $initialGraph = @{
        expectedRevision = 0
        schemaVersion = 1
        revision = 0
        modules = @(
            @{
                id = "source"
                typeId = "pamguard.acquisition"
                name = "Hydrophone input"
                enabled = $true
                settings = @{
                    sourceId = "browser-smoke"
                    sampleRateHz = 48000
                    channelCount = 2
                    subtractDC = $false
                    dcTimeConstantSeconds = 1
                    calibrationDbOffsetByChannel = @()
                }
            }
        )
        connections = @()
    } | ConvertTo-Json -Depth 12 -Compress
    Invoke-RestMethod `
        -Method Put `
        -Uri "$base/module-graph" `
        -ContentType "application/json" `
        -Body $initialGraph |
        Out-Null

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
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
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
        throw "Chrome DevTools page did not become available"
    }

    $socket = [System.Net.WebSockets.ClientWebSocket]::new()
    [void]$socket.ConnectAsync(
        [Uri]$page.webSocketDebuggerUrl,
        [Threading.CancellationToken]::None
    ).GetAwaiter().GetResult()
    Invoke-Cdp -Method "Page.enable" | Out-Null
    Invoke-Cdp -Method "Runtime.enable" | Out-Null
    Start-Sleep -Milliseconds 1200

    $initial = (
        Invoke-BrowserExpression -Expression @'
JSON.stringify({
  active: document.querySelector("#tab-graph").classList.contains("active"),
  palette: document.querySelectorAll(".graph-palette-item").length,
  nodes: document.querySelectorAll(".graph-node").length,
  graphModules: __pamguardTest.graph.modules.length
})
'@
    ) | ConvertFrom-Json
    if (-not $initial.active -or
        $initial.palette -lt 20 -or
        $initial.nodes -ne 1 -or
        $initial.graphModules -ne 1) {
        throw (
            "Visual graph did not render its palette/source node: " +
            ($initial | ConvertTo-Json -Compress)
        )
    }

    $connected = (
        Invoke-BrowserExpression -Expression @'
(async () => {
  __pamguardTest.addGraphModuleAt(
    "pamguard.fft", { x: 560, y: 120 });
  const fft = __pamguardTest.graph.modules.find(
    (module) => module.typeId === "pamguard.fft");
  const source = document.querySelector(
    '.graph-port.output[data-module-id="source"][data-port-id="audio"]');
  const target = document.querySelector(
    `.graph-port.input[data-module-id="${fft.id}"][data-port-id="input"]`);
  const sourceRect = source.getBoundingClientRect();
  const targetRect = target.getBoundingClientRect();
  source.dispatchEvent(new PointerEvent("pointerdown", {
    bubbles: true,
    button: 0,
    clientX: sourceRect.left + sourceRect.width / 2,
    clientY: sourceRect.top + sourceRect.height / 2
  }));
  window.dispatchEvent(new PointerEvent("pointerup", {
    bubbles: true,
    button: 0,
    clientX: targetRect.left + targetRect.width / 2,
    clientY: targetRect.top + targetRect.height / 2
  }));
  await new Promise((resolve) => requestAnimationFrame(
    () => requestAnimationFrame(resolve)));
  return JSON.stringify({
    fftId: fft.id,
    modules: __pamguardTest.graph.modules.length,
    connections: __pamguardTest.graph.connections.length,
    wires: document.querySelectorAll(
      ".graph-connection:not(.hit):not(.preview)").length,
    dirty: __pamguardTest.graphDirty
  });
})()
'@
    ) | ConvertFrom-Json
    if ($connected.modules -ne 2 -or
        $connected.connections -ne 1 -or
        $connected.wires -ne 1 -or
        -not $connected.dirty) {
        throw (
            "Typed drag connection did not update the draft: " +
            ($connected | ConvertTo-Json -Compress)
        )
    }

    $settings = (
        Invoke-BrowserExpression -Expression @'
(() => {
  const fft = __pamguardTest.graph.modules.find(
    (module) => module.typeId === "pamguard.fft");
  const node = document.querySelector(
    `.graph-node[data-module-id="${fft.id}"]`);
  const nodeRect = node.getBoundingClientRect();
  node.dispatchEvent(new MouseEvent("contextmenu", {
    bubbles: true,
    cancelable: true,
    clientX: nodeRect.left + 30,
    clientY: nodeRect.top + 30
  }));
  const menuOpen = document.querySelector(
    "#graphContextMenu").classList.contains("open");
  document.querySelector(
    '#graphContextMenu [data-graph-action="configure"]').click();
  return JSON.stringify({
    menuOpen,
    open: document.querySelector("#graphSettingsDialog").open,
    sections: Array.from(document.querySelectorAll(
      "#graphSettingsNav button")).map((button) => button.textContent),
    fields: document.querySelectorAll(
      "#graphSettingsContent [data-setting-path]").length,
    rawJsonVisible: Boolean(document.querySelector("#graphAdvancedJson"))
  });
})()
'@
    ) | ConvertFrom-Json
    if (-not $settings.menuOpen -or
        -not $settings.open -or
        $settings.sections -notcontains "FFT" -or
        $settings.sections -notcontains "Click removal" -or
        $settings.fields -lt 4 -or
        $settings.rawJsonVisible) {
        throw (
            "Guided FFT settings did not render: " +
            ($settings | ConvertTo-Json -Compress)
        )
    }

    $savedSettings = (
        Invoke-BrowserExpression -Expression @'
(() => {
  const length = Array.from(document.querySelectorAll(
    "#graphSettingsContent [data-setting-path]")).find(
      (control) => control.dataset.settingPath === '["fftLength"]');
  length.value = "2048";
  document.querySelector("#graphSettingsSave").click();
  return __pamguardTest.graph.modules.find(
    (module) => module.typeId === "pamguard.fft").settings.fftLength;
})()
'@
    )
    if ($savedSettings -ne 2048) {
        throw "Guided settings did not save FFT length to the draft"
    }

    $valid = Invoke-BrowserExpression `
        -Expression "__pamguardTest.validateGraph()"
    if (-not $valid) {
        throw "Browser visual graph draft did not pass server validation"
    }
    Invoke-BrowserExpression `
        -Expression "__pamguardTest.applyGraph()" |
        Out-Null
    Start-Sleep -Milliseconds 200
    $applied = Invoke-RestMethod -Uri "$base/module-graph"
    if (@($applied.modules).Count -ne 2 -or
        @($applied.connections).Count -ne 1 -or
        $applied.revision -ne 2) {
        throw "Visual graph Apply did not persist the validated graph"
    }

    $layoutBeforeReload = (
        Invoke-BrowserExpression -Expression @'
(() => {
  const fft = __pamguardTest.graph.modules.find(
    (module) => module.typeId === "pamguard.fft");
  const head = document.querySelector(
    `.graph-node[data-module-id="${fft.id}"] .graph-node-head`);
  const rect = head.getBoundingClientRect();
  head.dispatchEvent(new PointerEvent("pointerdown", {
    bubbles: true,
    button: 0,
    clientX: rect.left + 30,
    clientY: rect.top + 20
  }));
  window.dispatchEvent(new PointerEvent("pointermove", {
    bubbles: true,
    button: 0,
    clientX: rect.left + 150,
    clientY: rect.top + 90
  }));
  window.dispatchEvent(new PointerEvent("pointerup", {
    bubbles: true,
    button: 0,
    clientX: rect.left + 150,
    clientY: rect.top + 90
  }));
  __pamguardTest.setGraphViewport(
    { x: 45, y: 35, zoom: 0.85 });
  __pamguardTest.saveGraphLayout();
  const layout = __pamguardTest.graphLayout;
  return JSON.stringify({
    id: fft.id,
    x: layout.positions[fft.id].x,
    y: layout.positions[fft.id].y
  });
})()
'@
    ) | ConvertFrom-Json
    Invoke-Cdp -Method "Page.reload" -Parameters @{
        ignoreCache = $true
    } | Out-Null
    Start-Sleep -Milliseconds 1200
    $restored = (
        Invoke-BrowserExpression -Expression @"
JSON.stringify({
  x: __pamguardTest.graphLayout.positions["$($layoutBeforeReload.id)"].x,
  y: __pamguardTest.graphLayout.positions["$($layoutBeforeReload.id)"].y,
  zoom: __pamguardTest.graphLayout.viewport.zoom,
  nodes: document.querySelectorAll(".graph-node").length,
  wires: document.querySelectorAll(
    ".graph-connection:not(.hit):not(.preview)").length
})
"@
    ) | ConvertFrom-Json
    if ([Math]::Abs(
            $restored.x - $layoutBeforeReload.x) -gt 0.001 -or
        [Math]::Abs(
            $restored.y - $layoutBeforeReload.y) -gt 0.001 -or
        [Math]::Abs($restored.zoom - 0.85) -gt 0.0001 -or
        $restored.nodes -ne 2 -or
        $restored.wires -ne 1) {
        throw (
            "Visual graph layout did not survive browser reload: " +
            ($restored | ConvertTo-Json -Compress)
        )
    }

    if ($ArtifactPath) {
        $artifactFullPath = [System.IO.Path]::GetFullPath(
            (Join-Path (Get-Location) $ArtifactPath))
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
        "Visual graph browser smoke passed: categorized palette, " +
        "node canvas, typed drag connection, guided FFT settings, " +
        "transactional Apply, and reload-persisted layout")
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
