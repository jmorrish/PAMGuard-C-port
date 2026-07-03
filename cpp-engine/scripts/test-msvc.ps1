$ErrorActionPreference = "Stop"

$engineRoot = Split-Path -Parent $PSScriptRoot
$vsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$ctest = "C:\Program Files\CMake\bin\ctest.exe"

Push-Location $engineRoot
try {
    cmd.exe /s /c "`"$vsDevCmd`" -arch=x64 && `"$ctest`" --test-dir build --output-on-failure"
}
finally {
    Pop-Location
}

