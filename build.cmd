@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

git pull --ff-only || exit /b 1

set "GENERATOR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do set "VS_VERSION=%%V"
    if defined VS_VERSION (
        for /f "tokens=1 delims=." %%M in ("!VS_VERSION!") do set "VS_MAJOR=%%M"
        if "!VS_MAJOR!"=="18" set "GENERATOR=Visual Studio 18 2026"
        if "!VS_MAJOR!"=="17" set "GENERATOR=Visual Studio 17 2022"
        if "!VS_MAJOR!"=="16" set "GENERATOR=Visual Studio 16 2019"
    )
)

if not defined GENERATOR (
    echo No se encontro una instalacion de Visual Studio/Build Tools con C++.
    echo Instala "Desktop development with C++" o los Visual Studio Build Tools con MSVC y Windows SDK.
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
