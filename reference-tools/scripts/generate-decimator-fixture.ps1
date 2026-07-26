$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$Output = Join-Path $PortRoot "cpp-engine\tests\fixtures\decimator\decimator-stream.csv"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\DecimatorFixtureExporter.java"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$OracleEnvironment = & (Join-Path $ScriptDir "resolve-pamguard-oracle.ps1") -PortRoot $PortRoot -RequireClasses -RequireClasspath
$Java = $OracleEnvironment.Java
$Javac = $OracleEnvironment.Javac
$TargetClasses = $OracleEnvironment.TargetClasses
$DependencyClasspath = $OracleEnvironment.DependencyClasspath

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

$javacArgFile = Join-Path $BuildDir "javac-args-decimator.txt"
$javacErrFile = Join-Path $BuildDir "javac-err-decimator.txt"
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

& $Java -cp "$BuildDir;$TargetClasses;$DependencyClasspath" org.pamguard.port.reference.DecimatorFixtureExporter $Output
if ($LASTEXITCODE -ne 0) {
    throw "Java fixture exporter failed with exit code $LASTEXITCODE"
}

Get-Content $Output | Out-Host
