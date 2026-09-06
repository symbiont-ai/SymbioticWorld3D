@echo off
rem Starts Epic's Pixel Streaming signalling server (player page on port 80, streamer on 8888)
rem from the engine's plugin folder. First run downloads Node and builds the frontend (a few minutes).
rem Fetch the server once with:  "<UE_5.7>\Engine\Plugins\Media\PixelStreaming2\Resources\WebServers\get_ps_servers.bat"
rem Then run the sim with:        python Tools\run_sim.py --mode C --seed 1 --windowed --speed 1 --duration 36000 --stream
rem Viewers on the same network open  http://<this machine's LAN IP>/  in Chrome or Safari.
rem Allow node.exe on private networks when Windows Firewall asks, or nobody but you can connect.
set "UE_ROOT=%UE_ROOT%"
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.7"
set "PS=%UE_ROOT%\Engine\Plugins\Media\PixelStreaming2\Resources\WebServers\SignallingWebServer\platform_scripts\cmd\start.bat"
if not exist "%PS%" (
  echo Signalling server not found at "%PS%".
  echo Run get_ps_servers.bat in "%UE_ROOT%\Engine\Plugins\Media\PixelStreaming2\Resources\WebServers" first.
  exit /b 1
)
call "%PS%" %*
