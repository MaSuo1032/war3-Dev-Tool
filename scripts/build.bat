@echo off
setlocal
set "BUILD_NO_PAUSE=0"
if defined CI set "BUILD_NO_PAUSE=1"
if /I "%~1"=="--no-pause" set "BUILD_NO_PAUSE=1"
if not "%~1"=="" if /I not "%~1"=="--no-pause" (
    echo Usage: %~nx0 [--no-pause]
    exit /b 2
)

pushd "%~dp0.."
if errorlevel 1 exit /b 1
set "BUILD_EXIT=0"

echo Building War3DevTool ^(x86 / 32-bit Release^)...
where cmake.exe >nul 2>nul
if errorlevel 1 (
    echo [-] CMake is not on PATH. Install CMake 3.15 or newer.
    set "BUILD_EXIT=1"
    goto finish
)

cmake -S . -B build -A Win32
set "BUILD_EXIT=%errorlevel%"
if not "%BUILD_EXIT%"=="0" (
    echo [-] CMake configuration failed.
    goto finish
)

cmake --build build --config Release
set "BUILD_EXIT=%errorlevel%"
if not "%BUILD_EXIT%"=="0" (
    echo [-] Compilation failed.
    goto finish
)

cmake --install build --config Release --component Runtime --prefix "%CD%\bin"
set "BUILD_EXIT=%errorlevel%"
if not "%BUILD_EXIT%"=="0" (
    echo [-] Packaging failed. Close the running tool before replacing its files.
    goto finish
)

echo [+] Build and packaging completed: %CD%\bin

:finish
popd
if "%BUILD_NO_PAUSE%"=="0" pause
exit /b %BUILD_EXIT%
