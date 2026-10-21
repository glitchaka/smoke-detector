@echo off
setlocal
cd /d "%~dp0"

git pull --ff-only || exit /b 1

if exist build rmdir /s /q build

cmake -S . -B build -G "Visual Studio 18 2026" -A x64 || exit /b 1
cmake --build build --config Release --target smoke_detector resource_export resource_unpack smoke_sfx_stub || exit /b 1

echo.
echo Listo:
echo   build\Release\smoke_detector.exe
echo   build\Release\resource_export.exe
echo   build\Release\resource_unpack.exe
echo   build\Release\smoke_sfx_stub.exe
