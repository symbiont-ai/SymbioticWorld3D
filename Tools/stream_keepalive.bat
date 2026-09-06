@echo off
rem Runs the streamed sim under a supervisor: if the process exits (the Pixel Streaming media layer has
rem aborted several times after viewer sessions), it is relaunched after 5 s and viewers just click start again.
rem   Stop the loop:            create the file Saved\stop_stream (and close the sim if it is still running)
rem   Predator after restarts:  create Saved\predator_on; each relaunch then re-applies
rem                             "set Settings.bLeviathan=1" + "reset" through the control file once the NEW world
rem                             is watching it (a command appended before that is treated as an old line and ignored)
rem Usage: Tools\stream_keepalive.bat [extra run_sim.py args, e.g. --seed 3 --policy-file Saved\servers.txt]
setlocal enabledelayedexpansion
set "HERE=%~dp0"
for %%I in ("%HERE%..") do set "REPO=%%~fI"
cd /d "%REPO%"
if exist Saved\stop_stream del Saved\stop_stream
:loop
if exist Saved\stop_stream goto :done
echo [keepalive] %date% %time% launching
echo.> Saved\launch_marker
start "SymbioticWorld stream" /b python Tools\run_sim.py --mode C --seed 1 --windowed --offscreen --speed 1 --duration 36000 --stream %*
set /a waited=0
:waitup
timeout /t 5 /nobreak >nul
set /a waited+=5
tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | find /I "UnrealEditor.exe" >nul || goto :exited
rem the new process rotates the old log to a backup and writes a fresh SymbioticWorld.log (newer than the marker)
powershell -NoProfile -Command "$l='Saved\Logs\SymbioticWorld.log'; if ((Test-Path $l) -and ((Get-Item $l).LastWriteTime -gt (Get-Item 'Saved\launch_marker').LastWriteTime) -and (Select-String -Path $l -Pattern 'control file:' -Quiet)) { exit 0 } else { exit 1 }" >nul 2>&1
if not errorlevel 1 goto :up
if !waited! lss 900 goto :waitup
:up
if exist Saved\predator_on (
  timeout /t 3 /nobreak >nul
  python Tools\control.py "set Settings.bLeviathan=1" "reset"
  echo [keepalive] %date% %time% predator re-enabled
)
:running
timeout /t 10 /nobreak >nul
tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>nul | find /I "UnrealEditor.exe" >nul && goto :running
:exited
echo [keepalive] %date% %time% sim exited
timeout /t 5 /nobreak >nul
goto :loop
:done
echo [keepalive] stopped by Saved\stop_stream
