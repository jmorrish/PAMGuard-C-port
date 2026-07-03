$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$outputDir = Join-Path $repoRoot "pamguard-enterprise-port\cpp-engine\tests\fixtures\fft"
$generator = Join-Path $PSScriptRoot "generate-fft-fixture.ps1"

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$fixtures = @(
    @{ Type = 2; Length = 8; Name = "hann-8.csv" },
    @{ Type = 2; Length = 1024; Name = "hann-1024.csv" }
)

foreach ($fixture in $fixtures) {
    $outputPath = Join-Path $outputDir $fixture.Name
    & $generator -WindowType $fixture.Type -FftLength $fixture.Length -OutputPath $outputPath
}

