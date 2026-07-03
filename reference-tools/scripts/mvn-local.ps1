param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $MavenArgs
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$env:JAVA_HOME = "C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot"
$mvn = Join-Path $repoRoot "tools\apache-maven-3.9.16\bin\mvn.cmd"

if (!(Test-Path $mvn)) {
    throw "Maven not found: $mvn"
}
if (!(Test-Path $env:JAVA_HOME)) {
    throw "JAVA_HOME not found: $env:JAVA_HOME"
}

& $mvn @MavenArgs

