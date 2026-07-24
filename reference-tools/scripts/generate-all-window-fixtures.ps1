$ErrorActionPreference = "Stop"

$portRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$environment = & (Join-Path $PSScriptRoot "resolve-pamguard-oracle.ps1") `
    -PortRoot $portRoot -RequireClasses
$outputDir = Join-Path $portRoot "cpp-engine\tests\fixtures\window"
$generator = Join-Path $PSScriptRoot "generate-window-fixture.ps1"

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$fixtures = @(
    @{ Type = 0; Length = 8; Name = "rectangular-8.csv" },
    @{ Type = 1; Length = 8; Name = "hamming-8.csv" },
    @{ Type = 2; Length = 8; Name = "hann-8.csv" },
    @{ Type = 3; Length = 8; Name = "bartlett-8.csv" },
    @{ Type = 4; Length = 8; Name = "blackman-8.csv" },
    @{ Type = 5; Length = 8; Name = "blackman-harris-8.csv" },
    @{ Type = 2; Length = 1024; Name = "hann-1024.csv" }
)

foreach ($fixture in $fixtures) {
    $outputPath = Join-Path $outputDir $fixture.Name
    & $generator -WindowType $fixture.Type -Length $fixture.Length `
        -PamguardClasses $environment.TargetClasses -OutputPath $outputPath `
        -JavaHome $environment.JavaHome
}
