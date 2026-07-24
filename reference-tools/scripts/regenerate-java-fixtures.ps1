param(
    [string] $LogDirectory = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$OracleEnvironment = & (Join-Path $ScriptDir "resolve-pamguard-oracle.ps1") `
    -PortRoot $PortRoot -RequireClasses -RequireClasspath

if (!$LogDirectory) {
    $LogDirectory = Join-Path $OracleEnvironment.BuildDir "fixture-logs"
}
New-Item -ItemType Directory -Force -Path $LogDirectory | Out-Null

# The three parameterised leaf scripts are driven by their corresponding
# generate-all-* entry points. Every other generate-* script is a complete
# fixture-family entry point.
$generators = @(
    Get-ChildItem -LiteralPath $ScriptDir -Filter "generate-*.ps1" |
        Where-Object {
            (Get-Content -LiteralPath $_.FullName -Raw) -notmatch "(?m)^\s*param\("
        } |
        Sort-Object Name
)
if ($generators.Count -eq 0) {
    throw "No fixture generator entry points found under $ScriptDir"
}

$powershell = (Get-Process -Id $PID).Path
for ($index = 0; $index -lt $generators.Count; $index++) {
    $generator = $generators[$index]
    $stem = [IO.Path]::GetFileNameWithoutExtension($generator.Name)
    $stdout = Join-Path $LogDirectory "$stem.stdout.log"
    $stderr = Join-Path $LogDirectory "$stem.stderr.log"
    Write-Host "[$($index + 1)/$($generators.Count)] $($generator.Name)"
    $process = Start-Process -FilePath $powershell `
        -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $generator.FullName) `
        -WorkingDirectory $PortRoot -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if ($process.ExitCode -ne 0) {
        if ((Get-Item -LiteralPath $stderr).Length -gt 0) {
            Get-Content -LiteralPath $stderr -Tail 40 | Out-Host
        }
        if ((Get-Item -LiteralPath $stdout).Length -gt 0) {
            Get-Content -LiteralPath $stdout -Tail 40 | Out-Host
        }
        throw "$($generator.Name) failed with exit code $($process.ExitCode)"
    }
}

# Project fixture generation contains Java-serialised object graphs. Generate
# it once more and require an identical hash so accidental random/default state
# cannot silently enter the checked-in binary.
$projectFixture = Join-Path $PortRoot "cpp-engine\tests\fixtures\project-import\sample-project.psfx"
$projectGenerator = Join-Path $ScriptDir "generate-project-import-fixture.ps1"
$firstHash = (Get-FileHash -LiteralPath $projectFixture -Algorithm SHA256).Hash
$stdout = Join-Path $LogDirectory "generate-project-import-determinism.stdout.log"
$stderr = Join-Path $LogDirectory "generate-project-import-determinism.stderr.log"
$process = Start-Process -FilePath $powershell `
    -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $projectGenerator) `
    -WorkingDirectory $PortRoot -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
if ($process.ExitCode -ne 0) {
    if ((Get-Item -LiteralPath $stderr).Length -gt 0) {
        Get-Content -LiteralPath $stderr -Tail 40 | Out-Host
    }
    throw "Project fixture determinism pass failed with exit code $($process.ExitCode)"
}
$secondHash = (Get-FileHash -LiteralPath $projectFixture -Algorithm SHA256).Hash
if ($firstHash -ne $secondHash) {
    throw "Project fixture is not deterministic: $firstHash != $secondHash"
}

Write-Host "Regenerated $($generators.Count) fixture families from PAMGuard $($OracleEnvironment.Version)."
Write-Host "Project fixture determinism: PASS ($secondHash)"
