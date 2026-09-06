"""Watcher (PRD F1): parse run CSVs into SQLite and mint citable evidence
objects with stable IDs. Stats come from Analysis/analyze_run.py imported as a
library — never reimplemented. Agents may only cite evidence IDs."""
import sys

import numpy as np

from . import config, db

sys.path.insert(0, str(config.ANALYSIS_DIR))
import analyze_run  # noqa: E402  (the repo's own analysis library)


def _prov(run, desc):
    return f"{desc} [run {run['name']}, mode {run['mode']}, seed {run['seed']}]"


def compute_run_stats(run):
    """Scalar, citable stats for one loaded run. Returns [(stat, value, provenance)]."""
    out = []
    pop = run["population"]
    if not pop.empty:
        t_end = pop["sim_time"].max()
        out.append(("sim_end", float(t_end), _prov(run, "last logged sim_time")))
        for sp in ("Lumen", "Tecton"):
            p = analyze_run.population_trajectory(pop, sp)
            if p.empty:
                continue
            s = sp.lower()
            out += [
                (f"{s}_end_n", float(p["n"].iloc[-1]), _prov(run, f"{sp} population at end of run")),
                (f"{s}_min_n", float(p["n"].min()), _prov(run, f"{sp} population minimum over run")),
                (f"{s}_end_mean_alpha", float(p["mean_alpha"].iloc[-1]), _prov(run, f"{sp} mean alpha at end")),
                (f"{s}_end_mean_epsilon", float(p["mean_epsilon"].iloc[-1]), _prov(run, f"{sp} mean epsilon at end")),
                (f"{s}_start_mean_alpha", float(p["mean_alpha"].iloc[0]), _prov(run, f"{sp} mean alpha at start")),
                (f"{s}_max_generation", float(p["max_generation"].max()), _prov(run, f"{sp} max generation reached")),
            ]
            if "mean_env_effect" in p.columns:
                out.append((f"{s}_end_mean_env_effect", float(p["mean_env_effect"].iloc[-1]),
                            _prov(run, f"{sp} mean env-effect e at end")))
        last = pop[pop["sim_time"] == t_end]
        out += [
            ("total_births", float(last["births"].iloc[0]), _prov(run, "cumulative births at end")),
            ("total_deaths", float(last["deaths"].iloc[0]), _prov(run, "cumulative deaths at end")),
        ]
        for res, col in (("resource_A_end", "resource_A"), ("resource_B_end", "resource_B")):
            if col in last.columns:
                out.append((res, float(last[col].iloc[0]),
                            _prov(run, f"total {col} stock at end of run")))
        if "trace_Y_mean" in pop.columns:
            out.append(("trace_y_end_mean", float(pop["trace_Y_mean"].iloc[-1]),
                        _prov(run, "Trace Y field mean at end")))
        if "drought_state" in pop.columns:
            out.append(("drought_fraction", float((pop["drought_state"] == 1).mean()),
                        _prov(run, "fraction of logged time under drought")))

    ll = analyze_run.lifetime_learning(run["agents"])
    if not ll.empty:
        for sp in ("Lumen", "Tecton"):
            g = ll[(ll["species"] == sp) & (ll["decisions"] >= 20)]
            if g.empty:
                continue
            s = sp.lower()
            out += [
                (f"{s}_q_drift_median", float(g["q_drift_l1"].median()),
                 _prov(run, f"{sp} median L1 Q-table drift first->last row, decisions>=20, n={len(g)}")),
                (f"{s}_greedy_changed_pct", float(100 * g["greedy_changed"].mean()),
                 _prov(run, f"{sp} % of agents whose greedy action changed over life")),
            ]

    inh = analyze_run.inheritance(run["births"])
    for p in ("alpha", "epsilon", "social", "env_effect"):
        if f"{p}_corr" in inh and np.isfinite(inh[f"{p}_corr"]):
            out.append((f"inherit_{p}_corr", float(inh[f"{p}_corr"]),
                        _prov(run, f"parent-child correlation of {p}, n={inh['n_births']} births")))

    pg = analyze_run.per_generation(run["births"])
    if not pg.empty and len(pg.index) >= 3:
        gens = pg.index.to_numpy(float)
        for p in ("child_alpha", "child_epsilon", "child_env_effect"):
            if p in pg.columns.get_level_values(0):
                means = pg[p]["mean"].to_numpy(float)
                ok = np.isfinite(means)
                if ok.sum() >= 3:
                    slope = float(np.polyfit(gens[ok], means[ok], 1)[0])
                    out.append((f"{p.replace('child_', '')}_gen_slope", slope,
                                _prov(run, f"OLS slope of mean {p.replace('child_', '')} per generation")))
    return out


def ingest_run(con, run_dir):
    """Load one run directory; returns list of new evidence IDs (empty if seen)."""
    run = analyze_run.load_run(run_dir)
    if run["population"].empty and run["agents"].empty:
        return []
    rid = run["name"]
    if con.execute("SELECT 1 FROM runs WHERE run_id=?", (rid,)).fetchone():
        return []
    sim_end = float(run["population"]["sim_time"].max()) if not run["population"].empty else 0.0
    con.execute("INSERT INTO runs VALUES(?,?,?,?,?,?)",
                (rid, run["mode"], run["seed"], sim_end, str(run_dir), db.now()))
    eids = []
    for stat, value, prov in compute_run_stats(run):
        eids.append(db.add_evidence(con, rid, stat, value, prov))
    con.commit()
    return eids


def ingest_all(con, root=None):
    root = root or config.SAVED
    new = []
    if root.exists():
        for d in sorted(p for p in root.iterdir() if p.is_dir() and (p / "population.csv").exists()):
            new += ingest_run(con, d)
    return new


def cross_run_stats(con):
    """Cross-run, code-computed comparisons (e.g. the C-vs-N Welch test).
    Minted as evidence attached to run_id='cross'."""
    import pandas as pd
    rows = con.execute(
        "SELECT e.run_id, e.stat, e.value, r.mode FROM evidence e JOIN runs r ON e.run_id=r.run_id "
        "WHERE e.stat='lumen_end_mean_alpha'").fetchall()
    if not rows:
        return []
    df = pd.DataFrame([dict(r) for r in rows])
    c = df[df["mode"] == "C_learning_evolution"]["value"]
    n = df[df["mode"] == "N_neutral_control"]["value"]
    out = []
    if len(c) >= 2 and len(n) >= 2:
        from scipy import stats
        t, p = stats.ttest_ind(c, n, equal_var=False)
        eid = db.add_evidence(con, "cross", "welch_alpha_C_vs_N_p", float(p),
                              f"Welch t on end-run Lumen mean alpha, C (n={len(c)}) vs N (n={len(n)}), t={t:.2f}")
        out.append(eid)
        con.commit()
    return out


def evidence_digest(con, limit=40):
    """Compact evidence listing given to agents in context. Newest first."""
    rows = con.execute(
        "SELECT id, run_id, stat, value, provenance FROM evidence "
        "ORDER BY created_at DESC, id DESC LIMIT ?", (limit,)).fetchall()
    return [f"{r['id']}: {r['stat']}={r['value']:.4g} — {r['provenance']}" for r in rows]
