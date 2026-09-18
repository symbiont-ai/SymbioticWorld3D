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

    # Death causes (deaths.csv 'cause': starvation / age / predation since the
    # Leviathan merge). Data-driven: minted only when the column exists, so
    # pre-predator runs ingest unchanged. No interpretation here — the counts
    # are the observable; what they mean is the scientists' argument to have.
    dd = run.get("deaths")
    if dd is not None and not dd.empty and "cause" in dd.columns and "species" in dd.columns:
        for sp in ("Lumen", "Tecton"):
            g = dd[dd["species"] == sp]
            if g.empty:
                continue
            s = sp.lower()
            n_pred = int((g["cause"] == "predation").sum())
            n_starve = int((g["cause"] == "starvation").sum())
            out += [
                (f"{s}_deaths_predation", float(n_pred),
                 _prov(run, f"{sp} deaths by predation, of {len(g)} {sp} deaths")),
                (f"{s}_deaths_starvation", float(n_starve),
                 _prov(run, f"{sp} deaths by starvation, of {len(g)} {sp} deaths")),
                (f"{s}_predation_frac", n_pred / len(g),
                 _prov(run, f"share of {sp} deaths caused by predation")),
            ]

    # Tail and performance statistics on the agent snapshots.
    #
    # A trait whose selection is one-sided is invisible to a mean. Measured on 2026-09-18 over 16
    # runs and 6585 Lumen: epsilon below 0.15 costs 12.6% of reproduction, epsilon above it is
    # flat, and the population mean therefore wanders by one mutation step between seeds (caveat
    # H-028). The tenth percentile and the low-tail share carry what the mean cannot.
    #
    # mean_reward is the bandit's own performance per decision. It is the one measure here that is
    # NOT downstream of reproduction: energy statistics invert the fitness ordering, because a
    # frequent reproducer is repeatedly knocked down by ReproCost and because the population cap
    # blocks reproduction at the top of a cycle, leaving rich agents queued above the threshold.
    ag = run.get("agents")
    if ag is not None and not ag.empty and "epsilon" in ag.columns:
        t_last = ag["sim_time"].max()
        final = ag[ag["sim_time"] == t_last]
        for sp in ("Lumen", "Tecton"):
            s_ = sp.lower()
            g = final[final["species"] == sp]
            if len(g) >= 5:
                out += [
                    (f"{s_}_end_p10_epsilon", float(g["epsilon"].quantile(0.10)),
                     _prov(run, f"{sp} 10th-percentile epsilon at end, n={len(g)}")),
                    (f"{s_}_end_p10_alpha", float(g["alpha"].quantile(0.10)),
                     _prov(run, f"{sp} 10th-percentile alpha at end, n={len(g)}")),
                    (f"{s_}_end_frac_eps_low", float((g["epsilon"] < 0.15).mean()),
                     _prov(run, f"share of {sp} below epsilon 0.15 at end, n={len(g)}")),
                ]
            m = ag[(ag["species"] == sp) & (ag["age"] > 20)]
            if "reward" in m.columns and not m.empty:
                out.append((f"{s_}_mean_reward", float(m["reward"].mean()),
                            _prov(run, f"{sp} mean reward per decision, age>20, "
                                       f"{len(m)} snapshots")))

    # The selection gradient: within ONE run, does an individual's own genome predict how many
    # offspring it leaves? This is the statistic an "is X under selection" question actually needs,
    # and the only one here that survives a change in population size - a two-arm contrast of means
    # cannot separate selection from drift, and mode N runs at a third of mode C's population, so
    # every cross-arm mean and every per-decision average is confounded by density.
    # Its null is exact rather than assumed: in neutral mode the manager picks parents at random,
    # so the gradient is zero by construction.
    bb = run.get("births")
    if bb is not None and not bb.empty and "child_epsilon" in bb.columns:
        for sp in ("Lumen", "Tecton"):
            b = bb[bb["species"] == sp] if "species" in bb.columns else bb
            if len(b) < 50:
                continue
            early = b[b["sim_time"] <= b["sim_time"].max() * 0.6]   # a full life in which to breed
            if len(early) < 30:
                continue
            counts = b.groupby("parent_id").size()
            kids = early.assign(offspring=early["child_id"].map(counts).fillna(0.0))
            s_ = sp.lower()
            for param in ("epsilon", "alpha", "social"):
                col = f"child_{param}"
                if col not in kids.columns:
                    continue
                r = kids[col].corr(kids["offspring"])
                if r == r:      # not NaN
                    out.append((f"{s_}_selection_r_{param}", float(r),
                                _prov(run, f"{sp} correlation of inherited {param} with offspring "
                                           f"count, n={len(kids)} born in the first 60%")))

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


def evidence_digest(con, limit=40, agent=None):
    """Compact evidence listing given to agents in context. Newest first.

    With `agent` set (embodied mode), the listing is what that scientist can
    personally read: all shared instrument evidence (run CSVs, the plain
    bridge windows, experiment results) plus ONLY their own witnessed rows —
    another scientist's field observations are not theirs to present."""
    if agent is None:
        rows = con.execute(
            "SELECT id, run_id, stat, value, provenance FROM evidence "
            "ORDER BY created_at DESC, id DESC LIMIT ?", (limit,)).fetchall()
    else:
        rows = con.execute(
            "SELECT id, run_id, stat, value, provenance FROM evidence "
            "WHERE provenance NOT LIKE 'witnessed by %' "
            "   OR provenance LIKE ? "
            "ORDER BY created_at DESC, id DESC LIMIT ?",
            (f"witnessed by {agent} %", limit)).fetchall()
    return [f"{r['id']}: {r['stat']}={r['value']:.4g} — {r['provenance']}" for r in rows]
