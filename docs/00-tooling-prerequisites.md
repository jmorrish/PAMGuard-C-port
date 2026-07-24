# Tooling Prerequisites

This workspace contains source and build definitions. The local machine initially did not have C++ or Java tooling on `PATH`.

Installed system-wide or through `winget`:

- Visual Studio Build Tools 2022 with C++ workload
- CMake
- Ninja

Installed as checksum-verified portable tools under `C:\python\tools\`:

- Eclipse Temurin JDK 21.0.11+10
- Apache Maven 3.9.16

The Maven helper discovers those portable paths and verifies the exact Java
oracle commit before building. FFmpeg remains optional unless live ingest is
being exercised.

Recommended Windows setup:

- Visual Studio Build Tools with C++ workload
- CMake
- Ninja
- Java JDK matching the PAMGuard build target

Recommended command once tooling is installed:

```powershell
cd C:\python\PAMGuard_Port
.\reference-tools\scripts\mvn-local.ps1 -DskipTests compile
.\reference-tools\scripts\regenerate-java-fixtures.ps1
.\cpp-engine\scripts\build-msvc.ps1
.\cpp-engine\scripts\test-msvc.ps1
```

Validated length-8 Hann parity round trip:

```powershell
.\reference-tools\scripts\generate-window-fixture.ps1 `
  -WindowType 2 -Length 8 `
  -PamguardClasses .\PAMGuard_Java\target\classes `
  -OutputPath .\cpp-engine\tests\fixtures\window\hann-8.csv `
  -JavaHome C:\python\tools\jdk-21.0.11+10
```

Repo-local Maven helper:

```powershell
.\reference-tools\scripts\mvn-local.ps1 --version
```
