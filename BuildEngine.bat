@echo off
REM Potato Engine Build Script
REM This script requires Visual Studio with C++ development tools and CMake

echo ========================================
echo Potato Engine Build Script
echo ========================================
echo.

REM Check for CMake
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake not found in PATH
    echo Please install CMake or add it to PATH
    echo Download from: https://cmake.org/download/
    pause
    exit /b 1
)

echo CMake found: 
cmake --version
echo.

REM Check for Visual Studio
where cl >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo WARNING: MSVC compiler not found in PATH
    echo Please run this script from Visual Studio Developer Command Prompt
    echo Or setup environment with: "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    echo.
    echo Attempting to find Visual Studio...
)

REM Create build directory
if not exist build mkdir build
cd build

echo ========================================
echo Configuring CMake...
echo ========================================
cmake .. -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed
    pause
    exit /b 1
)

echo.
echo ========================================
echo Building Potato Engine...
echo ========================================
cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo.
echo Executables can be found in: build\bin\Release\
echo.
echo Main executables:
echo   - SimpleExample.exe
echo   - PlatformTest.exe
echo   - AIAgentGUIExample.exe
echo.

pause