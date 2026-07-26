$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$Output = Join-Path $PortRoot "cpp-engine\tests\fixtures\click-train\settings-defaults.json"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\MhtClickTrainSettingsFixtureExporter.java"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$OracleEnvironment = & (Join-Path $ScriptDir "resolve-pamguard-oracle.ps1") `
    -PortRoot $PortRoot -RequireClasses -RequireClasspath
$Java = $OracleEnvironment.Java
$Javac = $OracleEnvironment.Javac
$TargetClasses = $OracleEnvironment.TargetClasses
$DependencyClasspath = $OracleEnvironment.DependencyClasspath

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

$JavacArgFile = Join-Path $BuildDir "javac-args-mht-click-train-settings.txt"
$JavacErrFile = Join-Path $BuildDir "javac-err-mht-click-train-settings.txt"
@(
    "-nowarn"
    "-cp"
    "`"$($TargetClasses -replace '\\', '/');$($DependencyClasspath -replace '\\', '/')`""
    "-d"
    "`"$($BuildDir -replace '\\', '/')`""
    "`"$($JavaSrc -replace '\\', '/')`""
) | Set-Content -Path $JavacArgFile -Encoding ascii
$JavacProcess = Start-Process -FilePath $Javac `
    -ArgumentList "@`"$JavacArgFile`"" -Wait -PassThru -NoNewWindow `
    -RedirectStandardError $JavacErrFile
if ($JavacProcess.ExitCode -ne 0) {
    Get-Content -LiteralPath $JavacErrFile | Out-Host
    throw "javac failed with exit code $($JavacProcess.ExitCode)"
}

& $Java -cp "$BuildDir;$TargetClasses;$DependencyClasspath" `
    org.pamguard.port.reference.MhtClickTrainSettingsFixtureExporter `
    $Output $OracleEnvironment.Version $OracleEnvironment.Commit
if ($LASTEXITCODE -ne 0) {
    throw "Java fixture exporter failed with exit code $LASTEXITCODE"
}

Get-Content -LiteralPath $Output | Out-Host
