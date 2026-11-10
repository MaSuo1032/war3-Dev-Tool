@echo off
setlocal
set "NO_PAUSE=0"
if defined CI set "NO_PAUSE=1"
if /I "%~1"=="--no-pause" set "NO_PAUSE=1"
if not "%~1"=="" if /I not "%~1"=="--no-pause" (
    echo Usage: %~nx0 [--no-pause]
    exit /b 2
)

pushd "%~dp0.."
if errorlevel 1 exit /b 1
set "RESULT=1"
set "PLUGIN_PATH=%CD%\..\kkduizhan\plugins\Game\War3Plugin.dll"
if defined WAR3_PLUGIN_PATH set "PLUGIN_PATH=%WAR3_PLUGIN_PATH%"

where git.exe >nul 2>nul
if errorlevel 1 (
    echo [-] Git is not on PATH.
    goto finish
)
where python.exe >nul 2>nul
if errorlevel 1 (
    echo [-] Python 3 is not on PATH. The registry generator also needs pefile and capstone.
    goto finish
)
if not exist "%PLUGIN_PATH%" (
    echo [-] War3Plugin.dll not found: "%PLUGIN_PATH%"
    echo     Set WAR3_PLUGIN_PATH to the installed plugin DLL, then retry.
    goto finish
)

echo [1/4] Updating war3DevTool from GitHub...
git pull --ff-only origin main
if errorlevel 1 (
    echo [-] Git update failed. Resolve local changes or network access, then retry.
    goto finish
)

echo [2/4] Reading installed War3Plugin.dll: "%PLUGIN_PATH%"
python scripts\generate_dz_registry.py --plugin "%PLUGIN_PATH%"
if errorlevel 1 (
    echo [-] Plugin analysis failed. No binaries were replaced.
    goto finish
)
python scripts\generate_dz_registry.py --plugin "%PLUGIN_PATH%" --check
if errorlevel 1 goto finish

echo [3/4] Building and installing x86 Release binaries...
call scripts\build.bat --no-pause
if errorlevel 1 goto finish

echo [4/4] Running regression tests...
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 (
    echo [-] Regression tests failed. Check the output before using the new binaries.
    goto finish
)

echo [+] Ready: %CD%\bin
set "RESULT=0"

:finish
popd
if "%NO_PAUSE%"=="0" pause
exit /b %RESULT%
