@echo off
setlocal
pushd "%~dp0..\bin"
if errorlevel 1 exit /b 1
if not exist War3DevToolUI.exe (
    echo [-] War3DevToolUI.exe is missing. Run scripts\build.bat first.
    popd
    exit /b 2
)
start "" /D "%CD%" War3DevToolUI.exe
set "RUN_EXIT=%errorlevel%"
popd
exit /b %RUN_EXIT%
