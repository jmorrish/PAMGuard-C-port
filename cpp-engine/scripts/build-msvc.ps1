$ErrorActionPreference = "Stop"

$engineRoot = Split-Path -Parent $PSScriptRoot
$vsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Program Files\CMake\bin\cmake.exe"
$ninja = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe"

if (!(Test-Path $vsDevCmd)) {
    throw "Visual Studio developer command not found: $vsDevCmd"
}
if (!(Test-Path $cmake)) {
    throw "CMake not found: $cmake"
}
if (!(Test-Path $ninja)) {
    throw "Ninja not found: $ninja"
}

Push-Location $engineRoot
try {
    cmd.exe /s /c "`"$vsDevCmd`" -arch=x64 && `"$cmake`" -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninja`" && `"$cmake`" --build build"
}
finally {
    Pop-Location
}

