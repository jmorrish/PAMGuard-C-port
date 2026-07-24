$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$Output = Join-Path $PortRoot "cpp-engine\tests\fixtures\sgram-corr\sgram-corr.csv"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\SgramCorrFixtureExporter.java"
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

# The exporter deliberately uses sun.reflect.ReflectionFactory (the same
# allocation path ObjectInputStream uses). javac's internal-API warning goes to
# stderr, which PowerShell's stop-on-error handling would treat as failure, so
# the compile runs via Start-Process with stderr captured to a file, shown only
# when it actually fails. The classpath exceeds the command-line limit, so the
# arguments travel in a javac @argfile.
$javacArgFile = Join-Path $BuildDir "javac-args.txt"
$javacErrFile = Join-Path $BuildDir "javac-err.txt"
@(
    "-nowarn"
    "-cp"
    "`"$($TargetClasses -replace '\\', '/');$($DependencyClasspath -replace '\\', '/')`""
    "-d"
    "`"$($BuildDir -replace '\\', '/')`""
    "`"$($JavaSrc -replace '\\', '/')`""
) | Set-Content -Path $javacArgFile -Encoding ascii
$javacProcess = Start-Process -FilePath $Javac -ArgumentList "@`"$javacArgFile`"" -Wait -PassThru -NoNewWindow -RedirectStandardError $javacErrFile
if ($javacProcess.ExitCode -ne 0) {
    Get-Content $javacErrFile | Out-Host
    throw "javac failed with exit code $($javacProcess.ExitCode)"
}
$LASTEXITCODE = 0

& $Java -cp "$BuildDir;$TargetClasses;$DependencyClasspath" org.pamguard.port.reference.SgramCorrFixtureExporter $Output
if ($LASTEXITCODE -ne 0) {
    throw "java fixture exporter failed with exit code $LASTEXITCODE"
}

Get-Content $Output -TotalCount 6 | Out-Host
