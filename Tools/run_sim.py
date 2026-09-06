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
  python Tools/run_sim.py --mode C --seed 7 --duration 600 --speed 20 --policy "10.228.152.5:9000=Lumen"   # a collaborator's Python agents drive the Lumen
  python Tools/run_sim.py --mode C --seed 7 --duration 600 --speed 20 --policy-file Saved/policy_servers.txt   # servers added/removed by editing that file while it runs

Each run writes Saved/SymbioticWorld/<run_id>/ and the script prints the
directory when the process exits. -SWDuration makes the sim quit itself.
"""
import argparse
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


def run_one(mode, seed, duration, speed, windowed, extra, set_spec=None, shots=None, no_logs=False, auto_select=False, cam=None, offscreen=False, stream=None,
            policy=None, policy_timeout=None, policy_share=None, policy_file=None):
    before = {p.name for p in SAVED.iterdir()} if SAVED.exists() else set()
    exe = EDITOR if windowed else EDITOR_CMD
    cmd = [str(exe), str(UPROJECT), "-game", "-log", "-unattended", "-nosound",
           f"-SWMode={mode}", f"-SWSeed={seed}", f"-SWDuration={duration}", f"-SWSpeed={speed}"]
    if not windowed:
        cmd += ["-nullrhi", "-NoSplash", "-stdout", "-FullStdOutLogOutput"]
    else:
        cmd += ["-windowed", "-ResX=1600", "-ResY=900"]
        if offscreen:
            cmd.append("-RenderOffScreen")   # no window: nothing steals the keyboard, screenshots still land
        if stream:
            # Pixel Streaming 2: the sim connects to the signalling server as the streamer; viewers open its player page in a browser.
            cmd += [f"-PixelStreamingURL={stream}", "-PixelStreamingID=SymbioticWorld"]
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
    ap.add_argument("--set", dest="set_spec", default=None,
                    help='parameter overrides, e.g. "Settings.PatchRegenPerSec=5;Lumen.ReproThreshold=85"')
    ap.add_argument("--shot", default=None, help="comma-separated sim times for self-screenshots (windowed only)")
    ap.add_argument("--no-logs", action="store_true", help="do not write CSV logs")
    ap.add_argument("--auto-select", action="store_true", help="auto-select the youngest Lumen so screenshots show the inspector")
    ap.add_argument("--cam", default=None, help="start camera x,y,z,pitch,yaw (e.g. -3000,900,420,-8,10)")
    ap.add_argument("--offscreen", action="store_true", help="windowed run without a visible window (-RenderOffScreen); use for scripted screenshots")
    ap.add_argument("--stream", nargs="?", const="ws://127.0.0.1:8888", default=None, metavar="WS_URL",
                    help="Pixel Streaming: connect to a signalling server (default ws://127.0.0.1:8888, start it with Tools/start_stream_server.bat) so LAN browsers can watch and drive the sim")
    ap.add_argument("--policy", default=None, metavar="SPEC",
                    help='external policy servers, e.g. "10.0.0.5:9000=Lumen|10.0.0.7:9000=Tecton" (Species: Lumen, Tecton, Both); see docs/POLICY_API.md')
    ap.add_argument("--policy-timeout", type=int, default=None, metavar="MS", help="ms to wait for a server's reply per substep (default 200); on timeout the built-in bandit decides")
    ap.add_argument("--policy-share", type=float, default=None, metavar="FRAC", help="fraction of a served species assigned to its server, decided per organism at birth (default 1.0)")
    ap.add_argument("--policy-file", default=None, metavar="PATH",
                    help="server list file polled while the sim runs (one host:port=Species per line, # comments; default Saved/policy_servers.txt under the project); edit it to add or remove servers without a restart")
    ap.add_argument("extra", nargs="*", help="extra engine args (put them after --)")
    args = ap.parse_args()

    if not EDITOR_CMD.exists():
        sys.exit(f"engine not found at {ENGINE}")
    produced = []
    for m in args.mode:
        for s in args.seed:
            produced += run_one(m.upper(), s, args.duration, args.speed, args.windowed, args.extra,
                                args.set_spec, args.shot, args.no_logs, args.auto_select, args.cam, args.offscreen, args.stream,
                                args.policy, args.policy_timeout, args.policy_share, args.policy_file)
    if args.analyze and produced:
        subprocess.run([sys.executable, str(ROOT / "Analysis/analyze_run.py"), *map(str, produced)], cwd=str(ROOT))


if __name__ == "__main__":
    main()
