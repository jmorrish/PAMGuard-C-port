$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$RepoRoot = Resolve-Path (Join-Path $PortRoot "..")
$Maven = Join-Path $PortRoot "reference-tools\scripts\mvn-local.ps1"
$Output = Join-Path $PortRoot "cpp-engine\tests\fixtures\localisation\world-vector.csv"
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot" }
$Java = Join-Path $JavaHome "bin\java.exe"
$Javac = Join-Path $JavaHome "bin\javac.exe"
# The array shape/direction transcription is shared with ArrayShapeFixtureExporter
# and reached by reflection, so both sources compile together.
$JavaSrc = @(
    (Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\WorldVectorFixtureExporter.java"),
    (Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\ArrayShapeFixtureExporter.java")
)
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$TargetClasses = Join-Path $RepoRoot "target\classes"
$ClasspathFile = Join-Path $PortRoot "reference-tools\java\pamguard-classpath.txt"
$DependencyClasspath = if (Test-Path $ClasspathFile) { (Get-Content $ClasspathFile -Raw).Trim() } else { "" }
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

& $Javac -cp "$TargetClasses;$DependencyClasspath" -d $BuildDir $JavaSrc
if ($LASTEXITCODE -ne 0) {
    throw "javac failed with exit code $LASTEXITCODE"
}

& $Java -cp "$BuildDir;$TargetClasses;$DependencyClasspath" org.pamguard.port.reference.WorldVectorFixtureExporter $Output
if ($LASTEXITCODE -ne 0) {
    throw "java fixture exporter failed with exit code $LASTEXITCODE"
}

Get-Content $Output | Out-Host
