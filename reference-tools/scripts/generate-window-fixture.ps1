param(
    [Parameter(Mandatory = $true)]
    [int] $WindowType,

    [Parameter(Mandatory = $true)]
    [int] $Length,

    [Parameter(Mandatory = $true)]
    [string] $PamguardClasses,

    [Parameter(Mandatory = $true)]
    [string] $OutputPath,

    [string] $JavaHome = ""
)

$ErrorActionPreference = "Stop"

$toolRoot = Split-Path -Parent $PSScriptRoot
$javaSrc = Join-Path $toolRoot "java\src\org\pamguard\port\reference\WindowFixtureExporter.java"
$buildDir = Join-Path $toolRoot "java\build"

if ($JavaHome -ne "") {
    $javac = Join-Path $JavaHome "bin\javac.exe"
    $java = Join-Path $JavaHome "bin\java.exe"
}
else {
    $javac = "javac"
    $java = "java"
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

& $javac -cp $PamguardClasses -d $buildDir $javaSrc
& $java -cp "$buildDir;$PamguardClasses" org.pamguard.port.reference.WindowFixtureExporter $WindowType $Length | Set-Content -Path $OutputPath -Encoding UTF8
