#!/usr/bin/env python
"""Parameter sweep for balance / research runs. No recompiles: every variant
is a -SWSet override string.

  python Tools/sweep.py --mode C --seed 1 2 --duration 600 \
      --set "Settings.PatchRegenPerSec=1.6" \
      --set "Settings.PatchRegenPerSec=4" \
      --set "Settings.PatchRegenPerSec=6;Lumen.ReproThreshold=85"

  python Tools/sweep.py --file sweeps/regen.txt --mode C --seed 1 2 3

--file: one -SWSet spec per line (blank lines and # comments ignored).
An empty spec ("") is the baseline.

Prints one row per (spec, seed): min / final population per species, births,
starvation deaths, end-of-run mean alpha and epsilon for Lumen, and whether
either species went extinct. Also writes sweep_<timestamp>.csv in Saved/.
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
RUN_SIM = ROOT / "Tools/run_sim.py"
SAVED = ROOT / "Saved/SymbioticWorld"


def run(mode, seed, duration, speed, spec):
    before = {p.name for p in SAVED.iterdir()} if SAVED.exists() else set()
    cmd = [sys.executable, str(RUN_SIM), "--mode", mode, "--seed", str(seed),
           "--duration", str(duration), "--speed", str(speed)]
    if spec:
        cmd += ["--set", spec]
    subprocess.run(cmd, cwd=str(ROOT), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    after = {p.name for p in SAVED.iterdir()} if SAVED.exists() else set()
    new = sorted(after - before)
    return SAVED / new[-1] if new else None


def summarize(run_dir, mode, seed, spec):
    pop = pd.read_csv(run_dir / "population.csv")
    deaths_p = run_dir / "deaths.csv"
    deaths = pd.read_csv(deaths_p) if deaths_p.exists() and deaths_p.stat().st_size > 0 else pd.DataFrame()
    row = {"spec": spec or "(baseline)", "mode": mode, "seed": seed, "run": run_dir.name,
           "t_end": round(pop["sim_time"].max(), 1)}
    for sp, tag in (("Lumen", "L"), ("Tecton", "T")):
        p = pop[pop["species"] == sp].sort_values("sim_time")
        if p.empty:
            continue
        row[f"{tag}_min"] = int(p["n"].min())
        row[f"{tag}_final"] = int(p["n"].iloc[-1])
        row[f"{tag}_maxgen"] = int(p["max_generation"].max())
        row[f"{tag}_alpha_end"] = round(float(p["mean_alpha"].iloc[-1]), 3)
        row[f"{tag}_eps_end"] = round(float(p["mean_epsilon"].iloc[-1]), 3)
        row[f"{tag}_extinct"] = int(p["n"].iloc[-1] == 0)
        if not deaths.empty:
            d = deaths[deaths["species"] == sp]
            row[f"{tag}_starved"] = int((d["cause"] == "starvation").sum())
            row[f"{tag}_aged"] = int((d["cause"] == "age").sum())
    last = pop.iloc[-1]
    row["births"] = int(last["births"]); row["deaths"] = int(last["deaths"])
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--set", dest="specs", action="append", default=[], help="a -SWSet spec (repeatable)")
    ap.add_argument("--file", help="file with one spec per line")
    ap.add_argument("--mode", nargs="+", default=["C"])
    ap.add_argument("--seed", nargs="+", type=int, default=[1])
    ap.add_argument("--duration", type=float, default=600)
    ap.add_argument("--speed", type=float, default=200)
    ap.add_argument("--baseline", action="store_true", help="also run with no overrides")
    args = ap.parse_args()

    specs = list(args.specs)
    if args.file:
        for line in Path(args.file).read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                specs.append(line)
    if args.baseline or not specs:
        specs.insert(0, "")

    rows = []
    total = len(specs) * len(args.mode) * len(args.seed)
    i = 0
    for spec in specs:
        for mode in args.mode:
            for seed in args.seed:
                i += 1
                t0 = time.time()
                rd = run(mode.upper(), seed, args.duration, args.speed, spec)
                if rd is None:
                    print(f"[{i}/{total}] {spec or '(baseline)'} {mode} seed {seed}: NO RUN DIR (crash?)", flush=True)
                    continue
                row = summarize(rd, mode.upper(), seed, spec)
                rows.append(row)
                print(f"[{i}/{total}] {time.time()-t0:4.0f}s  {spec or '(baseline)':55s} {mode} s{seed}  "
                      f"L {row.get('L_min','-')}->{row.get('L_final','-')} (starved {row.get('L_starved','-')})  "
                      f"T {row.get('T_min','-')}->{row.get('T_final','-')}  gen {row.get('L_maxgen','-')}  "
                      f"alpha {row.get('L_alpha_end','-')}", flush=True)

    if rows:
        df = pd.DataFrame(rows)
        out = ROOT / "Saved" / f"sweep_{time.strftime('%Y%m%d-%H%M%S')}.csv"
        df.to_csv(out, index=False)
        print("\n" + df.drop(columns=["run"]).to_string(index=False))
        print(f"\n-> {out}")


if __name__ == "__main__":
    main()
