param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $MavenArgs
)

$ErrorActionPreference = "Stop"

$portRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$environment = & (Join-Path $PSScriptRoot "resolve-pamguard-oracle.ps1") -PortRoot $portRoot

Push-Location $environment.JavaRepo
try {
    & $environment.Maven @MavenArgs
    $mavenExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}

if ($mavenExitCode -ne 0) {
    throw "Maven failed with exit code $mavenExitCode"
}
