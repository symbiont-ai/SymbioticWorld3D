"""Vega, the data scientist (non-voting).

Produces graphs and trend forecasts from run telemetry for the humans reading
the lab report. Quarantine is structural, not polite: this module runs after
the meetings, writes only PNGs + the forecasts table + report text, and
nothing here mints evidence, touches stances, notebooks, credibility, or any
table the scientists read. The scientists never see Vega's outputs.

All fitting is code (least squares on the run's own time series); the LLM is
not involved at all — captions are templated.
"""
import sys
from pathlib import Path

import numpy as np

from . import config, db

sys.path.insert(0, str(config.ANALYSIS_DIR))
import analyze_run  # noqa: E402

FORECAST_HORIZON_FACTOR = 1.5   # forecast to 1.5x the run's logged duration
FIT_TAIL = 0.5                  # fit the trend on the last half of the series


def _fit_forecast(t, y, horizon):
    """OLS on the tail; band = 2x residual std. Returns (pred, lo, hi)."""
    n0 = int(len(t) * (1 - FIT_TAIL))
    tt, yy = np.asarray(t[n0:], float), np.asarray(y[n0:], float)
    if len(tt) < 4 or np.ptp(tt) == 0:
        return None
    slope, intercept = np.polyfit(tt, yy, 1)
    resid = yy - (slope * tt + intercept)
    band = 2.0 * float(resid.std()) if len(resid) > 2 else 0.0
    pred = float(slope * horizon + intercept)
    return pred, pred - band, pred + band


def forecast_run(con, run_row):
    """Store population + genome forecasts for one run."""
    run = analyze_run.load_run(run_row["path"])
    pop = run["population"]
    if pop.empty:
        return []
    made = []
    t_end = float(pop["sim_time"].max())
    horizon = t_end * FORECAST_HORIZON_FACTOR
    for species in ("Lumen", "Tecton"):
        p = analyze_run.population_trajectory(pop, species)
        if p.empty:
            continue
        for metric, col in ((f"{species.lower()}_n", "n"),
                            (f"{species.lower()}_mean_alpha", "mean_alpha")):
            fc = _fit_forecast(p["sim_time"].to_numpy(), p[col].to_numpy(), horizon)
            if fc is None:
                continue
            exists = con.execute(
                "SELECT 1 FROM forecasts WHERE run_id=? AND metric=? AND horizon=?",
                (run_row["run_id"], metric, horizon)).fetchone()
            if exists:
                continue
            con.execute(
                "INSERT INTO forecasts(run_id, metric, horizon, predicted, lower, upper,"
                " actual, created_at) VALUES(?,?,?,?,?,?,NULL,?)",
                (run_row["run_id"], metric, horizon, fc[0], fc[1], fc[2], db.now()))
            made.append((run_row["run_id"], metric, horizon, fc))
    con.commit()
    return made


def update_actuals(con):
    """Fill in actuals for old forecasts when a longer run of the same mode and
    seed has since been ingested (forecasts are falsifiable too)."""
    filled = 0
    for f in con.execute("SELECT rowid, * FROM forecasts WHERE actual IS NULL").fetchall():
        src = con.execute("SELECT mode, seed FROM runs WHERE run_id=?", (f["run_id"],)).fetchone()
        if not src:
            continue
        longer = con.execute(
            "SELECT * FROM runs WHERE mode=? AND seed=? AND sim_end>=? AND run_id != ?",
            (src["mode"], src["seed"], f["horizon"], f["run_id"])).fetchone()
        if not longer:
            continue
        run = analyze_run.load_run(longer["path"])
        species = "Lumen" if f["metric"].startswith("lumen") else "Tecton"
        p = analyze_run.population_trajectory(run["population"], species)
        if p.empty:
            continue
        col = "n" if f["metric"].endswith("_n") else "mean_alpha"
        idx = (p["sim_time"] - f["horizon"]).abs().idxmin()
        con.execute("UPDATE forecasts SET actual=? WHERE rowid=?",
                    (float(p.loc[idx, col]), f["rowid"]))
        filled += 1
    con.commit()
    return filled


def _plot_runs(con, fig_dir, label):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    runs = con.execute("SELECT * FROM runs ORDER BY ingested_at").fetchall()
    if not runs:
        return []
    fig_dir.mkdir(parents=True, exist_ok=True)
    images = []

    # 1. population trajectories, all runs
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.2))
    for ax, (species, cmap) in zip(axes, (("Lumen", "winter"), ("Tecton", "autumn"))):
        for i, r in enumerate(runs):
            run = analyze_run.load_run(r["path"])
            p = analyze_run.population_trajectory(run["population"], species)
            if p.empty:
                continue
            color = plt.get_cmap(cmap)(0.15 + 0.7 * i / max(1, len(runs) - 1))
            ax.plot(p["sim_time"], p["n"], color=color, lw=1.2,
                    label=f"{r['mode'][:1]} s{r['seed']}")
        ax.set_title(f"{species} population, all ingested runs")
        ax.set_xlabel("sim time (s)"); ax.set_ylabel("n")
        ax.legend(fontsize=7, ncol=2)
    fig.tight_layout()
    path = fig_dir / f"vega_population_{label}.png"
    fig.savefig(path, dpi=110); plt.close(fig)
    images.append(path)

    # 2. mean alpha trajectories by mode (the selection story at a glance)
    fig, ax = plt.subplots(figsize=(7, 4.2))
    palette = {"C_learning_evolution": "#3fd1ff", "N_neutral_control": "#999999",
               "B_learning_on": "#7bd88f", "A_learning_off": "#d88f7b"}
    for r in runs:
        run = analyze_run.load_run(r["path"])
        p = analyze_run.population_trajectory(run["population"], "Lumen")
        if p.empty:
            continue
        ax.plot(p["sim_time"], p["mean_alpha"],
                color=palette.get(r["mode"], "#cccccc"), lw=1.2, alpha=0.85)
    for mode, c in palette.items():
        if any(r["mode"] == mode for r in runs):
            ax.plot([], [], color=c, label=mode)
    ax.set_title("Lumen mean alpha by mode (each line one run)")
    ax.set_xlabel("sim time (s)"); ax.set_ylabel("mean alpha"); ax.legend(fontsize=8)
    fig.tight_layout()
    path = fig_dir / f"vega_alpha_by_mode_{label}.png"
    fig.savefig(path, dpi=110); plt.close(fig)
    images.append(path)
    return images


def annex(con, label):
    """Build the data annex: figures + forecasts. Returns markdown lines for
    the report (paths relative to Lab/reports/)."""
    for r in con.execute("SELECT * FROM runs").fetchall():
        forecast_run(con, r)
    filled = update_actuals(con)

    fig_dir = config.REPORT_DIR / "figs"
    try:
        images = _plot_runs(con, fig_dir, label)
    except Exception as ex:          # plotting must never block the report
        images = []
        print(f"  [vega] plots skipped: {ex}")

    lines = ["## Data annex — Vega (non-voting)",
             "",
             "_Charts and trend forecasts computed directly from the run telemetry. "
             "This annex is for the reader; the scientists neither see nor cite it._",
             ""]
    for img in images:
        lines.append(f"![{img.stem}](figs/{img.name})")
    if images:
        lines.append("")

    fcs = con.execute("SELECT * FROM forecasts ORDER BY run_id, metric").fetchall()
    if fcs:
        lines += ["| run | metric | horizon (s) | forecast | 95% band | actual |",
                  "|---|---|---|---|---|---|"]
        for f in fcs:
            actual = f"{f['actual']:.3g}" if f["actual"] is not None else "—"
            lines.append(f"| {f['run_id']} | {f['metric']} | {f['horizon']:.0f} | "
                         f"{f['predicted']:.3g} | [{f['lower']:.3g}, {f['upper']:.3g}] "
                         f"| {actual} |")
        lines.append("")
        if filled:
            lines.append(f"_{filled} earlier forecast(s) verified against longer runs._")
            lines.append("")
    return lines
