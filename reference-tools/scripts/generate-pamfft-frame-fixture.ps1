param(
    [Parameter(Mandatory = $true)]
    [int] $WindowType,

    [Parameter(Mandatory = $true)]
    [int] $FftLength,

    [Parameter(Mandatory = $true)]
    [int] $FftHop,

    [Parameter(Mandatory = $true)]
    [int] $SampleRate,

    [Parameter(Mandatory = $true)]
    [int] $ChunkLength,

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
    -PortRoot $portRoot -RequireClasses -RequireClasspath
$javaSrc = Join-Path $toolRoot "java\src\org\pamguard\port\reference\PamFftFrameFixtureExporter.java"
$buildDir = $environment.BuildDir
$fullClasspath = "$($environment.TargetClasses);$($environment.DependencyClasspath)"

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null

& $environment.Javac -cp $fullClasspath -d $buildDir $javaSrc
if ($LASTEXITCODE -ne 0) {
    throw "javac failed with exit code $LASTEXITCODE"
}
& $environment.Java -cp "$buildDir;$fullClasspath" `
    org.pamguard.port.reference.PamFftFrameFixtureExporter `
    $WindowType $FftLength $FftHop $SampleRate $ChunkLength |
    Set-Content -Path $OutputPath -Encoding UTF8
if ($LASTEXITCODE -ne 0) {
    throw "java fixture exporter failed with exit code $LASTEXITCODE"
}
