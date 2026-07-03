param(
    [Parameter(Mandatory = $true)]
    [int] $WindowType,

    [Parameter(Mandatory = $true)]
    [int] $FftLength,

    [Parameter(Mandatory = $true)]
    [string] $OutputPath,

    [string] $JavaHome = "C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$toolRoot = Split-Path -Parent $PSScriptRoot
$javaSrc = Join-Path $toolRoot "java\src\org\pamguard\port\reference\FftFixtureExporter.java"
$buildDir = Join-Path $toolRoot "java\build"
$classpathFile = Join-Path $toolRoot "java\pamguard-classpath.txt"
$targetClasses = Join-Path $repoRoot "target\classes"
$mvn = Join-Path $repoRoot "tools\apache-maven-3.9.16\bin\mvn.cmd"
$javac = Join-Path $JavaHome "bin\javac.exe"
$java = Join-Path $JavaHome "bin\java.exe"

if (!(Test-Path $targetClasses)) {
    throw "PAMGuard target classes not found. Run: .\pamguard-enterprise-port\reference-tools\scripts\mvn-local.ps1 -DskipTests compile"
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null

$env:JAVA_HOME = $JavaHome
Push-Location $repoRoot
try {
    & $mvn -q dependency:build-classpath "-Dmdep.outputFile=$classpathFile"
    if ($LASTEXITCODE -ne 0) {
        throw "Maven dependency classpath generation failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

$dependencyClasspath = Get-Content -Path $classpathFile -Raw
$fullClasspath = "$targetClasses;$dependencyClasspath"

& $javac -cp $fullClasspath -d $buildDir $javaSrc
if ($LASTEXITCODE -ne 0) {
    throw "javac failed with exit code $LASTEXITCODE"
}
& $java -cp "$buildDir;$fullClasspath" org.pamguard.port.reference.FftFixtureExporter $WindowType $FftLength | Set-Content -Path $OutputPath -Encoding UTF8
if ($LASTEXITCODE -ne 0) {
    throw "java fixture exporter failed with exit code $LASTEXITCODE"
}
