# Recorded takes

How the two demo videos in `README.md` were made. Related: `docs/CONTROL_FILE.md` (the grammar of
every shot below), `docs/RUNNING.md` (build and launch, and every `run_sim.py` flag),
`docs/STREAMING.md` (the stream side, for a dashboard clip).

A demo video is a scripted take, not a live performance: start a rendering run, append the whole shot list
to its control file, and record the screen with ffmpeg. Game Bar is not usable here - it recorded nothing
on this machine - and neither are window-targeted captures of a GPU-composited window, which come back
black. What works is a desktop capture of a window that has been raised and pinned:

```bash
ffmpeg -f gdigrab -framerate 30 -draw_mouse 0 -offset_x 0 -offset_y 0 -video_size 2400x1600        -i desktop -t 240 -c:v h264_nvenc -preset p5 -cq 23 -pix_fmt yuv420p -an take.mp4
```

Three things cost whole takes before this was written down: `run_sim.py --no-console` (without it the
engine's log console floats over the frame), never resizing the window after launch (the HUD lays out
for the render size, so a forced resize crops its right column - pick `--res` to fit instead), and the
fact that a desktop capture records whatever is in FRONT. Pinning a window `HWND_TOPMOST` is not enough,
because Windows denies `SetForegroundWindow` to a process that is not already the foreground app; a take
recorded four minutes of an unrelated app's title bar that way. Raise the window first (drop
`SPI_SETFOREGROUNDLOCKTIMEOUT`, attach the foreground thread's input queue, tap Alt), then pin it, then
refuse to record unless the window you want reads back as the foreground process - and verify the result
from the recording itself (a frame per segment) rather than from the script's exit code.

```bash
python Tools/run_sim.py --mode C --seed 1 --duration 400 --speed 1 --windowed --res 1920x1080 --control-file Saved/take1.txt
python Tools/control.py --file Saved/take1.txt "at=58 caption=Drought: the river drops" "at=60 drought=on" "at=90 follow=Leviathan" "at=120 cam=-3000,900,420,-8,10" "at=150 set Look.bShowHUD=0"
```

`at=<sim_time> <command>` runs any control command at an exact **logical** time, so the same seed and shot
list produce the same take twice; `cam=x,y,z,pitch,yaw` places the camera, `follow=Lumen|Tecton|Leviathan|<scientist>|none`
frames a subject, `caption=<text>` announces the scenario with a large title in the lower third for
`Look.CaptionSeconds` (6 s; `set Look.bShowCaptions=0` turns the layer off), and `set Look.bShowHUD=0` hides
the HUD — captions keep drawing, so a clean-framed take still carries its narration (all of these are
camera/HUD only and change nothing in the world). The pending list is cleared by `reset` / `mode=`, so an old shot never re-fires into a new run.
`commands.csv` records each scheduled command at the sim time it actually ran. Append the shot list **after**
the sim starts: lines that already exist in the file when it launches are ignored.

A clip of the Lab dashboard instead of the world: run the sim `--windowed --offscreen --stream` (both flags
only apply to a rendering run, so `--windowed` is required even though no window appears), show it inside the
dashboard's World tab (`LAB_STREAM_URL` with `AutoConnect=true&AutoPlayVideo=true&AFKDetection=false`),
and give every recorded segment its own throwaway Chrome profile — a shared profile restores the previous
take's tabs and one of them comes forward mid-capture.
