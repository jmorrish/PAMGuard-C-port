$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$FixtureDir = Join-Path $PortRoot "cpp-engine\tests\fixtures\fft-noise"
$Output = Join-Path $FixtureDir "fft-noise.csv"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$Oracle = & (Join-Path $ScriptDir "resolve-pamguard-oracle.ps1") -PortRoot $PortRoot -RequireClasses -RequireClasspath
New-Item -ItemType Directory -Force -Path $BuildDir, $FixtureDir | Out-Null
$Classpath = "$($Oracle.TargetClasses);$($Oracle.DependencyClasspath)"
$Sources = @(
    (Join-Path $PortRoot "reference-tools\java\src\Acquisition\NoiseFixtureAcquisitionProcess.java"),
    (Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\FftNoiseFixtureExporter.java")
)
& $Oracle.Javac -cp $Classpath -d $BuildDir $Sources
if ($LASTEXITCODE -ne 0) { throw "javac failed with exit code $LASTEXITCODE" }
& $Oracle.Java -cp "$BuildDir;$Classpath" `
    org.pamguard.port.reference.FftNoiseFixtureExporter $Output
if ($LASTEXITCODE -ne 0) { throw "Java fixture exporter failed with exit code $LASTEXITCODE" }
