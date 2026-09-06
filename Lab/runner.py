"""Experiment runner (PRD F4). Executes approved protocols through the repo's
own Tools/run_sim.py (imported, so we reuse its run-dir capture), computes the
preregistered metric in code, marks predictions pass/fail mechanically, scores
agents, and applies the registry rules.

On a machine without the UE engine (this lab also runs on the Mac that only
holds the textbook), experiments stay queued as 'awaiting-sim' and the lab
report lists them for the sim machine to execute.
"""
import json
import sys

from . import config, db, evidence, memory, scorer

sys.path.insert(0, str(config.ROOT / "Tools"))
import run_sim  # noqa: E402  (the repo's own launcher; ENGINE paths live there)

# The only stats a protocol may preregister as its metric — each is a per-run
# scalar minted by evidence.compute_run_stats, so every metric is citable.
METRICS = [
    "lumen_end_n", "lumen_min_n", "lumen_end_mean_alpha", "lumen_end_mean_epsilon",
    "lumen_end_mean_env_effect", "lumen_max_generation", "lumen_q_drift_median",
    "lumen_greedy_changed_pct", "tecton_end_n", "tecton_min_n",
    "inherit_alpha_corr", "inherit_epsilon_corr", "inherit_env_effect_corr",
    "alpha_gen_slope", "epsilon_gen_slope", "env_effect_gen_slope",
    "trace_y_end_mean", "total_births", "total_deaths",
    "resource_A_end", "resource_B_end",
]


def engine_available():
    return run_sim.EDITOR_CMD.exists()


def service_client(log=print):
    """Client for a Tools/experiment_service.py on the sim host, when
    LAB_SIM_SERVICE=<host[:port]> is set and the service answers /health.
    Lets a lab without the UE engine execute its queue remotely: runs happen
    on the host, the CSVs are downloaded under Saved/SymbioticWorld/ here,
    and every scoring rule then applies unchanged."""
    import os
    spec = os.environ.get("LAB_SIM_SERVICE", "")
    if not spec:
        return None
    host, _, port = spec.partition(":")
    import scientist_client   # Tools/ is on sys.path (see run_sim import)
    client = scientist_client.Client(host, int(port or 8800), timeout=30.0)
    try:
        client.health()
        return client
    except Exception as ex:
        log(f"  [runner] experiment service {spec} unreachable ({ex})")
        return None


def _run_arm_remote(client, mode, set_spec, seeds, duration, label, log):
    """One arm through the experiment service; returns local run dirs."""
    job = client.submit_runs(mode, list(seeds), duration=duration, speed=200,
                             set_spec=set_spec or "", label=label[:80])
    jid = job["job_id"]
    log(f"  [runner] service job {jid}: mode={mode} seeds={list(seeds)} "
        f"duration={duration} set={set_spec or '-'}")
    done = client.wait(jid, poll_s=5.0, timeout_s=3600, verbose=False)
    dirs = []
    for r in done["runs"]:
        if r["status"] != "done" or not r.get("sim_run_id"):
            raise RuntimeError(f"service run {r['run_id']}: {r.get('error') or r['status']}")
        dest = config.SAVED / r["sim_run_id"]
        dest.mkdir(parents=True, exist_ok=True)
        for name in ("population", "agents", "births", "deaths"):
            client.download_csv(r["run_id"], name, dest / f"{name}.csv")
        dirs.append(dest)
        log(f"  [runner]   {r['sim_run_id']} downloaded ({r['wall_s']:.0f}s wall)")
    return dirs


def metric_value(run_dir, metric):
    import analyze_run
    run = analyze_run.load_run(run_dir)
    for stat, value, _ in evidence.compute_run_stats(run):
        if stat == metric:
            return value
    return None


def _run_arm(mode, set_spec, seeds, duration, log):
    dirs = []
    for s in seeds:
        log(f"  [runner] run_sim mode={mode} seed={s} duration={duration} set={set_spec or '-'}")
        dirs += run_sim.run_one(mode, s, duration, speed=200, windowed=False,
                                extra=[], set_spec=set_spec or None)
    return dirs


def execute_experiment(con, exp_id, log=print):
    exp = con.execute("SELECT * FROM experiments WHERE id=?", (exp_id,)).fetchone()
    proto = json.loads(exp["protocol"])
    remote = None
    if not engine_available():
        remote = service_client(log)
        if remote is None:
            con.execute("UPDATE experiments SET status='awaiting-sim' WHERE id=?", (exp_id,))
            con.commit()
            log(f"  [runner] {exp_id}: no UE engine and no experiment service -> awaiting-sim")
            return None

    def arm(mode, set_spec, label):
        if remote:
            return _run_arm_remote(remote, mode, set_spec, proto["seeds"],
                                   proto["duration"], label, log)
        return _run_arm(mode, set_spec, proto["seeds"], proto["duration"], log)

    try:
        t_dirs = arm(proto["intervention_mode"], proto["intervention_set"],
                     f"{exp_id} treatment")
        c_dirs = [] if proto.get("kind") == "assessment" else \
            arm(proto["control_mode"], proto["control_set"], f"{exp_id} control")
    except Exception as ex:
        con.execute("UPDATE experiments SET status='failed', metric_result=? WHERE id=?",
                    (f"launch error: {ex}", exp_id))
        con.commit()
        return None

    for d in t_dirs + c_dirs:
        evidence.ingest_run(con, d)
    return score_experiment(con, exp_id, t_dirs, c_dirs, log)


def score_experiment(con, exp_id, treatment_dirs, control_dirs, log=print):
    """Mechanical verdict, no model in the loop.
    - contrast (default): mean(metric) treatment arm minus control arm vs the
      preregistered threshold.
    - assessment (conservation stage 1): single arm; pass iff mean(metric) >=
      the code-templated criteria_min."""
    exp = con.execute("SELECT * FROM experiments WHERE id=?", (exp_id,)).fetchone()
    proto = json.loads(exp["protocol"])
    metric, threshold = proto["metric"], float(proto["threshold"])
    kind = proto.get("kind", "contrast")

    tv = [v for v in (metric_value(d, metric) for d in treatment_dirs) if v is not None]
    cv = [v for v in (metric_value(d, metric) for d in control_dirs) if v is not None]
    if not tv or (kind == "contrast" and not cv):
        con.execute("UPDATE experiments SET status='failed', metric_result=? WHERE id=?",
                    (f"metric {metric} missing from runs", exp_id))
        con.commit()
        return None

    if kind == "assessment":
        mean = sum(tv) / len(tv)
        crit = float(proto["criteria_min"])
        verdict = "pass" if mean >= crit else "fail"
        actual = verdict
        result = {"metric": metric, "mean": mean, "criteria_min": crit,
                  "n": len(tv), "kind": kind}
        con.execute("UPDATE experiments SET status='done', run_ids=?, metric_result=?,"
                    " verdict=? WHERE id=?",
                    (json.dumps([str(d) for d in treatment_dirs]),
                     json.dumps(result), verdict, exp_id))
        db.add_evidence(con, f"exp:{exp_id}", metric, mean,
                        f"assessment {exp_id}: mean {metric} over {len(tv)} baseline runs, "
                        f"criterion >= {crit}")
        log(f"  [runner] {exp_id} (assessment): {metric} mean {mean:.4g} vs "
            f"criterion {crit} -> {verdict}")
        con.commit()
        return verdict

    diff = sum(tv) / len(tv) - sum(cv) / len(cv)
    actual = ("treatment_higher" if diff > threshold
              else "treatment_lower" if diff < -threshold
              else "no_difference")
    expected = proto.get("direction_if_claim_true", "treatment_higher")
    verdict = ("pass" if actual == expected
               else "opposite" if actual != "no_difference"
               else "fail")

    result = {"metric": metric, "treatment_mean": sum(tv) / len(tv),
              "control_mean": sum(cv) / len(cv), "diff": diff,
              "threshold": threshold, "actual": actual, "n_per_arm": len(tv)}
    con.execute(
        "UPDATE experiments SET status='done', run_ids=?, metric_result=?, verdict=? WHERE id=?",
        (json.dumps([str(d) for d in treatment_dirs + control_dirs]),
         json.dumps(result), verdict, exp_id))

    eid = db.add_evidence(con, f"exp:{exp_id}", f"{metric}_diff", diff,
                          f"experiment {exp_id}: mean {metric}, treatment minus control, "
                          f"n={len(tv)}/arm, threshold {threshold}")
    log(f"  [runner] {exp_id}: {metric} diff {diff:+.4g} (threshold {threshold}) "
        f"-> actual={actual}, verdict={verdict} ({eid})")

    # Score every preregistered prediction; update credibility per domain.
    from .profiles import load_profiles
    profs = load_profiles()
    for p in con.execute("SELECT * FROM predictions WHERE experiment_id=?", (exp_id,)):
        b = scorer.brier(p["predicted"], p["confidence"], actual)
        con.execute("UPDATE predictions SET actual=?, brier=? WHERE experiment_id=? AND agent=?",
                    (actual, b, exp_id, p["agent"]))
        domains = profs[p["agent"]].domains if p["agent"] in profs else []
        scorer.update_credibility(con, p["agent"], domains, b)
        hit = "RIGHT" if p["predicted"] == actual else "WRONG"
        memory.append_notebook(con, p["agent"],
                               f"[{exp_id}] I predicted {p['predicted']} ({p['confidence']:.2f}) "
                               f"and was {hit} (Brier {b:.2f}).")

    # Discourse has consequences: the stance sides on this card are scored too.
    if verdict in ("pass", "opposite"):
        claim_held = (verdict == "pass")
        for st in con.execute(
                "SELECT agent, stance FROM stances WHERE hypothesis_id=? AND id IN "
                "(SELECT MAX(id) FROM stances WHERE hypothesis_id=? GROUP BY agent)",
                (exp["hypothesis_id"], exp["hypothesis_id"])):
            if st["stance"] == "abstain" or st["agent"] not in profs:
                continue
            was_right = (st["stance"] == "agree") == claim_held
            scorer.adjust_for_stance(con, st["agent"], profs[st["agent"]].domains, was_right)
    con.commit()

    memory.apply_experiment_outcome(con, exp_id, log)
    return verdict


def execute_queued(con, log=print):
    """Run every queued experiment; returns number executed here."""
    done = 0
    for row in con.execute("SELECT id FROM experiments WHERE status='queued'").fetchall():
        if execute_experiment(con, row["id"], log) is not None:
            done += 1
    return done
