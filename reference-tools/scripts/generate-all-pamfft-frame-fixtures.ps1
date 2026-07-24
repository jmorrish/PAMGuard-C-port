$ErrorActionPreference = "Stop"

$portRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$outputDir = Join-Path $portRoot "cpp-engine\tests\fixtures\pamfft-frame"
$generator = Join-Path $PSScriptRoot "generate-pamfft-frame-fixture.ps1"

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$fixtures = @(
    @{
        Type = 2
        FftLength = 8
        FftHop = 4
        SampleRate = 8000
        ChunkLength = 16
        Name = "hann-8-hop-4-sr-8000-chunk-16.csv"
    }
)

foreach ($fixture in $fixtures) {
    $outputPath = Join-Path $outputDir $fixture.Name
    & $generator -WindowType $fixture.Type -FftLength $fixture.FftLength `
        -FftHop $fixture.FftHop -SampleRate $fixture.SampleRate `
        -ChunkLength $fixture.ChunkLength -OutputPath $outputPath
}
