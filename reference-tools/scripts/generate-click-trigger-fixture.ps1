$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$RepoRoot = Resolve-Path (Join-Path $PortRoot "..")
$Maven = Join-Path $PortRoot "reference-tools\scripts\mvn-local.ps1"
$Output = Join-Path $PortRoot "cpp-engine\tests\fixtures\click-trigger\basic-2ch-threshold10.csv"
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot" }
$Java = Join-Path $JavaHome "bin\java.exe"
$Javac = Join-Path $JavaHome "bin\javac.exe"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\ClickTriggerFixtureExporter.java"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$TargetClasses = Join-Path $RepoRoot "target\classes"
if (-not (Test-Path $Java)) {
    throw "java.exe was not found at $Java"
}
if (-not (Test-Path $Javac)) {
    throw "javac.exe was not found at $Javac"
}

if (-not (Test-Path $TargetClasses)) {
    Push-Location $RepoRoot
    try {
        & $Maven -DskipTests compile | Out-Host
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
    finally {
        Pop-Location
    }
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

& $Javac -cp $TargetClasses -d $BuildDir $JavaSrc
if ($LASTEXITCODE -ne 0) {
    throw "javac failed with exit code $LASTEXITCODE"
}

& $Java -cp "$BuildDir;$TargetClasses" org.pamguard.port.reference.ClickTriggerFixtureExporter `
    0x3 `
    0x3 `
    10.0 `
    0.1 `
    0.00001 `
    10 `
    12 `
    8 `
    128 `
    1 `
    48000 `
    256 `
    $Output
if ($LASTEXITCODE -ne 0) {
    throw "java fixture exporter failed with exit code $LASTEXITCODE"
}
