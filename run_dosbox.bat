@echo off
setlocal

set "APP_DIR=%~dp0"
set "DOSBOX_EXE=%DOSBOX_EXE%"
if not defined DOSBOX_EXE set "DOSBOX_EXE=C:\Projects\RetroComputers\Emulators\DOSbox\DOSBox.exe"

if not exist "%APP_DIR%LLander.exe" (
    echo LLander.exe was not found. Build the project first with build.ps1.
    pause
    exit /b 1
)
if not exist "%APP_DIR%LANDOFF.DAT" (
    echo LANDOFF.DAT is missing. Build the project first with build.ps1.
    pause
    exit /b 1
)
if not exist "%APP_DIR%LANDON.DAT" (
    echo LANDON.DAT is missing. Build the project first with build.ps1.
    pause
    exit /b 1
)
if not exist "%APP_DIR%MOON.DAT" (
    echo MOON.DAT is missing. Build the project first with build.ps1.
    pause
    exit /b 1
)

if not exist "%DOSBOX_EXE%" (
    set "DOSBOX_EXE="
    for /f "delims=" %%I in ('where DOSBox.exe 2^>nul') do if not defined DOSBOX_EXE set "DOSBOX_EXE=%%I"
)
if not defined DOSBOX_EXE (
    echo DOSBox.exe was not found. Set DOSBOX_EXE or edit this launcher with its path.
    pause
    exit /b 1
)

start "" /D "%APP_DIR%" "%DOSBOX_EXE%" -c "mount c ." -c "c:" -c "LLander.exe"