$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$DefaultsOutput = Join-Path $PortRoot "cpp-engine\tests\fixtures\matched-template\settings-defaults.json"
$CatalogueOutput = Join-Path $PortRoot "web-ui\assets\matched-template-default-templates.json"
$ResampleOutput = Join-Path $PortRoot "cpp-engine\tests\fixtures\matched-template\template-resample.csv"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\MatchedTemplateSettingsFixtureExporter.java"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$OracleEnvironment = & (Join-Path $ScriptDir "resolve-pamguard-oracle.ps1") -PortRoot $PortRoot -RequireClasses -RequireClasspath
$Java = $OracleEnvironment.Java
$Javac = $OracleEnvironment.Javac
$TargetClasses = $OracleEnvironment.TargetClasses
$DependencyClasspath = $OracleEnvironment.DependencyClasspath

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DefaultsOutput) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $CatalogueOutput) | Out-Null

$JavacArgFile = Join-Path $BuildDir "javac-args-matched-template-settings.txt"
$JavacErrFile = Join-Path $BuildDir "javac-err-matched-template-settings.txt"
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

& $Java -cp "$BuildDir;$TargetClasses;$DependencyClasspath" org.pamguard.port.reference.MatchedTemplateSettingsFixtureExporter $DefaultsOutput $CatalogueOutput $ResampleOutput
if ($LASTEXITCODE -ne 0) {
    throw "Java fixture exporter failed with exit code $LASTEXITCODE"
}

Get-FileHash -Algorithm SHA256 $DefaultsOutput, $CatalogueOutput, $ResampleOutput |
    Select-Object Path, Hash |
    Format-Table -AutoSize
