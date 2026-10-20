@echo off
setlocal
cd /d "%~dp0"

git pull --ff-only || exit /b 1

if exist build rmdir /s /q build

cmake -S . -B build -G "Visual Studio 18 2026" -A x64 || exit /b 1
cmake --build build --config Release --target promptemon_demo || exit /b 1

echo.
echo Listo: build\Release\promptemon_demo.exe
