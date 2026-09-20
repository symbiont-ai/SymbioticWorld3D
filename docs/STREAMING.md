# Watching and driving the sim from other computers (Pixel Streaming)

One Symbiotic World (`README.md`) runs on the Windows host and a room watches it in their browsers.
Related: `docs/RUNNING.md` (build and launch it first), `docs/CONTROL_FILE.md` (the grammar of the
control file used below), `Lab/README.md` (the dashboard the 🧪 button opens).

The sim runs once, on the Windows machine; anyone on the same network watches and
controls it from a browser (Chrome or Safari, including Macs). One-time setup:
fetch Epic's signalling server with the engine's script
`Engine/Plugins/Media/PixelStreaming2/Resources/WebServers/get_ps_servers.bat`
(the `PixelStreaming2` plugin is enabled in the `.uproject`). Then, in two terminals:

```bash
Tools\start_stream_server.bat
Tools\stream_keepalive.bat
```

`stream_keepalive.bat` runs `run_sim.py --windowed --offscreen --stream` for the day and relaunches it
after any exit (create `Saved\stop_stream` to stop the loop). Viewers open
`http://<host LAN IP>/?AFKDetection=false&HoveringMouse=true` and click to start. The two
parameters matter: the player page's idle timeout disconnects a still viewer after 120 s and
that disconnect has aborted the Pixel Streaming media layer three times (exit 0xC0000409,
no crash report); hovering-mouse mode makes clicks select organisms instead of grabbing the
camera. Mouse and keyboard from the
browser reach the sim (select, follow, drought, speed, camera), so agree on one driver at a
time. Allow `node.exe` on private networks when Windows Firewall asks. The stream is
encoded on the host GPU (NVENC on NVIDIA); hackathon wifi that isolates clients from each
other blocks it, in which case fall back to screen sharing.

The sim sends no audio: the per-viewer audio tracks are pure risk in the media layer, so
`run_sim.py --stream` disables transmit and receive audio.

`python Tools/add_lab_button.py` (host, idempotent, `--remove` restores) patches the served
player pages with a floating **🧪 Symbiotic Lab** button that opens the dashboard on the same
host at :8765, so anyone watching the world is one click from the scientists' conversations
(`Lab/README.md`).

## Perturbing the running world without its keyboard

To perturb the running world from a script or a second terminal instead of its keyboard (drought,
speed, pause, any `--set` parameter, reset, mode), append lines to `Saved/control.txt`:
`python Tools/control.py "drought=on"`; every executed command is logged to the run's `commands.csv`.
Grammar and which settings take effect live: `docs/CONTROL_FILE.md`. Over HTTP instead of a file:
`docs/SCIENTIST_API.md` (`POST /control`).
