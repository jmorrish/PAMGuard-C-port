$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$OutputDir = Join-Path $PortRoot "cpp-engine\tests\fixtures\localisation"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\DelayGroupFixtureExporter.java"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"

$OracleEnvironment = & (Join-Path $ScriptDir "resolve-pamguard-oracle.ps1") -PortRoot $PortRoot -RequireClasses -RequireClasspath
$RepoRoot = $OracleEnvironment.JavaRepo
$JavaHome = $OracleEnvironment.JavaHome
$Java = $OracleEnvironment.Java
$Javac = $OracleEnvironment.Javac
$Maven = Join-Path $ScriptDir "mvn-local.ps1"
$Mvn = $OracleEnvironment.Maven
$TargetClasses = $OracleEnvironment.TargetClasses
$ClasspathFile = $OracleEnvironment.ClasspathFile
$DependencyClasspath = $OracleEnvironment.DependencyClasspath
if (-not (Test-Path $Java)) {
    throw "java.exe was not found at $Java"
}
if (-not (Test-Path $Javac)) {
    throw "javac.exe was not found at $Javac"
}
$env:JAVA_HOME = $JavaHome

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
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Push-Location $RepoRoot
try {
    & $Mvn -q dependency:build-classpath "-Dmdep.outputFile=$ClasspathFile"
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

foreach ($Mode in @("raw", "restricted", "upsample", "filter", "envelope", "leading", "combined")) {
    $Suffix = if ($Mode -eq "raw") { "basic" } else { $Mode }
    $Output = Join-Path $OutputDir "delay-group-3ch-$Suffix.csv"
    & $Java -cp "$BuildDir;$FullClasspath" org.pamguard.port.reference.DelayGroupFixtureExporter `
        48000 `
        64 `
        16 `
        $Output `
        $Mode
    if ($LASTEXITCODE -ne 0) {
        throw "java fixture exporter failed for $Mode with exit code $LASTEXITCODE"
    }
}
