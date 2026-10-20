@echo off
setlocal
cd /d "%~dp0"

git pull --ff-only || exit /b 1

if exist build rmdir /s /q build

cmake -S . -B build -G "Visual Studio 18 2026" -A x64 || exit /b 1
cmake --build build --config Release --target smoke_detector || exit /b 1

echo.
echo Listo: build\Release\smoke_detector.exe
