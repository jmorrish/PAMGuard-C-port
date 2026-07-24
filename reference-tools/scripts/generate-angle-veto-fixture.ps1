$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$FixtureDir = Join-Path $PortRoot "cpp-engine\tests\fixtures\click-angle-veto"
$Output = Join-Path $FixtureDir "angle-veto.csv"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\AngleVetoFixtureExporter.java"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$Oracle = & (Join-Path $ScriptDir "resolve-pamguard-oracle.ps1") -PortRoot $PortRoot -RequireClasses -RequireClasspath

New-Item -ItemType Directory -Force -Path $BuildDir, $FixtureDir | Out-Null
$FullClasspath = "$($Oracle.TargetClasses);$($Oracle.DependencyClasspath)"
& $Oracle.Javac -cp $FullClasspath -d $BuildDir $JavaSrc
if ($LASTEXITCODE -ne 0) { throw "javac failed with exit code $LASTEXITCODE" }
& $Oracle.Java -cp "$BuildDir;$FullClasspath" `
    org.pamguard.port.reference.AngleVetoFixtureExporter $Output
if ($LASTEXITCODE -ne 0) { throw "Java fixture exporter failed with exit code $LASTEXITCODE" }
