param(
    [string] $PortRoot = "",
    [switch] $RequireClasses,
    [switch] $RequireClasspath
)

$ErrorActionPreference = "Stop"

if (!$PortRoot) {
    $PortRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
}
else {
    $PortRoot = Resolve-Path $PortRoot
}

$toolRoot = Join-Path (Split-Path -Parent $PortRoot) "tools"
$oracleConfigPath = Join-Path $PortRoot "reference-tools\pamguard-oracle.json"
if (!(Test-Path -LiteralPath $oracleConfigPath)) {
    throw "PAMGuard oracle config not found: $oracleConfigPath"
}

$oracle = Get-Content -LiteralPath $oracleConfigPath -Raw | ConvertFrom-Json
$javaRepo = Join-Path $PortRoot $oracle.checkout
if (!(Test-Path -LiteralPath (Join-Path $javaRepo ".git"))) {
    throw "PAMGuard Java checkout not found: $javaRepo"
}

$actualCommit = (& git -C $javaRepo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Unable to read the PAMGuard Java commit from $javaRepo"
}
if ($actualCommit -ne $oracle.commit) {
    throw "Wrong PAMGuard Java oracle commit. Expected $($oracle.commit), found $actualCommit"
}

$versionPattern = "<version>$([regex]::Escape($oracle.version))</version>"
$versionMatches = Select-String -Path (Join-Path $javaRepo "pom.xml") -Pattern $versionPattern -Quiet
if (!$versionMatches) {
    throw "PAMGuard Java pom.xml does not declare oracle version $($oracle.version)"
}

$javaHomeCandidates = @(@(
    $env:JAVA_HOME
    (Join-Path $toolRoot "jdk-21.0.11+10")
    "C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot"
) | Where-Object { $_ -and (Test-Path -LiteralPath (Join-Path $_ "bin\javac.exe")) })
if ($javaHomeCandidates.Count -eq 0) {
    throw "JDK 21 not found. Set JAVA_HOME or install the portable JDK under $toolRoot"
}
$javaHome = $javaHomeCandidates[0]
$env:JAVA_HOME = $javaHome

$mavenCandidates = @(@(
    $(if ($env:PAMGUARD_MAVEN) { $env:PAMGUARD_MAVEN })
    $(if ($env:MAVEN_HOME) { Join-Path $env:MAVEN_HOME "bin\mvn.cmd" })
    (Join-Path $toolRoot "apache-maven-3.9.16\bin\mvn.cmd")
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) })
if ($mavenCandidates.Count -eq 0) {
    throw "Maven not found. Set PAMGUARD_MAVEN/MAVEN_HOME or install Maven under $toolRoot"
}
$maven = $mavenCandidates[0]

$targetClasses = Join-Path $javaRepo "target\classes"
if ($RequireClasses -and !(Test-Path -LiteralPath $targetClasses)) {
    Push-Location $javaRepo
    try {
        & $maven "-DskipTests" "compile"
        if ($LASTEXITCODE -ne 0) {
            throw "PAMGuard Java compilation failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}

$buildDir = Join-Path $PortRoot "reference-tools\java\build"
$classpathFile = Join-Path $buildDir "pamguard-classpath.txt"
$dependencyClasspath = ""
if ($RequireClasspath) {
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
    Push-Location $javaRepo
    try {
        & $maven "-q" "dependency:build-classpath" "-Dmdep.outputFile=$classpathFile"
        if ($LASTEXITCODE -ne 0) {
            throw "Maven dependency classpath generation failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
    $dependencyClasspath = (Get-Content -LiteralPath $classpathFile -Raw).Trim()
}

[pscustomobject]@{
    PortRoot = [string] $PortRoot
    JavaRepo = $javaRepo
    Version = $oracle.version
    Commit = $oracle.commit
    JavaHome = $javaHome
    Java = Join-Path $javaHome "bin\java.exe"
    Javac = Join-Path $javaHome "bin\javac.exe"
    Maven = $maven
    TargetClasses = $targetClasses
    BuildDir = $buildDir
    ClasspathFile = $classpathFile
    DependencyClasspath = $dependencyClasspath
}
