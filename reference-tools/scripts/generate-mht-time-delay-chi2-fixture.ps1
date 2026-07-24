$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$Output = Join-Path $PortRoot "cpp-engine\tests\fixtures\click-train\mht-time-delay-chi2.csv"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\MhtTimeDelayChi2FixtureExporter.java"
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

& $Java -cp "$BuildDir;$TargetClasses;$DependencyClasspath" org.pamguard.port.reference.MhtTimeDelayChi2FixtureExporter $Output
if ($LASTEXITCODE -ne 0) {
    throw "java fixture exporter failed with exit code $LASTEXITCODE"
}

Get-Content $Output | Out-Host
