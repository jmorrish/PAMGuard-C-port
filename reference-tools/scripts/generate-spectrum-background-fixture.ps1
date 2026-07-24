$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$FixtureDir = Join-Path $PortRoot "cpp-engine\tests\fixtures\spectrum-background"
$Output = Join-Path $FixtureDir "spectrum-background.csv"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$Oracle = & (Join-Path $ScriptDir "resolve-pamguard-oracle.ps1") `
    -PortRoot $PortRoot -RequireClasses -RequireClasspath
New-Item -ItemType Directory -Force -Path $BuildDir, $FixtureDir | Out-Null
$Classpath = "$($Oracle.TargetClasses);$($Oracle.DependencyClasspath)"
$Source = Join-Path $PortRoot `
    "reference-tools\java\src\org\pamguard\port\reference\SpectrumBackgroundFixtureExporter.java"
& $Oracle.Javac -cp $Classpath -d $BuildDir $Source
if ($LASTEXITCODE -ne 0) { throw "javac failed with exit code $LASTEXITCODE" }
& $Oracle.Java -cp "$BuildDir;$Classpath" `
    org.pamguard.port.reference.SpectrumBackgroundFixtureExporter $Output
if ($LASTEXITCODE -ne 0) {
    throw "Java fixture exporter failed with exit code $LASTEXITCODE"
}
