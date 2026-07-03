$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$RepoRoot = Resolve-Path (Join-Path $PortRoot "..")
$Maven = Join-Path $PortRoot "reference-tools\scripts\mvn-local.ps1"
$Output = Join-Path $PortRoot "cpp-engine\tests\fixtures\click-feature\basic-features.csv"
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot" }
$Java = Join-Path $JavaHome "bin\java.exe"
$Javac = Join-Path $JavaHome "bin\javac.exe"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\ClickFeatureFixtureExporter.java"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$ClasspathFile = Join-Path $PortRoot "reference-tools\java\pamguard-classpath.txt"
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

$env:JAVA_HOME = $JavaHome
Push-Location $RepoRoot
try {
    & (Join-Path $RepoRoot "tools\apache-maven-3.9.16\bin\mvn.cmd") -q dependency:build-classpath "-Dmdep.outputFile=$ClasspathFile"
    if ($LASTEXITCODE -ne 0) {
        throw "Maven dependency classpath generation failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

$DependencyClasspath = Get-Content -Path $ClasspathFile -Raw
$FullClasspath = "$TargetClasses;$DependencyClasspath"

& $Javac -cp $FullClasspath -d $BuildDir $JavaSrc
if ($LASTEXITCODE -ne 0) {
    throw "javac failed with exit code $LASTEXITCODE"
}

& $Java -cp "$BuildDir;$FullClasspath" org.pamguard.port.reference.ClickFeatureFixtureExporter $Output
if ($LASTEXITCODE -ne 0) {
    throw "java fixture exporter failed with exit code $LASTEXITCODE"
}
