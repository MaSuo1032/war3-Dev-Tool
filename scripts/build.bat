@echo off
setlocal
cd /d "%~dp0.."

echo ========================================================
echo  Building War3DevTool v2.0 (x86 / 32-bit Release)
echo ========================================================

if not exist build (
    mkdir build
)

cmake -B build -A Win32 -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo [-] CMake configuration failed!
    pause
    exit /b %errorlevel%
)

cmake --build build --config Release
if %errorlevel% neq 0 (
    echo [-] Compilation failed!
    pause
    exit /b %errorlevel%
)

echo.
echo ========================================================
echo  Preparing bin release package inside project...
echo ========================================================

if not exist bin (
    mkdir bin
)

copy /Y build\bin\Release\JassHook.dll bin\JassHook.dll
copy /Y build\bin\Release\War3JassDevTool.exe bin\War3JassDevTool.exe
copy /Y build\bin\Release\Injector.exe bin\Injector.exe

copy /Y config\JassHookConfig.ini bin\JassHookConfig.ini
copy /Y config\mall_blacklist.txt bin\mall_blacklist.txt
copy /Y config\mall_spoof.txt bin\mall_spoof.txt

echo [+] Build and packaging completed successfully!
echo Files generated in: %~dp0..\bin
pause
