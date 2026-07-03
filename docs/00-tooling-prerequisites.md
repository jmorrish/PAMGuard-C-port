# Tooling Prerequisites

This workspace contains source and build definitions. The local machine initially did not have C++ or Java tooling on `PATH`.

Installed during setup via `winget`:

- Visual Studio Build Tools 2022 with C++ workload
- CMake
- Ninja
- Eclipse Temurin JDK 21
- FFmpeg 8.1.2

Installed locally under `tools/`:

- Apache Maven 3.9.16

Depending on the parent shell, new tools may not appear on `PATH` until a new terminal is opened. The helper scripts use known install locations where practical.

Recommended Windows setup:

- Visual Studio Build Tools with C++ workload
- CMake
- Ninja
- Java JDK matching the PAMGuard build target

Recommended command once tooling is installed:

```powershell
cd C:\python\PAMGuard\pamguard-enterprise-port\cpp-engine
cmake -S . -B build -G Ninja
cmake --build build
```

The first validated build target is `pamguard_engine_cli`.

Current validated command path:

```powershell
.\pamguard-enterprise-port\cpp-engine\scripts\build-msvc.ps1
```

Window parity fixture generation:

```powershell
.\pamguard-enterprise-port\reference-tools\scripts\generate-all-window-fixtures.ps1
```

FFT parity fixture generation:

```powershell
.\pamguard-enterprise-port\reference-tools\scripts\generate-all-fft-fixtures.ps1
```

Repo-local Maven helper:

```powershell
.\pamguard-enterprise-port\reference-tools\scripts\mvn-local.ps1 -version
```

FFmpeg path found during setup:

```text
C:\Users\j.morrish\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1.2-full_build\bin\ffmpeg.exe
```
