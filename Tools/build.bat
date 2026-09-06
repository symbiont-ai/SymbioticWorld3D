@echo off
rem Builds the editor module, but REFUSES while any Unreal process is running: linking over a loaded
rem UnrealEditor-SymbioticWorld.dll fails, and replacing it under a live -game session (e.g. a Pixel
rem Streaming demo) crashes that session. Run from anywhere; UE_ROOT overrides the engine location.
rem System32 paths on purpose: under Git Bash, PATH puts Unix find/sort ahead of the Windows ones.
setlocal
set "HERE=%~dp0"
for %%I in ("%HERE%..") do set "REPO=%%~fI"
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.7"
set "SYS=%SystemRoot%\System32"

"%SYS%\tasklist.exe" /NH 2>nul | "%SYS%\findstr.exe" /I /C:"UnrealEditor" >nul
if not errorlevel 1 goto :busy

"%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" SymbioticWorldEditor Win64 Development -Project="%REPO%\SymbioticWorld.uproject" -WaitMutex -NoHotReload %*
exit /b %ERRORLEVEL%

:busy
echo BUILD REFUSED: an UnrealEditor process is running (a live sim, a stream, or a headless run).
echo Close it first, or wait for it to finish. Nothing was built.
exit /b 2
