$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PortRoot = Resolve-Path (Join-Path $ScriptDir "..\..")
$RepoRoot = Resolve-Path (Join-Path $PortRoot "..")
$Maven = Join-Path $PortRoot "reference-tools\scripts\mvn-local.ps1"
$Psfx = Join-Path $PortRoot "cpp-engine\tests\fixtures\project-import\sample-project.psfx"
$Output = Join-Path $PortRoot "cpp-engine\tests\fixtures\project-import\sample-project-session.json"
$JavaHome = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { "C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot" }
$Java = Join-Path $JavaHome "bin\java.exe"
$Javac = Join-Path $JavaHome "bin\javac.exe"
$JavaSrc = Join-Path $PortRoot "reference-tools\java\src\org\pamguard\port\reference\PamguardProjectConverter.java"
$BuildDir = Join-Path $PortRoot "reference-tools\java\build"
$TargetClasses = Join-Path $RepoRoot "target\classes"
$ClasspathFile = Join-Path $PortRoot "reference-tools\java\pamguard-classpath.txt"
$DependencyClasspath = if (Test-Path $ClasspathFile) { (Get-Content $ClasspathFile -Raw).Trim() } else { "" }
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
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null

# The converter deliberately uses sun.reflect.ReflectionFactory (the same
# allocation path ObjectInputStream uses). javac's internal-API warning goes to
# stderr, which PowerShell's stop-on-error handling would treat as failure, so
# the compile runs via Start-Process with stderr captured to a file, shown only
# when it actually fails. The classpath exceeds the command-line limit, so the
# arguments travel in a javac @argfile.
$javacArgFile = Join-Path $BuildDir "javac-args.txt"
$javacErrFile = Join-Path $BuildDir "javac-err.txt"
@(
    "-nowarn"
    "-cp"
    "`"$($TargetClasses -replace '\\', '/');$($DependencyClasspath -replace '\\', '/')`""
    "-d"
    "`"$($BuildDir -replace '\\', '/')`""
    "`"$($JavaSrc -replace '\\', '/')`""
) | Set-Content -Path $javacArgFile -Encoding ascii
$javacProcess = Start-Process -FilePath $Javac -ArgumentList "@`"$javacArgFile`"" -Wait -PassThru -NoNewWindow -RedirectStandardError $javacErrFile
if ($javacProcess.ExitCode -ne 0) {
    Get-Content $javacErrFile | Out-Host
    throw "javac failed with exit code $($javacProcess.ExitCode)"
}
$LASTEXITCODE = 0

& $Java -cp "$BuildDir;$TargetClasses;$DependencyClasspath" org.pamguard.port.reference.PamguardProjectConverter write-sample $Psfx
if ($LASTEXITCODE -ne 0) {
    throw "sample psfx generation failed with exit code $LASTEXITCODE"
}
& $Java -cp "$BuildDir;$TargetClasses;$DependencyClasspath" org.pamguard.port.reference.PamguardProjectConverter convert $Psfx $Output
if ($LASTEXITCODE -ne 0) {
    throw "psfx conversion failed with exit code $LASTEXITCODE"
}

Get-Content $Output | Out-Host
