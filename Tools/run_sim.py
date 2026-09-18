#!/usr/bin/env python
"""Launch headless (or windowed) Symbiotic World runs from the command line.

This is the closed-loop check: run the sim without watching it, then point
Analysis/analyze_run.py at the CSV logs it wrote.

Examples
  python Tools/run_sim.py --mode C --seed 42 --duration 600
  python Tools/run_sim.py --mode C N --seed 1 2 3 --duration 900      # 6 runs, sequential
  python Tools/run_sim.py --mode B --seed 7 --duration 300 --windowed # watch it
  python Tools/run_sim.py --mode C --seed 1 --duration 600 --set "Settings.PatchRegenPerSec=5;Lumen.ReproThreshold=85"
  python Tools/run_sim.py --mode C --seed 1 --duration 45 --speed 1 --windowed --shot 4,40 --no-logs
  python Tools/run_sim.py --mode C --seed 1 --duration 300 --speed 1 --windowed --res 1920x1080 --control-file Saved/take1.txt   # recorded take (docs/CONTROL_FILE.md)
  python Tools/run_sim.py --mode C --seed 7 --duration 600 --speed 20 --policy "10.228.152.5:9000=Lumen"   # a collaborator's Python agents drive the Lumen
  python Tools/run_sim.py --mode C --seed 7 --duration 600 --speed 20 --policy-file Saved/policy_servers.txt   # servers added/removed by editing that file while it runs
  python Tools/run_sim.py --mode C --seed 7 --duration 600 --speed 20 --control-file Saved/control.txt       # then: python Tools/control.py "drought=on" (docs/CONTROL_FILE.md)

Each run writes Saved/SymbioticWorld/<run_id>/ and the script prints the
directory when the process exits. -SWDuration makes the sim quit itself.
"""
import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UPROJECT = ROOT / "SymbioticWorld.uproject"
ENGINE = Path(r"C:\Program Files\Epic Games\UE_5.7")
EDITOR_CMD = ENGINE / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
EDITOR = ENGINE / "Engine/Binaries/Win64/UnrealEditor.exe"
SAVED = ROOT / "Saved/SymbioticWorld"
DEFAULT_RES = "1600x900"


def parse_res(spec):
    """'WxH' in pixels -> (W, H). Exits on anything else (a bad -ResX silently gives a default window)."""
    m = re.fullmatch(r"\s*(\d{3,5})\s*[xX*]\s*(\d{3,5})\s*", str(spec))
    if not m:
        sys.exit(f"--res: use WxH in pixels, e.g. 1920x1080 (got {spec!r})")
    return int(m.group(1)), int(m.group(2))


# Named -SWSet bundles. bank-cycle = the bank-alternating regrowth regime (DESIGN.md §4, "Bank cycle") with the
# world set up so residents of the switched-off bank can SEE food across the water: patches allowed at the banks,
# a Lumen sense range at which most patches see a far-bank patch (the "Bank cycle: N/34 type-0 patches" log line;
# 30-34 of 34 by seed), no off-bank residue and doubled active-bank regrowth. The first six settings are the
# preset as built on 2026-09-14; alone they FAIL the exit test (Lumen extinct on 2 of 5 seeds): the decaying home
# bank held residents for ~10 s and 36% of Lumen were within 60 s of MaxAge at a switch. bBankCycleHardOff and
# Lumen.MaxAge=250 (2026-09-18) pass the preregistered exit test on seeds 1-5 x 1800 s (Lumen lowest 28, 0 extinct,
# 59 crossings per switch), but the valley is crowded: Lumen reach ~170, Tecton ~125, and the shared MaxPopulation
# 220 is hit on 3 of 5 seeds. MaxTecton=30 and MaxLumen=150 (per-species caps, same day) pass the same test with
# the valley uncrowded (Lumen lowest 26, peak Lumen + Tecton 152-180, 48-70 crossings per switch). Tecton stay out
# of the cycle (scope ResourceA): in it they die out or push Lumen below 20 (four screens). NOT yet safe beyond 1800 s or
# in a drought: a 7200 s live run dipped to 17 Lumen and a drought took them to 1. See PROGRESS.md 2026-09-18.
PRESETS = {
    "bank-cycle": "Settings.bBankCycle=1;Settings.PatchDryMargin=0;Settings.PatchChannelClearance=0.6;"
                  "Lumen.SenseRange=8000;Settings.BankCycleOffCapacity=0;Settings.BankCycleOnRegen=2;"
                  "Settings.bBankCycleHardOff=1;Lumen.MaxAge=250;Settings.MaxTecton=30;Settings.MaxLumen=150",
}


def run_one(mode, seed, duration, speed, windowed, extra, set_spec=None, shots=None, no_logs=False, auto_select=False, cam=None, offscreen=False, stream=None,
            policy=None, policy_timeout=None, policy_share=None, policy_file=None, control_file=None, res=DEFAULT_RES,
            no_console=False):
    before = {p.name for p in SAVED.iterdir()} if SAVED.exists() else set()
    exe = EDITOR if windowed else EDITOR_CMD
    cmd = [str(exe), str(UPROJECT), "-game", "-unattended", "-nosound",
           f"-SWMode={mode}", f"-SWSeed={seed}", f"-SWDuration={duration}", f"-SWSpeed={speed}"]
    # "-log" opens the engine's console window. It is second nature while developing, but it floats
    # over everything and lands in a screen recording, so a take can leave it out; Saved/Logs is
    # written either way.
    if not no_console:
        cmd.insert(3, "-log")
    if not windowed:
        cmd += ["-nullrhi", "-NoSplash", "-stdout", "-FullStdOutLogOutput"]
    else:
        # Window size for a rendering run: the recorded take's frame (Game Bar records this window).
        width, height = parse_res(res)
        cmd += ["-windowed", f"-ResX={width}", f"-ResY={height}"]
        if offscreen:
            cmd.append("-RenderOffScreen")   # no window: nothing steals the keyboard, screenshots still land
        if stream:
            # Pixel Streaming 2: the sim connects to the signalling server as the streamer; viewers open its player page in a browser.
            cmd += [f"-PixelStreamingURL={stream}", "-PixelStreamingID=SymbioticWorld",
                    # The sim has no sound; the per-viewer audio tracks are pure risk in the media layer (see README, streaming).
                    "-ini:Game:[/Script/PixelStreaming2Settings.PixelStreaming2PluginSettings]:WebRTCDisableTransmitAudio=True",
                    "-ini:Game:[/Script/PixelStreaming2Settings.PixelStreaming2PluginSettings]:WebRTCDisableReceiveAudio=True"]
    if set_spec:
        cmd.append(f"-SWSet={set_spec}")
    if shots:
        # UE's FParse::Value stops at commas; the sim accepts ':' separated times.
        cmd.append(f"-SWShot={str(shots).replace(',', ':')}")
    if no_logs:
        cmd.append("-SWNoLogs=1")
    if auto_select:
        cmd.append("-SWAutoSelect=1")
    if cam:
        cmd.append(f"-SWCam={str(cam).replace(',', ':')}")
    if policy:
        # External policy servers (docs/POLICY_API.md): "host:port=Lumen|host:port=Tecton|host:port=Both".
        # ',' and ';' are not allowed (UE stops parsing at ',' and -SWSet owns ';').
        if "," in policy or ";" in policy:
            sys.exit("--policy: use '|' between servers and '=' before the species; ',' and ';' are not allowed")
        cmd.append(f"-SWPolicy={policy}")
    if policy_timeout is not None:
        cmd.append(f"-SWPolicyTimeoutMs={int(policy_timeout)}")
    if policy_share is not None:
        cmd.append(f"-SWPolicyShare={policy_share}")
    if policy_file is not None:
        # Server list file watched while the sim runs (docs/POLICY_API.md, "Adding servers while the sim runs").
        # Relative paths are resolved under the project directory by the sim; the default is Saved/policy_servers.txt.
        if "," in str(policy_file):
            sys.exit("--policy-file: the path must not contain ',' (UE stops parsing the value there)")
        cmd.append(f"-SWPolicyFile={policy_file}")
    if control_file is not None:
        # Live control file (docs/CONTROL_FILE.md): commands appended to it while the sim runs are executed once each.
        # Relative paths are resolved under the project directory by the sim; the default is Saved/control.txt.
        if "," in str(control_file):
            sys.exit("--control-file: the path must not contain ',' (UE stops parsing the value there)")
        cmd.append(f"-SWControlFile={control_file}")
    cmd += extra
    t0 = time.time()
    print(">>", " ".join(cmd), flush=True)
    proc = subprocess.run(cmd, cwd=str(ROOT))
    dt = time.time() - t0
    after = {p.name for p in SAVED.iterdir()} if SAVED.exists() else set()
    new = sorted(after - before)
    print(f"<< exit {proc.returncode} after {dt:.0f}s  new run dirs: {new}", flush=True)
    return [SAVED / n for n in new]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mode", nargs="+", default=["C"], help="A B C N (one or more)")
    ap.add_argument("--seed", nargs="+", type=int, default=[42])
    ap.add_argument("--duration", type=float, default=600, help="logical seconds before the sim quits itself")
    ap.add_argument("--speed", type=float, default=200, help="time scale (headless can go high)")
    ap.add_argument("--windowed", action="store_true", help="use the rendering editor exe with a window")
    ap.add_argument("--analyze", action="store_true", help="run Analysis/analyze_run.py on the new runs")
    ap.add_argument("--preset", choices=sorted(PRESETS), default=None,
                    help="named -SWSet bundle, prepended to --set: " + "; ".join(f"{k} = {v}" for k, v in PRESETS.items()))
    ap.add_argument("--set", dest="set_spec", default=None,
                    help='parameter overrides, e.g. "Settings.PatchRegenPerSec=5;Lumen.ReproThreshold=85"')
    ap.add_argument("--shot", default=None, help="comma-separated sim times for self-screenshots (windowed only)")
    ap.add_argument("--no-logs", action="store_true", help="do not write CSV logs")
    ap.add_argument("--auto-select", action="store_true", help="auto-select the youngest Lumen so screenshots show the inspector")
    ap.add_argument("--cam", default=None, help="start camera x,y,z,pitch,yaw (e.g. -3000,900,420,-8,10)")
    ap.add_argument("--res", default=DEFAULT_RES, metavar="WxH",
                    help=f"window size for --windowed / --offscreen runs (default {DEFAULT_RES}); Game Bar records that window")
    ap.add_argument("--offscreen", action="store_true", help="windowed run without a visible window (-RenderOffScreen); use for scripted screenshots")
    ap.add_argument("--no-console", action="store_true",
                    help="omit -log, so the engine's console window never floats over a recorded take (Saved/Logs still written)")
    ap.add_argument("--stream", nargs="?", const="ws://127.0.0.1:8888", default=None, metavar="WS_URL",
                    help="Pixel Streaming: connect to a signalling server (default ws://127.0.0.1:8888, start it with Tools/start_stream_server.bat) so LAN browsers can watch and drive the sim")
    ap.add_argument("--policy", default=None, metavar="SPEC",
                    help='external policy servers, e.g. "10.0.0.5:9000=Lumen|10.0.0.7:9000=Tecton" (Species: Lumen, Tecton, Both); see docs/POLICY_API.md')
    ap.add_argument("--policy-timeout", type=int, default=None, metavar="MS", help="ms to wait for a server's reply per substep (default 200); on timeout the built-in bandit decides")
    ap.add_argument("--policy-share", type=float, default=None, metavar="FRAC", help="fraction of a served species assigned to its server, decided per organism at birth (default 1.0)")
    ap.add_argument("--policy-file", default=None, metavar="PATH",
                    help="server list file polled while the sim runs (one host:port=Species per line, # comments; default Saved/policy_servers.txt under the project); edit it to add or remove servers without a restart")
    ap.add_argument("--control-file", default=None, metavar="PATH",
                    help="live control file polled every 2 s while the sim runs (default Saved/control.txt under the project; empty string disables); append lines with Tools/control.py, grammar in docs/CONTROL_FILE.md")
    ap.add_argument("extra", nargs="*", help="extra engine args (put them after --)")
    args = ap.parse_args()
    if args.preset:
        args.set_spec = PRESETS[args.preset] + (";" + args.set_spec if args.set_spec else "")

    parse_res(args.res)   # fail before launching anything
    if not EDITOR_CMD.exists():
        sys.exit(f"engine not found at {ENGINE}")
    produced = []
    for m in args.mode:
        for s in args.seed:
            produced += run_one(m.upper(), s, args.duration, args.speed, args.windowed, args.extra,
                                args.set_spec, args.shot, args.no_logs, args.auto_select, args.cam, args.offscreen, args.stream,
                                args.policy, args.policy_timeout, args.policy_share, args.policy_file, args.control_file,
                                args.res, args.no_console)
    if args.analyze and produced:
        subprocess.run([sys.executable, str(ROOT / "Analysis/analyze_run.py"), *map(str, produced)], cwd=str(ROOT))


if __name__ == "__main__":
    main()
