$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$Output = Join-Path $PortRoot "cpp-engine\tests\fixtures\whistle\connected-region-basic.csv"
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot" }
$Java = Join-Path $JavaHome "bin\java.exe"
$Javac = Join-Path $JavaHome "bin\javac.exe"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\ConnectedRegionFixtureExporter.java"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"

if (-not (Test-Path $Java)) {
    throw "java.exe was not found at $Java"
}
if (-not (Test-Path $Javac)) {
    throw "javac.exe was not found at $Javac"
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

& $Javac -d $BuildDir $JavaSrc
if ($LASTEXITCODE -ne 0) {
    throw "javac failed with exit code $LASTEXITCODE"
}

& $Java -cp $BuildDir org.pamguard.port.reference.ConnectedRegionFixtureExporter $Output
if ($LASTEXITCODE -ne 0) {
    throw "java fixture exporter failed with exit code $LASTEXITCODE"
}
