#!/usr/bin/env python
"""Offline analysis of Symbiotic World run logs.

Reads Saved/SymbioticWorld/<run_id>/{agents,births,deaths,population}.csv and
answers the three questions the hack must answer with data, not eyeballing:

  1. LIFETIME LEARNING  - does the SAME individual's learned Q table move
                          during its life?  (per-agent L1 drift, first vs last
                          logged row; ~0 under mode A, > 0 under B / C / N)
  2. INHERITANCE        - are child meta-parameters related-but-mutated copies
                          of the parent's?  (births.csv: corr + mean |delta|)
  3. SELECTION          - does mean alpha / epsilon shift across generations
                          under C in a way that N (neutral drift) does not?

Usage
  python Analysis/analyze_run.py <run_dir>                # one run: report + plots
  python Analysis/analyze_run.py <run_dir1> <run_dir2> ... # compare runs (e.g. C vs N seeds)
  python Analysis/analyze_run.py --root Saved/SymbioticWorld  # all runs under root

Outputs a text report to stdout and PNGs next to each run (or in --out).
"""
import argparse
import os
import sys
from pathlib import Path

import numpy as np
import pandas as pd

ACTIONS = ["forage", "explore", "follow", "avoid", "signal", "rest", "modify"]
BINS = ["low", "mid", "high"]


def q_columns(df):
    """Return the Q columns present. Supports both the all-bins schema
    (Q_low_forage ...) and the older current-bin-only schema (Q_forage ...)."""
    full = [f"Q_{b}_{a}" for b in BINS for a in ACTIONS]
    if all(c in df.columns for c in full):
        return full, True
    cur = [f"Q_{a}" for a in ACTIONS]
    return [c for c in cur if c in df.columns], False


def load_run(run_dir):
    run_dir = Path(run_dir)
    out = {"dir": run_dir, "name": run_dir.name}
    for f in ("agents", "births", "deaths", "population"):
        p = run_dir / f"{f}.csv"
        out[f] = pd.read_csv(p) if p.exists() and p.stat().st_size > 0 else pd.DataFrame()
    if not out["population"].empty:
        out["mode"] = out["population"]["mode"].iloc[0]
        out["seed"] = int(out["population"]["seed"].iloc[0])
    elif not out["agents"].empty:
        out["mode"] = out["agents"]["mode"].iloc[0]
        out["seed"] = int(out["agents"]["seed"].iloc[0])
    else:
        out["mode"], out["seed"] = "?", -1
    return out


# ---------------------------------------------------------------------------
# 1. Lifetime learning
# ---------------------------------------------------------------------------
def lifetime_learning(agents):
    """Per-agent L1 drift of the Q table between its first and last logged row.
    With the all-bins schema this is exact. With the current-bin-only schema it
    is a lower bound restricted to rows sharing the first row's bin."""
    if agents.empty:
        return pd.DataFrame()
    qcols, full = q_columns(agents)
    if not qcols:
        return pd.DataFrame()
    rows = []
    for (aid, sp), g in agents.groupby(["agent_id", "species"]):
        g = g.sort_values("sim_time")
        if len(g) < 2:
            continue
        if not full:
            g = g[g["energy_bin"] == g["energy_bin"].iloc[0]]
            if len(g) < 2:
                continue
        first, last = g.iloc[0][qcols].to_numpy(float), g.iloc[-1][qcols].to_numpy(float)
        rows.append({
            "agent_id": aid, "species": sp,
            "alpha": g["alpha"].iloc[0],
            "lifetime_logged_s": g["sim_time"].iloc[-1] - g["sim_time"].iloc[0],
            "decisions": int(g["decisions"].iloc[-1]),
            "q_drift_l1": float(np.abs(last - first).sum()),
            "q_drift_max": float(np.abs(last - first).max()),
            "greedy_changed": int(np.argmax(first[:7]) != np.argmax(last[:7])),
        })
    return pd.DataFrame(rows)


# ---------------------------------------------------------------------------
# 2. Inheritance
# ---------------------------------------------------------------------------
def inheritance(births):
    if births.empty:
        return {}
    out = {"n_births": len(births)}
    for p in ("alpha", "epsilon", "social", "env_effect"):
        if f"parent_{p}" not in births.columns:
            continue
        c, ch = births[f"parent_{p}"], births[f"child_{p}"]
        out[f"{p}_corr"] = float(np.corrcoef(c, ch)[0, 1]) if c.std() > 0 and ch.std() > 0 else float("nan")
        out[f"{p}_mean_abs_delta"] = float((ch - c).abs().mean())
    return out


# ---------------------------------------------------------------------------
# 3. Selection across generations
# ---------------------------------------------------------------------------
def per_generation(births, species="Lumen"):
    if births.empty:
        return pd.DataFrame()
    b = births[births["species"] == species]
    cols = [c for c in ("child_alpha", "child_epsilon", "child_social", "child_env_effect") if c in b.columns]
    return (b.groupby("child_generation")[cols].agg(["mean", "std", "count"]))


def population_trajectory(pop, species="Lumen"):
    if pop.empty:
        return pd.DataFrame()
    return pop[pop["species"] == species].sort_values("sim_time")


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
def report_run(run, plots=True, out_dir=None):
    name, mode, seed = run["name"], run["mode"], run["seed"]
    print("=" * 78)
    print(f"RUN {name}   mode={mode}   seed={seed}")
    pop = run["population"]
    if not pop.empty:
        t_end = pop["sim_time"].max()
        last = pop[pop["sim_time"] == t_end]
        print(f"  sim time {t_end:.0f} s   births {int(last['births'].iloc[0])}   deaths {int(last['deaths'].iloc[0])}")
        for sp in ("Lumen", "Tecton"):
            r = last[last["species"] == sp]
            if r.empty:
                continue
            r = r.iloc[0]
            print(f"  {sp:6s} n={int(r['n']):3d}  gen mean {r['mean_generation']:.1f} max {int(r['max_generation'])}  "
                  f"alpha {r['mean_alpha']:.3f}+/-{r['sd_alpha']:.3f}  eps {r['mean_epsilon']:.3f}+/-{r['sd_epsilon']:.3f}  "
                  f"social {r['mean_social']:.2f}")

    ll = lifetime_learning(run["agents"])
    if not ll.empty:
        print("  LIFETIME LEARNING (Q drift first->last row per agent)")
        for sp, g in ll.groupby("species"):
            g2 = g[g["decisions"] >= 20]
            if g2.empty:
                continue
            print(f"    {sp:6s} agents={len(g2):3d}  median L1 drift {g2['q_drift_l1'].median():.3f}  "
                  f"median max|dQ| {g2['q_drift_max'].median():.3f}  greedy action changed in {100*g2['greedy_changed'].mean():.0f}%")
    inh = inheritance(run["births"])
    if inh:
        print(f"  INHERITANCE  births={inh['n_births']}  "
              + "  ".join(f"{p}: corr {inh[f'{p}_corr']:.2f} |d| {inh[f'{p}_mean_abs_delta']:.3f}" for p in ("alpha", "epsilon", "social", "env_effect") if f"{p}_corr" in inh))
    pg = per_generation(run["births"])
    if not pg.empty:
        print("  LUMEN META-PARAMETERS BY GENERATION (children born into each generation)")
        a = pg["child_alpha"]; e = pg["child_epsilon"]
        v = pg["child_env_effect"] if "child_env_effect" in pg.columns.get_level_values(0) else None
        for gen in pg.index:
            extra = f"  e {v.loc[gen, 'mean']:.3f}" if v is not None else ""
            print(f"    gen {int(gen):2d}  n={int(a.loc[gen, 'count']):3d}  alpha {a.loc[gen, 'mean']:.3f}  eps {e.loc[gen, 'mean']:.3f}{extra}")

    if plots:
        try:
            plot_run(run, ll, out_dir)
        except Exception as ex:  # plotting must never kill the report
            print(f"  (plot skipped: {ex})")
    return {"name": name, "mode": mode, "seed": seed, "lifetime": ll, "inheritance": inh, "per_gen": pg, "pop": pop}


def plot_run(run, ll, out_dir=None):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    out_dir = Path(out_dir) if out_dir else run["dir"]
    out_dir.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(2, 2, figsize=(12, 8))
    fig.suptitle(f"{run['name']}  mode={run['mode']} seed={run['seed']}")

    pop = run["population"]
    ax = axes[0, 0]
    for sp, c in (("Lumen", "#3fd1ff"), ("Tecton", "#ff9d2e")):
        p = population_trajectory(pop, sp)
        if not p.empty:
            ax.plot(p["sim_time"], p["n"], color=c, label=sp)
    if not pop.empty and "drought_state" in pop:
        d = pop[pop["drought_state"] == 1]
        if not d.empty:
            ax.axvspan(d["sim_time"].min(), d["sim_time"].max(), color="#ff4d3d", alpha=0.15, label="drought")
    ax.set_title("population"); ax.set_xlabel("sim time (s)"); ax.legend()

    ax = axes[0, 1]
    for sp, c in (("Lumen", "#3fd1ff"), ("Tecton", "#ff9d2e")):
        p = population_trajectory(pop, sp)
        if not p.empty:
            ax.plot(p["sim_time"], p["mean_alpha"], color=c, label=f"{sp} alpha")
            ax.fill_between(p["sim_time"], p["mean_alpha"] - p["sd_alpha"], p["mean_alpha"] + p["sd_alpha"], color=c, alpha=0.15)
            ax.plot(p["sim_time"], p["mean_epsilon"], color=c, ls="--", label=f"{sp} eps")
    ax.set_title("inherited meta-parameters (mean +/- sd)"); ax.set_xlabel("sim time (s)"); ax.legend(fontsize=8)

    ax = axes[1, 0]
    if not ll.empty:
        for sp, c in (("Lumen", "#3fd1ff"), ("Tecton", "#ff9d2e")):
            g = ll[(ll["species"] == sp) & (ll["decisions"] >= 20)]
            if not g.empty:
                ax.hist(g["q_drift_l1"], bins=25, color=c, alpha=0.6, label=sp)
    ax.set_title("lifetime learning: per-agent L1 drift of Q table"); ax.set_xlabel("sum |Q_last - Q_first|"); ax.legend()

    ax = axes[1, 1]
    pg = per_generation(run["births"])
    if not pg.empty:
        gens = pg.index.to_numpy()
        ax.errorbar(gens, pg["child_alpha"]["mean"], yerr=pg["child_alpha"]["std"].fillna(0), color="#3fd1ff", marker="o", label="alpha")
        ax.errorbar(gens, pg["child_epsilon"]["mean"], yerr=pg["child_epsilon"]["std"].fillna(0), color="#3fd1ff", ls="--", marker="s", label="epsilon")
    ax.set_title("Lumen children by generation"); ax.set_xlabel("generation"); ax.legend()

    fig.tight_layout()
    path = out_dir / f"{run['name']}_summary.png"
    fig.savefig(path, dpi=110)
    plt.close(fig)
    print(f"  plot -> {path}")


def compare(results):
    """C vs N: is the end-of-run mean alpha under C separable from drift?"""
    by_mode = {}
    for r in results:
        pop = r["pop"]
        if pop.empty:
            continue
        lum = population_trajectory(pop, "Lumen")
        if lum.empty:
            continue
        by_mode.setdefault(r["mode"], []).append((r["seed"], float(lum["mean_alpha"].iloc[-1]), float(lum["mean_epsilon"].iloc[-1]),
                                                 float(lum["mean_alpha"].iloc[0])))
    if not by_mode:
        return
    print("=" * 78)
    print("CROSS-RUN COMPARISON (Lumen, end of run)")
    for m, rows in sorted(by_mode.items()):
        a = np.array([r[1] for r in rows]); e = np.array([r[2] for r in rows]); a0 = np.array([r[3] for r in rows])
        print(f"  {m:22s} runs={len(rows)}  alpha0 {a0.mean():.3f} -> alpha_end {a.mean():.3f} (sd {a.std(ddof=0):.3f})  eps_end {e.mean():.3f}")
    c = by_mode.get("C_learning_evolution"); n = by_mode.get("N_neutral_control")
    if c and n and len(c) >= 2 and len(n) >= 2:
        try:
            from scipy import stats
            t, p = stats.ttest_ind([r[1] for r in c], [r[1] for r in n], equal_var=False)
            print(f"  Welch t-test  alpha_end  C vs N:  t={t:.2f}  p={p:.3f}   "
                  + ("SELECTION on alpha distinguishable from drift" if p < 0.05 else "NOT distinguishable from drift with these seeds"))
        except ImportError:
            print("  (scipy not available for t-test)")
    elif c and n:
        print("  need >= 2 seeds per mode (C and N) for the selection-vs-drift test")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs", nargs="*", help="run directories")
    ap.add_argument("--root", help="analyze every run directory under this root")
    ap.add_argument("--out", help="directory for plots (default: the run dir)")
    ap.add_argument("--no-plots", action="store_true")
    args = ap.parse_args()

    dirs = [Path(r) for r in args.runs]
    if args.root:
        dirs += sorted(p for p in Path(args.root).iterdir() if p.is_dir() and (p / "population.csv").exists())
    if not dirs:
        ap.error("give run directories or --root")

    results = [report_run(load_run(d), plots=not args.no_plots, out_dir=args.out) for d in dirs]
    if len(results) > 1:
        compare(results)


if __name__ == "__main__":
    main()
