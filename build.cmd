@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

git pull --ff-only || exit /b 1

set "GENERATOR="

rem Prefer well-known Visual Studio / Build Tools installations. This avoids
rem fragile FOR /F command-substitution around vswhere on some cmd.exe setups.
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC" set "GENERATOR=Visual Studio 17 2022"
if not defined GENERATOR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC" set "GENERATOR=Visual Studio 17 2022"
if not defined GENERATOR if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC" set "GENERATOR=Visual Studio 17 2022"
if not defined GENERATOR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC" set "GENERATOR=Visual Studio 17 2022"
if not defined GENERATOR if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC" set "GENERATOR=Visual Studio 17 2022"
if not defined GENERATOR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC" set "GENERATOR=Visual Studio 17 2022"
if not defined GENERATOR if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC" set "GENERATOR=Visual Studio 17 2022"
if not defined GENERATOR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC" set "GENERATOR=Visual Studio 17 2022"

if not defined GENERATOR if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC" set "GENERATOR=Visual Studio 18 2026"
if not defined GENERATOR if exist "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC" set "GENERATOR=Visual Studio 18 2026"

rem Fallback for custom install paths: ask vswhere, but write its result to a
rem temporary file instead of executing it inside FOR /F.
if not defined GENERATOR (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        set "VSINFO=%TEMP%\smoke_detector_vs.txt"
        "!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion > "!VSINFO!"
        set "VS_VERSION="
        set /p "VS_VERSION=" < "!VSINFO!"
        del /q "!VSINFO!" >nul 2>nul
        if defined VS_VERSION (
            for /f "tokens=1 delims=." %%M in ("!VS_VERSION!") do set "VS_MAJOR=%%M"
            if "!VS_MAJOR!"=="18" set "GENERATOR=Visual Studio 18 2026"
            if "!VS_MAJOR!"=="17" set "GENERATOR=Visual Studio 17 2022"
            if "!VS_MAJOR!"=="16" set "GENERATOR=Visual Studio 16 2019"
        )
    )
)

if not defined GENERATOR (
    echo No se encontro una instalacion de Visual Studio/Build Tools con C++.
    echo Se revisaron las instalaciones estandar y vswhere.
    exit /b 1
)

echo Usando: %GENERATOR%

if exist build rmdir /s /q build

cmake -S . -B build -G "%GENERATOR%" -A x64 || exit /b 1
cmake --build build --config Release --target smoke_detector resource_export resource_unpack smoke_sfx_stub || exit /b 1

echo.
echo Listo:
echo   build\Release\smoke_detector.exe
echo   build\Release\resource_export.exe
echo   build\Release\resource_unpack.exe
echo   build\Release\smoke_sfx_stub.exe
