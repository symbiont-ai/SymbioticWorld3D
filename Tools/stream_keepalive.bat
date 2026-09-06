@echo off
rem Runs the streamed sim under a supervisor: if the process exits (the Pixel Streaming media layer
rem has aborted on viewer idle-timeouts), it is relaunched after 5 s and viewers just click start again.
rem Stop the loop by creating the file Saved\stop_stream (and closing the sim if it is still running).
rem Usage: Tools\stream_keepalive.bat [extra run_sim.py args, e.g. --seed 3 --policy-file Saved\servers.txt]
setlocal
set "HERE=%~dp0"
for %%I in ("%HERE%..") do set "REPO=%%~fI"
cd /d "%REPO%"
if exist Saved\stop_stream del Saved\stop_stream
:loop
if exist Saved\stop_stream goto :done
echo [keepalive] %date% %time% launching
python Tools\run_sim.py --mode C --seed 1 --windowed --offscreen --speed 1 --duration 36000 --stream %*
echo [keepalive] %date% %time% sim exited
timeout /t 5 /nobreak >nul
goto :loop
:done
echo [keepalive] stopped by Saved\stop_stream
