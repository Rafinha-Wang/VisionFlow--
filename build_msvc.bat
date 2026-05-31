@echo off
setlocal

echo Building VisionFlow Version8ProSafe DebugFixed with MSVC...

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo CMake configure failed.
    pause
    exit /b 1
)

cmake --build build --config Debug
if errorlevel 1 (
    echo Build failed.
    pause
    exit /b 1
)

echo.
echo Build finished.
echo Try running:
echo   build\Debug\VisionFlow.exe static demo\frames output_static
echo   build\Debug\VisionFlow.exe motion demo\frames output_motion
pause
