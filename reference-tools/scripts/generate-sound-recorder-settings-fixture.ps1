$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$Output = Join-Path $PortRoot "cpp-engine\tests\fixtures\sound-recorder\settings-defaults.json"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\SoundRecorder\SoundRecorderSettingsFixtureExporter.java"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$FixtureHome = Join-Path $BuildDir "sound-recorder-fixture-home"
$OracleEnvironment = & (Join-Path $ScriptDir "resolve-pamguard-oracle.ps1") `
    -PortRoot $PortRoot -RequireClasses -RequireClasspath
$Java = $OracleEnvironment.Java
$Javac = $OracleEnvironment.Javac
$TargetClasses = $OracleEnvironment.TargetClasses
$DependencyClasspath = $OracleEnvironment.DependencyClasspath

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $FixtureHome | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

$JavacArgFile = Join-Path $BuildDir "javac-args-sound-recorder-settings.txt"
@(
    "-nowarn"
    "-cp"
    "`"$($TargetClasses -replace '\\', '/');$($DependencyClasspath -replace '\\', '/')`""
    "-d"
    "`"$($BuildDir -replace '\\', '/')`""
    "`"$($JavaSrc -replace '\\', '/')`""
) | Set-Content -LiteralPath $JavacArgFile -Encoding ascii

& $Javac "@$JavacArgFile"
if ($LASTEXITCODE -ne 0) {
    throw "javac failed with exit code $LASTEXITCODE"
}

& $Java "-Duser.home=$FixtureHome" `
    -cp "$BuildDir;$TargetClasses;$DependencyClasspath" `
    SoundRecorder.SoundRecorderSettingsFixtureExporter `
    $Output $OracleEnvironment.Version $OracleEnvironment.Commit
if ($LASTEXITCODE -ne 0) {
    throw "Java fixture exporter failed with exit code $LASTEXITCODE"
}

Get-FileHash -Algorithm SHA256 $Output |
    Select-Object Path, Hash |
    Format-Table -AutoSize
