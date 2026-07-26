$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$Output = Join-Path $PortRoot "cpp-engine\tests\fixtures\click-localisation\tracked-click-params.csv"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\TrackedClickParamsFixtureExporter.java"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$OracleEnvironment = & (Join-Path $ScriptDir "resolve-pamguard-oracle.ps1") -PortRoot $PortRoot -RequireClasses -RequireClasspath
$Java = $OracleEnvironment.Java
$Javac = $OracleEnvironment.Javac
$TargetClasses = $OracleEnvironment.TargetClasses
$DependencyClasspath = $OracleEnvironment.DependencyClasspath

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

$JavacArgFile = Join-Path $BuildDir "javac-args-tracked-click-params.txt"
$JavacErrFile = Join-Path $BuildDir "javac-err-tracked-click-params.txt"
@(
    "-nowarn"
    "-cp"
    "`"$($TargetClasses -replace '\\', '/');$($DependencyClasspath -replace '\\', '/')`""
    "-d"
    "`"$($BuildDir -replace '\\', '/')`""
    "`"$($JavaSrc -replace '\\', '/')`""
) | Set-Content -Path $JavacArgFile -Encoding ascii
$JavacProcess = Start-Process -FilePath $Javac -ArgumentList "@`"$JavacArgFile`"" -Wait -PassThru -NoNewWindow -RedirectStandardError $JavacErrFile
if ($JavacProcess.ExitCode -ne 0) {
    Get-Content $JavacErrFile | Out-Host
    throw "javac failed with exit code $($JavacProcess.ExitCode)"
}

& $Java -cp "$BuildDir;$TargetClasses;$DependencyClasspath" org.pamguard.port.reference.TrackedClickParamsFixtureExporter $Output
if ($LASTEXITCODE -ne 0) {
    throw "Java fixture exporter failed with exit code $LASTEXITCODE"
}

Get-Content $Output | Out-Host
