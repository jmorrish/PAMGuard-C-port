param(
    [int]$Port = 18197,
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

$testRoot = Join-Path (
    [System.IO.Path]::GetTempPath()
) (
    "pamguard-web-assets-" +
    [System.Guid]::NewGuid().ToString("N")
)
$webRoot = Join-Path $testRoot "web"
$fallbackAssets = Join-Path $webRoot "assets"
$nestedAssets = Join-Path $fallbackAssets "nested"
$explicitAssets = Join-Path $testRoot "explicit-assets"
$outside = Join-Path $testRoot "outside"
New-Item -ItemType Directory -Path (
    $webRoot,
    $fallbackAssets,
    $nestedAssets,
    $explicitAssets,
    $outside
) | Out-Null

$indexFile = Join-Path $webRoot "index.html"
[System.IO.File]::WriteAllText(
    $indexFile,
    "<!doctype html><title>asset-route-root-contract</title>",
    [System.Text.UTF8Encoding]::new($false)
)
[System.IO.File]::WriteAllText(
    (Join-Path $fallbackAssets "fallback.css"),
    "/* fallback-root */`nbody { color: rgb(1, 2, 3); }",
    [System.Text.UTF8Encoding]::new($false)
)
[System.IO.File]::WriteAllText(
    (Join-Path $nestedAssets "app.js"),
    "export const assetRouteContract = true;",
    [System.Text.UTF8Encoding]::new($false)
)
[System.IO.File]::WriteAllText(
    (Join-Path $fallbackAssets "unsupported.txt"),
    "This existing file type must not be served.",
    [System.Text.UTF8Encoding]::new($false)
)
[System.IO.File]::WriteAllText(
    (Join-Path $explicitAssets "explicit.css"),
    "/* explicit-root */",
    [System.Text.UTF8Encoding]::new($false)
)
[System.IO.File]::WriteAllText(
    (Join-Path $outside "secret.js"),
    "throw new Error('asset root escaped');",
    [System.Text.UTF8Encoding]::new($false)
)

$escapeJunction = Join-Path $fallbackAssets "escape"
try {
    New-Item `
        -ItemType Junction `
        -Path $escapeJunction `
        -Target $outside | Out-Null
}
catch {
    throw "Could not create the required junction-escape fixture: $($_.Exception.Message)"
}

$environmentNames = @(
    "PAMGUARD_API_KEY",
    "PAMGUARD_API_KEY_FILE",
    "PAMGUARD_CAPTURE_ENABLED",
    "PAMGUARD_MODULE_GRAPH_FILE",
    "PAMGUARD_OPENAPI_FILE",
    "PAMGUARD_RESULT_ARCHIVE_DIR",
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

function Invoke-RawHttpGet {
    param([Parameter(Mandatory = $true)][string]$Target)

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Connect("127.0.0.1", $Port)
        $stream = $client.GetStream()
        $requestText = (
            "GET $Target HTTP/1.1`r`n" +
            "Host: 127.0.0.1:$Port`r`n" +
            "Connection: close`r`n`r`n"
        )
        $requestBytes =
            [System.Text.Encoding]::ASCII.GetBytes($requestText)
        $stream.Write($requestBytes, 0, $requestBytes.Length)
        $stream.Flush()

        $responseBytes = [System.IO.MemoryStream]::new()
        $buffer = New-Object byte[] 8192
        while (($count = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $responseBytes.Write($buffer, 0, $count)
        }
        $responseText = [System.Text.Encoding]::UTF8.GetString(
            $responseBytes.ToArray()
        )
        $headerEnd = $responseText.IndexOf("`r`n`r`n")
        if ($headerEnd -lt 0) {
            throw "Malformed HTTP response for $Target"
        }
        $headerLines = $responseText.Substring(0, $headerEnd).Split(
            @("`r`n"),
            [System.StringSplitOptions]::None
        )
        if ($headerLines[0] -notmatch '^HTTP/\d\.\d\s+(\d{3})') {
            throw "Malformed HTTP status for $Target`: $($headerLines[0])"
        }
        $headers = @{}
        foreach ($line in $headerLines | Select-Object -Skip 1) {
            $separator = $line.IndexOf(":")
            if ($separator -gt 0) {
                $headers[$line.Substring(0, $separator).Trim()] =
                    $line.Substring($separator + 1).Trim()
            }
        }
        return [pscustomobject]@{
            Status = [int]$Matches[1]
            Headers = $headers
            Body = $responseText.Substring($headerEnd + 4)
        }
    }
    finally {
        $client.Dispose()
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
            "$Context returned HTTP $($Response.Status), " +
            "expected $Expected. Body: $($Response.Body)"
        )
    }
}

function Start-TestService {
    param([Parameter(Mandatory = $true)][string]$Label)

    $stdout = Join-Path $testRoot "$Label.stdout.log"
    $stderr = Join-Path $testRoot "$Label.stderr.log"
    $started = Start-Process `
        -FilePath $serviceExe `
        -ArgumentList "$Port" `
        -PassThru `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr

    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($started.HasExited) {
            $message = if (Test-Path -LiteralPath $stderr) {
                [System.IO.File]::ReadAllText($stderr)
            }
            else {
                ""
            }
            throw (
                "Service exited during $Label startup with code " +
                "$($started.ExitCode): $message"
            )
        }
        try {
            $health = Invoke-RawHttpGet -Target "/health"
            if ($health.Status -eq 200) {
                return $started
            }
        }
        catch {
            Start-Sleep -Milliseconds 100
        }
    }
    Stop-Process -Id $started.Id -Force -ErrorAction SilentlyContinue
    throw "Service did not become healthy for $Label"
}

function Stop-TestService {
    param($Process)
    if ($null -ne $Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
        Wait-Process -Id $Process.Id -ErrorAction SilentlyContinue
    }
}

$service = $null
try {
    # An explicitly configured asset root is authoritative and is exposed only
    # below /assets; files beside it and files in the fallback root are not.
    $env:PAMGUARD_WEB_UI_FILE = $indexFile
    $env:PAMGUARD_WEB_ASSET_DIR = $explicitAssets
    $service = Start-TestService -Label "explicit"

    $explicit = Invoke-RawHttpGet -Target "/assets/explicit.css"
    Assert-Status $explicit 200 "Explicit CSS asset"
    if ($explicit.Body -notmatch "explicit-root") {
        throw "Explicit asset response did not contain the configured file"
    }
    if ($explicit.Headers["Content-Type"] -ne "text/css; charset=utf-8") {
        throw "Explicit CSS asset had the wrong MIME type"
    }
    Assert-Status (
        Invoke-RawHttpGet -Target "/assets/fallback.css"
    ) 404 "Fallback file while explicit root is configured"
    Assert-Status (
        Invoke-RawHttpGet -Target "/outside/secret.js"
    ) 404 "Arbitrary URL path"
    Stop-TestService $service
    $service = $null

    # With no explicit root, a validated assets directory beside index.html is
    # used. Exercise text/binary-safe serving and every confinement boundary.
    Remove-Item Env:\PAMGUARD_WEB_ASSET_DIR -ErrorAction SilentlyContinue
    $service = Start-TestService -Label "fallback"

    $health = Invoke-RawHttpGet -Target "/health"
    Assert-Status $health 200 "Health"
    if (($health.Body | ConvertFrom-Json).webAssetsEnabled -ne $true) {
        throw "Health did not report the fallback asset route enabled"
    }

    foreach ($rootTarget in @("/", "/index.html")) {
        $rootResponse = Invoke-RawHttpGet -Target $rootTarget
        Assert-Status $rootResponse 200 "Preserved $rootTarget route"
        if ($rootResponse.Body -notmatch "asset-route-root-contract") {
            throw "$rootTarget no longer serves PAMGUARD_WEB_UI_FILE"
        }
        if ($rootResponse.Headers["Content-Type"] -ne
            "text/html; charset=utf-8") {
            throw "$rootTarget had the wrong HTML MIME type"
        }
    }

    $css = Invoke-RawHttpGet -Target "/assets/fallback.css"
    Assert-Status $css 200 "Fallback CSS asset"
    if ($css.Headers["Content-Type"] -ne "text/css; charset=utf-8" -or
        $css.Headers["X-Content-Type-Options"] -ne "nosniff" -or
        $css.Body -notmatch "fallback-root") {
        throw "Fallback CSS content, MIME, or nosniff header was incorrect"
    }

    $javascript = Invoke-RawHttpGet -Target "/assets/nested/app.js"
    Assert-Status $javascript 200 "Nested JavaScript asset"
    if ($javascript.Headers["Content-Type"] -ne
        "application/javascript; charset=utf-8" -or
        $javascript.Body -notmatch "assetRouteContract") {
        throw "JavaScript asset content or MIME type was incorrect"
    }

    Assert-Status (
        Invoke-RawHttpGet -Target "/assets/missing.js"
    ) 404 "Missing asset"
    Assert-Status (
        Invoke-RawHttpGet -Target "/assets/unsupported.txt"
    ) 415 "Unsupported asset type"
    Assert-Status (
        Invoke-RawHttpGet -Target "/assets/../outside/secret.js"
    ) 403 "Plain traversal"
    Assert-Status (
        Invoke-RawHttpGet -Target "/assets/%2e%2e/outside/secret.js"
    ) 403 "Encoded traversal"
    Assert-Status (
        Invoke-RawHttpGet -Target "/assets/%2e%2e%5coutside%5csecret.js"
    ) 403 "Encoded Windows traversal"
    Assert-Status (
        Invoke-RawHttpGet -Target "/assets/escape/secret.js"
    ) 403 "Junction escape"

    Stop-TestService $service
    $service = $null

    # Explicit configuration is validated eagerly; a regular file cannot be
    # promoted to a directory root.
    $env:PAMGUARD_WEB_ASSET_DIR = $indexFile
    $invalidStdout = Join-Path $testRoot "invalid.stdout.log"
    $invalidStderr = Join-Path $testRoot "invalid.stderr.log"
    $invalid = Start-Process `
        -FilePath $serviceExe `
        -ArgumentList "$Port" `
        -PassThru `
        -WindowStyle Hidden `
        -RedirectStandardOutput $invalidStdout `
        -RedirectStandardError $invalidStderr
    if (-not $invalid.WaitForExit(5000)) {
        Stop-Process -Id $invalid.Id -Force
        throw "Service accepted a non-directory PAMGUARD_WEB_ASSET_DIR"
    }
    if ($invalid.ExitCode -eq 0) {
        throw "Invalid explicit asset root did not fail service startup"
    }

    Write-Host (
        "Web asset service smoke passed: explicit/fallback roots, CSS/JS " +
        "MIME, preserved index routes, traversal, unsupported/missing files, " +
        "and junction confinement."
    )
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

    if (Test-Path -LiteralPath $escapeJunction) {
        $junctionItem = Get-Item -LiteralPath $escapeJunction -Force
        $expectedJunction = [System.IO.Path]::GetFullPath(
            $escapeJunction
        )
        if ($junctionItem.LinkType -ne "Junction" -or
            $junctionItem.FullName -ne $expectedJunction) {
            throw "Refusing to remove unexpected junction fixture"
        }
        # Windows PowerShell 5's Remove-Item can throw a NullReferenceException
        # for a valid junction. DirectoryInfo.Delete removes the link itself,
        # non-recursively; the target remains intact.
        $junctionItem.Delete()
        if (-not (Test-Path -LiteralPath (
            Join-Path $outside "secret.js"
        ))) {
            throw "Junction cleanup unexpectedly removed its target"
        }
    }
    $tempRoot = [System.IO.Path]::GetFullPath(
        [System.IO.Path]::GetTempPath()
    ).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar
    )
    $resolvedTestRoot = [System.IO.Path]::GetFullPath($testRoot)
    $expectedPrefix =
        $tempRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolvedTestRoot.StartsWith(
        $expectedPrefix,
        [System.StringComparison]::OrdinalIgnoreCase
    ) -or
        [System.IO.Path]::GetFileName($resolvedTestRoot) -notlike
        "pamguard-web-assets-*") {
        throw "Refusing to remove unexpected test path: $resolvedTestRoot"
    }
    if (Test-Path -LiteralPath $resolvedTestRoot) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
