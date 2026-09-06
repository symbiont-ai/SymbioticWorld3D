#!/usr/bin/env python3
"""End-to-end smoke test on FIXTURE data (no sim, no model needed).

Session 1 (3 meetings) -> simulated sim-machine scoring (contrast experiment
AND the conservation stage-1 assessment) -> session 2 (stage-2 introduction
queued with predictions) -> score it -> session 3 (program closes).
Asserts along the way: discourse rules, promotion, Brier/credibility,
endangerment docket + two-stage program, lab evolution across generations,
and Vega's quarantine (figures + forecasts exist; no stances or predictions).

  python3 Lab/tests/test_smoke.py [work_dir]
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from Lab import db, runner  # noqa: E402
from Lab.tests.make_fixture import make_run  # noqa: E402


def _session(dbp, meetings, ingest=()):
    cmd = [sys.executable, "-m", "Lab.lab", "--db", str(dbp), "session",
           "--meetings", str(meetings), "--llm", "mock"]
    if ingest:
        cmd += ["--ingest"] + [str(p) for p in ingest]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    assert r.returncode == 0, r.stderr


def main():
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp(prefix="lab_smoke_"))
    # Hermetic: sessions (this process and the subprocesses, which inherit the
    # environment) must never sweep the machine's real Saved/SymbioticWorld
    # runs into the fixture lab — that changes what the fixtures assert.
    os.environ["LAB_SAVED"] = str(work / "saved_isolated")
    fixtures = work / "fixtures"
    treatment, control = [], []
    for seed in (1, 2, 3):
        treatment.append(make_run(fixtures, "C_learning_evolution", seed, alpha_trend=0.05))
        control.append(make_run(fixtures, "N_neutral_control", seed, alpha_trend=0.0))

    dbp = work / "lab.sqlite"
    _session(dbp, 3, ingest=treatment + control)

    con = db.connect(dbp)
    exps = con.execute("SELECT * FROM experiments ORDER BY id").fetchall()
    assert exps, "no experiments were designed"
    assert all(e["status"] == "awaiting-sim" for e in exps), \
        [f"{e['id']}:{e['status']}" for e in exps]

    # --- conservation: the Tecton collapse in the fixtures must have opened a
    # docket, the debate carried, and a stage-1 assessment been queued
    prog = con.execute("SELECT * FROM programs").fetchone()
    assert prog and prog["species"] == "Tecton", "no conservation docket opened"
    assert prog["status"] == "assessing", f"program status {prog['status']}"
    assert prog["assessment_exp"], "no assessment experiment queued"
    print(f"\n--- conservation docket {prog['id']}: stage-1 {prog['assessment_exp']} ---")
    v = runner.score_experiment(con, prog["assessment_exp"], treatment, [])
    assert v == "pass", f"assessment verdict {v} (fixture resource_B should pass)"

    # pick the C-vs-N experiment if one exists (the selection dispute), else the first
    target = next((e for e in exps
                   if json.loads(e["protocol"])["control_mode"] == "N"), exps[0])
    exp_id, hid = target["id"], target["hypothesis_id"]
    print(f"\n--- simulating sim-machine scoring of {exp_id} (card {hid}) ---")
    verdict = runner.score_experiment(con, exp_id, treatment, control)
    assert verdict is not None, "scoring failed"

    exp = con.execute("SELECT * FROM experiments WHERE id=?", (exp_id,)).fetchone()
    assert exp["status"] == "done" and exp["verdict"] == verdict
    preds = con.execute("SELECT * FROM predictions WHERE experiment_id=?", (exp_id,)).fetchall()
    assert preds and all(p["brier"] is not None for p in preds), "predictions not scored"
    briers = {p["agent"]: round(p["brier"], 2) for p in preds}
    print(f"verdict: {verdict}   briers: {briers}")
    assert len(set(briers.values())) > 1, "all agents scored identically — profiles do nothing"

    cred = con.execute("SELECT COUNT(*) FROM credibility WHERE n_scored > 0").fetchone()[0]
    assert cred > 0, "credibility never updated"

    card = con.execute("SELECT status FROM hypotheses WHERE id=?", (hid,)).fetchone()
    print(f"card {hid} status after experiment: {card['status']}")
    if verdict == "pass":
        assert card["status"] in ("supported", "law")

    open_disputes = con.execute(
        "SELECT COUNT(*) FROM disputes WHERE experiment_id=? AND resolution='open'",
        (exp_id,)).fetchone()[0]
    assert open_disputes == 0, "dispute docketed on a scored experiment left open"

    # the citation rule itself (mock agents always cite, so test it directly)
    from Lab.llm import MockLLM
    from Lab.meeting import MeetingRunner
    from Lab.profiles import load_profiles
    mr = MeetingRunner(con, load_profiles(), MockLLM(), log=lambda *a: None)
    s, _, _, _, d = mr._validate_stance(
        {"stance": "agree", "confidence": 0.9, "reason": "trust me", "evidence_ids": []})
    assert (s, d) == ("abstain", 1), "free consensus not blocked"
    s, _, _, _, d = mr._validate_stance(
        {"stance": "agree", "confidence": 0.9, "reason": "x", "evidence_ids": ["E-999"]})
    assert (s, d) == ("abstain", 1), "invented evidence ID not blocked"
    s, _, _, _, d = mr._validate_stance(
        {"stance": "disagree", "confidence": 0.8, "reason": "prior: selection first",
         "evidence_ids": []})
    assert (s, d) == ("disagree", 0), "named-prior disagree wrongly blocked"

    down = con.execute("SELECT COUNT(*) FROM stances WHERE downgraded=1").fetchone()[0]
    total = con.execute("SELECT COUNT(*) FROM stances").fetchone()[0]
    agree = con.execute("SELECT COUNT(*) FROM stances WHERE stance='agree'").fetchone()[0]
    print(f"stances: {total} total, {agree} agree, {down} downgraded by the citation rule")
    assert 0 < agree < total, "stance distribution degenerate"
    con.commit()
    con.close()

    # --- session 2: assessment passed -> stage-2 introduction with predictions
    print("\n--- session 2 (stage-2 introduction should queue) ---")
    _session(dbp, 1)
    con = db.connect(dbp)
    prog = con.execute("SELECT * FROM programs").fetchone()
    assert prog["status"] == "introducing", f"program status {prog['status']}"
    intro = prog["introduction_exp"]
    n_preds = con.execute("SELECT COUNT(*) FROM predictions WHERE experiment_id=?",
                          (intro,)).fetchone()[0]
    assert n_preds == 6, f"introduction predictions {n_preds}, expected 6"
    v = runner.score_experiment(con, intro, treatment, control)
    print(f"introduction {intro} verdict: {v}")
    assert v is not None
    con.commit()
    con.close()

    # --- session 3: program closes; evolution has run across generations
    print("\n--- session 3 (program closes, lab evolves) ---")
    _session(dbp, 1)
    con = db.connect(dbp)
    prog = con.execute("SELECT * FROM programs").fetchone()
    assert prog["status"] == "done", f"program status {prog['status']}"

    gen = int(db.get_meta(con, "generation", 0))
    assert gen >= 3, f"generation counter {gen}, expected >= 3 after 3 sessions"
    evolved = con.execute(
        "SELECT COUNT(*) FROM agent_params WHERE fitness IS NOT NULL").fetchone()[0]
    assert evolved > 0, "no agent fitness recorded — lab evolution never ran"
    changed = con.execute(
        "SELECT COUNT(*) FROM agent_params a JOIN agent_params b "
        "ON a.agent=b.agent AND b.generation=a.generation+1 "
        "WHERE ABS(a.temperature-b.temperature) > 1e-9").fetchone()[0]
    assert changed > 0, "no agent parameters mutated across generations"
    print(f"lab generation {gen}; {changed} agent parameter mutation(s) recorded")

    # --- Vega: annex produced, and strictly quarantined
    n_fc = con.execute("SELECT COUNT(*) FROM forecasts").fetchone()[0]
    assert n_fc > 0, "Vega produced no forecasts"
    figs = list((ROOT / "Lab" / "reports" / "figs").glob("vega_*.png"))
    assert figs, "Vega produced no figures"
    for table, col in (("stances", "agent"), ("predictions", "agent"),
                       ("transcript", "agent"), ("credibility", "agent")):
        hit = con.execute(f"SELECT COUNT(*) FROM {table} WHERE {col}='Vega'").fetchone()[0]
        assert hit == 0, f"Vega leaked into {table} — quarantine broken"
    vega_ev = con.execute("SELECT COUNT(*) FROM evidence WHERE provenance LIKE '%Vega%'"
                          ).fetchone()[0]
    assert vega_ev == 0, "Vega minted evidence — quarantine broken"
    print(f"Vega: {n_fc} forecasts, {len(figs)} figure(s), quarantine holds")

    print("\nSMOKE TEST PASSED")


if __name__ == "__main__":
    main()
