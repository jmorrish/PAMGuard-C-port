param(
    [Parameter(Mandatory = $true)]
    [int] $WindowType,

    [Parameter(Mandatory = $true)]
    [int] $Length,

    [Parameter(Mandatory = $true)]
    [string] $PamguardClasses,

    [Parameter(Mandatory = $true)]
    [string] $OutputPath,

    [string] $JavaHome = ""
)

$ErrorActionPreference = "Stop"

if ($JavaHome) {
    $env:JAVA_HOME = $JavaHome
}

$portRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$toolRoot = Split-Path -Parent $PSScriptRoot
$environment = & (Join-Path $PSScriptRoot "resolve-pamguard-oracle.ps1") `
    -PortRoot $portRoot -RequireClasses
$expectedClasses = (Resolve-Path $environment.TargetClasses).Path
$actualClasses = (Resolve-Path $PamguardClasses).Path
if ($actualClasses -ne $expectedClasses) {
    throw "Window fixture classes must come from the pinned oracle: $expectedClasses"
}

$javaSrc = Join-Path $toolRoot "java\src\org\pamguard\port\reference\WindowFixtureExporter.java"
$buildDir = $environment.BuildDir

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null

& $environment.Javac -cp $expectedClasses -d $buildDir $javaSrc
if ($LASTEXITCODE -ne 0) {
    throw "javac failed with exit code $LASTEXITCODE"
}
& $environment.Java -cp "$buildDir;$expectedClasses" `
    org.pamguard.port.reference.WindowFixtureExporter $WindowType $Length |
    Set-Content -Path $OutputPath -Encoding UTF8
if ($LASTEXITCODE -ne 0) {
    throw "java fixture exporter failed with exit code $LASTEXITCODE"
}
