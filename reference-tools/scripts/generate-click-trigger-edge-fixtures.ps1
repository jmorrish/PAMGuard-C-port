$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$RepoRoot = Resolve-Path (Join-Path $PortRoot "..")
$Maven = Join-Path $PortRoot "reference-tools\scripts\mvn-local.ps1"
$FixtureDir = Join-Path $PortRoot "cpp-engine\tests\fixtures\click-trigger"
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot" }
$Java = Join-Path $JavaHome "bin\java.exe"
$Javac = Join-Path $JavaHome "bin\javac.exe"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\ClickTriggerFixtureExporter.java"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$TargetClasses = Join-Path $RepoRoot "target\classes"
if (-not (Test-Path $Java)) {
    throw "java.exe was not found at $Java"
}
if (-not (Test-Path $Javac)) {
    throw "javac.exe was not found at $Javac"
}

if (-not (Test-Path $TargetClasses)) {
    Push-Location $RepoRoot
    try {
        & $Maven -DskipTests compile | Out-Host
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
    finally {
        Pop-Location
    }
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $FixtureDir | Out-Null

& $Javac -cp $TargetClasses -d $BuildDir $JavaSrc
if ($LASTEXITCODE -ne 0) {
    throw "javac failed with exit code $LASTEXITCODE"
}

# Argument order: channelBitmap triggerBitmap thresholdDb shortFilter longFilter
#                 preSample postSample minSep maxLength minTriggerChannels
#                 sampleRate chunkLength output scenario
$Fixtures = @(
    @{ Name = "double-split-minsep8.csv";          Args = @("0x3", "0x3", "10.0", "0.1", "0.00001", "10", "12", "8",  "128", "1", "48000", "256", "double-transient") },
    @{ Name = "double-merge-minsep16.csv";         Args = @("0x3", "0x3", "10.0", "0.1", "0.00001", "10", "12", "16", "128", "1", "48000", "256", "double-transient") },
    @{ Name = "long-maxlen64.csv";                 Args = @("0x3", "0x3", "10.0", "0.1", "0.00001", "10", "12", "8",  "64",  "1", "48000", "1024", "long-transient") },
    @{ Name = "single-channel-min1.csv";           Args = @("0x3", "0x3", "10.0", "0.1", "0.00001", "10", "12", "8",  "128", "1", "48000", "256", "single-channel-transient") },
    @{ Name = "single-channel-min2-suppressed.csv"; Args = @("0x3", "0x3", "10.0", "0.1", "0.00001", "10", "12", "8",  "128", "2", "48000", "256", "single-channel-transient") },
    @{ Name = "single-threshold6.csv";             Args = @("0x3", "0x3", "6.0",  "0.2", "0.0001",  "10", "12", "8",  "128", "1", "48000", "256", "single-transient") }
)

foreach ($Fixture in $Fixtures) {
    $Output = Join-Path $FixtureDir $Fixture.Name
    $ExporterArgs = @($Fixture.Args[0..11]) + @($Output, $Fixture.Args[12])
    & $Java -cp "$BuildDir;$TargetClasses" org.pamguard.port.reference.ClickTriggerFixtureExporter @ExporterArgs
    if ($LASTEXITCODE -ne 0) {
        throw "java fixture exporter failed for $($Fixture.Name) with exit code $LASTEXITCODE"
    }
    $RowCount = (Get-Content $Output | Measure-Object -Line).Lines - 1
    Write-Host "generated $($Fixture.Name): $RowCount detection row(s)"
}
