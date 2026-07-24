param(
    [Parameter(Mandatory = $true)]
    [string] $WavPath,
    [string] $OutputDirectory = "",
    [int] $BlockSamples = 0,
    [double] $AcquisitionDcSeconds = 0.0,
    [double] $TriggerToleranceDb = 1e-12,
    [double] $ClickToleranceDb = 1e-12,
    [switch] $KeepTrace,
    [switch] $RebuildCpp
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$portRoot = Resolve-Path (Join-Path $scriptDirectory "..\..")
$wav = Resolve-Path -LiteralPath $WavPath
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) (
        "pamguard-click-wav-" + [System.Guid]::NewGuid().ToString("N"))
}
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null

$oracle = & (Join-Path $scriptDirectory "resolve-pamguard-oracle.ps1") `
    -PortRoot $portRoot -RequireClasses
$javaSource = Join-Path $portRoot `
    "reference-tools\java\src\org\pamguard\port\reference\ClickWavOracleExporter.java"
$javaBuild = Join-Path $portRoot "reference-tools\java\build"
New-Item -ItemType Directory -Force -Path $javaBuild | Out-Null

& $oracle.Javac -cp $oracle.TargetClasses -d $javaBuild $javaSource
if ($LASTEXITCODE -ne 0) {
    throw "ClickWavOracleExporter javac failed with exit code $LASTEXITCODE"
}

$cppExecutable = Join-Path $portRoot "cpp-engine\build\click_wav_parity_cli.exe"
if ($RebuildCpp -or !(Test-Path -LiteralPath $cppExecutable)) {
    & (Join-Path $portRoot "cpp-engine\scripts\build-msvc.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "C++ engine build failed with exit code $LASTEXITCODE"
    }
}
if (!(Test-Path -LiteralPath $cppExecutable)) {
    throw "click_wav_parity_cli was not found after the C++ build"
}

$javaClicksPath = Join-Path $output "java-clicks.csv"
$cppClicksPath = Join-Path $output "cpp-clicks.csv"
$javaTracePath = Join-Path $output "java-trigger-trace.csv"
$cppTracePath = Join-Path $output "cpp-trigger-trace.csv"

$javaArguments = @(
    "-cp"
    "$javaBuild;$($oracle.TargetClasses)"
    "org.pamguard.port.reference.ClickWavOracleExporter"
    [string] $wav
    $javaClicksPath
    $javaTracePath
    [string] $AcquisitionDcSeconds
)
if ($BlockSamples -gt 0) {
    $javaArguments += [string] $BlockSamples
}
& $oracle.Java @javaArguments
if ($LASTEXITCODE -ne 0) {
    throw "Java WAV oracle failed with exit code $LASTEXITCODE"
}

$cppArguments = @(
    [string] $wav
    $cppClicksPath
    $cppTracePath
)
if ($BlockSamples -gt 0) {
    $cppArguments += [string] $BlockSamples
}
& $cppExecutable @cppArguments | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "C++ WAV parity runner failed with exit code $LASTEXITCODE"
}

$javaClicks = @(Import-Csv -LiteralPath $javaClicksPath)
$cppClicks = @(Import-Csv -LiteralPath $cppClicksPath)
if ($javaClicks.Count -ne $cppClicks.Count) {
    throw "Click count mismatch: Java=$($javaClicks.Count), C++=$($cppClicks.Count)"
}

$maximumClickDbError = 0.0
for ($i = 0; $i -lt $javaClicks.Count; $i++) {
    $javaClick = $javaClicks[$i]
    $cppClick = $cppClicks[$i]
    $clickDbError = [Math]::Abs(
        [double] $javaClick.signalExcessDb - [double] $cppClick.signalExcessDb)
    $maximumClickDbError = [Math]::Max($maximumClickDbError, $clickDbError)
    $different = @()
    if ([int64] $javaClick.triggerStartSample -ne [int64] $cppClick.startSample) {
        $different += "startSample"
    }
    if ([int] $javaClick.duration -ne [int] $cppClick.duration) {
        $different += "duration"
    }
    if ([int] $javaClick.channelBitmap -ne [int] $cppClick.channelBitmap) {
        $different += "channelBitmap"
    }
    if ([int] $javaClick.triggerBitmap -ne [int] $cppClick.triggerBitmap) {
        $different += "triggerBitmap"
    }
    if ($clickDbError -gt $ClickToleranceDb) {
        $different += "signalExcessDb"
    }
    if ($different.Count -gt 0) {
        throw "Click $i mismatch ($($different -join ', ')): Java=$($javaClick | ConvertTo-Json -Compress), C++=$($cppClick | ConvertTo-Json -Compress)"
    }
}

$javaTrace = [System.IO.StreamReader]::new($javaTracePath)
$cppTrace = [System.IO.StreamReader]::new($cppTracePath)
$traceSamples = 0L
$maximumTriggerDbError = 0.0
$thresholdCrossingMismatches = 0L
try {
    $null = $javaTrace.ReadLine()
    $null = $cppTrace.ReadLine()
    while ($true) {
        $javaLine = $javaTrace.ReadLine()
        $cppLine = $cppTrace.ReadLine()
        if ($null -eq $javaLine -or $null -eq $cppLine) {
            if ($null -ne $javaLine -or $null -ne $cppLine) {
                throw "Trigger trace lengths differ after $traceSamples samples"
            }
            break
        }
        $javaValues = $javaLine.Split(',')
        $cppValues = $cppLine.Split(',')
        if ($javaValues[0] -ne $cppValues[0]) {
            throw "Trigger trace sample mismatch: Java=$($javaValues[0]), C++=$($cppValues[0])"
        }
        $errorDb = [Math]::Abs(
            [double] $javaValues[6] - [double] $cppValues[1])
        $maximumTriggerDbError = [Math]::Max($maximumTriggerDbError, $errorDb)
        if ($javaValues[7] -ne $cppValues[2]) {
            $thresholdCrossingMismatches++
        }
        if ($errorDb -gt $TriggerToleranceDb) {
            throw "Trigger mismatch at sample $($javaValues[0]): Java=$($javaValues[6]), C++=$($cppValues[1]), error=$errorDb dB"
        }
        $traceSamples++
    }
}
finally {
    $javaTrace.Dispose()
    $cppTrace.Dispose()
}
if ($thresholdCrossingMismatches -ne 0) {
    throw "$thresholdCrossingMismatches trigger threshold crossings differed"
}

$summary = [pscustomobject]@{
    wav = [string] $wav
    javaOracleVersion = $oracle.Version
    javaOracleCommit = $oracle.Commit
    blockSamples = if ($BlockSamples -gt 0) { $BlockSamples } else { "PAMGuard file default" }
    acquisitionDcSeconds = $AcquisitionDcSeconds
    samplesCompared = $traceSamples
    clicksCompared = $javaClicks.Count
    thresholdCrossingMismatches = $thresholdCrossingMismatches
    maximumTriggerDbError = $maximumTriggerDbError
    maximumClickDbError = $maximumClickDbError
    outputDirectory = $output
}
$summary | ConvertTo-Json -Depth 5 | Set-Content `
    -LiteralPath (Join-Path $output "summary.json") -Encoding utf8
$summary | Format-List | Out-Host

if (!$KeepTrace) {
    Remove-Item -LiteralPath $javaTracePath, $cppTracePath -Force
}

return $summary
