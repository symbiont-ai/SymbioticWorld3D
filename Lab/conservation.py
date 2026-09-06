"""Conservation dockets: the lab debates when to intervene to help a species.

Detection is code (a species' recent end-of-run population under a threshold
mints an endangerment card citing that evidence). The DEBATE is the agents'
(stances on the card, same citation rules as any card). The program itself is
two staged, code-templated experiments:

  stage 1 — ASSESSMENT: single-arm baseline runs; the environment must show
            resource headroom for the species (criteria checked in code).
  stage 2 — INTRODUCTION: founders of the endangered species boosted vs an
            unmodified control; every agent preregisters the outcome.

A program advances only on recorded verdicts: debating -> assessing ->
introducing -> done, or assessment-failed / rejected. Introduction is only
run-level today (more founders at t=0), because the sim has no mid-run
spawn verb — documented, not hidden.
"""
import json

from . import db, memory, scorer
from .profiles import TURN_ORDER

ENDANGERED_N = 8.0          # mean end-of-run population below this = endangered
RECENT_RUNS = 6
DEBATE_MEETINGS_MAX = 3     # majority never emerges -> rejected

SPECIES_SETTINGS = {
    "Lumen":  {"resource_metric": "resource_A_end", "resource_min": 400.0,
               "founder_key": "Settings.InitialLumen", "founder_boost": 60,
               "pop_metric": "lumen_end_n"},
    "Tecton": {"resource_metric": "resource_B_end", "resource_min": 250.0,
               "founder_key": "Settings.InitialTecton", "founder_boost": 24,
               "pop_metric": "tecton_end_n"},
}


def detect_endangered(con):
    """Species whose mean end-of-run population over the newest runs is below
    the threshold. Returns {species: [evidence_ids]}."""
    out = {}
    for species in SPECIES_SETTINGS:
        rows = con.execute(
            "SELECT e.id, e.value FROM evidence e JOIN runs r ON e.run_id=r.run_id "
            "WHERE e.stat=? ORDER BY r.ingested_at DESC, e.id DESC LIMIT ?",
            (f"{species.lower()}_end_n", RECENT_RUNS)).fetchall()
        if rows:
            vals = [r["value"] for r in rows]
            if sum(vals) / len(vals) < ENDANGERED_N:
                out[species] = [r["id"] for r in rows]
    return out


def _active_program(con, species):
    return con.execute(
        "SELECT * FROM programs WHERE species=? AND status IN "
        "('debating','assessing','introducing')", (species,)).fetchone()


def ensure_programs(con, mid, log=print):
    """Open a docket (card + program) for each newly endangered species."""
    for species, eids in detect_endangered(con).items():
        if _active_program(con, species):
            continue
        claim = (f"A staged reintroduction of {species} is warranted: recent runs show "
                 f"the {species} population floor collapsing, and if assessment finds "
                 f"resource headroom, introducing additional founders would raise the "
                 f"population floor without starving the rest of the valley.")
        hid = memory.create_card(con, claim,
                                 "two-stage program: environment assessment, then introduction",
                                 f"{species} only; run-level introduction (founders at t=0)",
                                 "Docket", mid, evidence_ids=eids)
        pid = db.next_id(con, "programs", "P")
        con.execute("INSERT INTO programs VALUES(?,?,?,?,?,?,?,?)",
                    (pid, species, hid, "assessment", "debating", None, None, mid))
        log(f"  [conservation] {species} endangered ({', '.join(eids[:3])}...) "
            f"-> docket {pid} opened with card {hid}")
    con.commit()


def _weighted_split(con, profiles, stances):
    w = {"agree": 0.0, "disagree": 0.0}
    for agent, (stance, _conf) in stances.items():
        if stance in w:
            w[stance] += scorer.vote_weight(con, agent, profiles[agent].domains)
    return w


def _assessment_protocol(species):
    s = SPECIES_SETTINGS[species]
    return {"kind": "assessment", "intervention_mode": "C", "intervention_set": "",
            "control_mode": "C", "control_set": "", "seeds": [1, 2],
            "duration": 600.0, "metric": s["resource_metric"],
            "criteria_min": s["resource_min"], "threshold": 0.0,
            "direction_if_claim_true": "treatment_higher",
            "rationale": f"Stage 1: does the environment hold >= {s['resource_min']} "
                         f"end-of-run stock of {species}'s resource?"}


def _introduction_protocol(species):
    s = SPECIES_SETTINGS[species]
    return {"kind": "contrast", "intervention_mode": "C",
            "intervention_set": f"{s['founder_key']}={s['founder_boost']}",
            "control_mode": "C", "control_set": "", "seeds": [1, 2, 3],
            "duration": 900.0, "metric": s["pop_metric"], "threshold": 3.0,
            "direction_if_claim_true": "treatment_higher",
            "rationale": f"Stage 2: boosted {species} founders vs baseline; the program "
                         f"succeeds if the {species} population ends measurably higher."}


def _queue_experiment(con, meeting_runner, mid, card, proto, with_predictions):
    exp_id = db.next_id(con, "experiments", "X")
    con.execute("INSERT INTO experiments VALUES(?,?,?,?,?,?,?,?)",
                (exp_id, card["id"], json.dumps(proto), "proposed", None, None, None, mid))
    review = meeting_runner._turn(
        "Karla", "review",
        f"Review this conservation-stage protocol for {card['id']}: {json.dumps(proto)}",
        {"protocol": proto, "claim": card["claim"]})
    db.log_turn(con, mid, "CONSERVATION-REVIEW", "Karla", review)
    if not review.get("approve", True) and review.get("veto_reason"):
        con.execute("UPDATE experiments SET status='vetoed' WHERE id=?", (exp_id,))
        return None
    if with_predictions:
        for agent in TURN_ORDER:
            if agent == "Archie":
                continue
            p = meeting_runner._turn(
                agent, "prediction",
                f"PREREGISTER the outcome of introduction experiment {exp_id} "
                f"(metric {proto['metric']}, boosted founders vs control, threshold "
                f"{proto['threshold']}). Card: \"{card['claim']}\"",
                {"claim": card["claim"],
                 "direction_if_claim_true": proto["direction_if_claim_true"]})
            pred = p.get("predicted", "no_difference")
            if pred not in scorer.OUTCOMES:
                pred = "no_difference"
            con.execute(
                "INSERT OR REPLACE INTO predictions(experiment_id, agent, predicted,"
                " confidence, actual, brier) VALUES(?,?,?,?,NULL,NULL)",
                (exp_id, agent, pred, max(0.0, min(1.0, float(p.get("confidence", 0.5))))))
    con.execute("UPDATE experiments SET status='queued' WHERE id=?", (exp_id,))
    return exp_id


def advance(con, meeting_runner, mid, stance_map, log=print):
    """Called once per meeting after the stance round. Moves each program
    through its state machine based on recorded stances and verdicts."""
    profiles = meeting_runner.profiles
    for prog in con.execute("SELECT * FROM programs WHERE status IN "
                            "('debating','assessing','introducing')").fetchall():
        pid, species, hid = prog["id"], prog["species"], prog["card_id"]
        card = con.execute("SELECT * FROM hypotheses WHERE id=?", (hid,)).fetchone()

        if prog["status"] == "debating":
            stances = stance_map.get(hid)
            if not stances:
                continue
            w = _weighted_split(con, profiles, stances)
            if w["agree"] > w["disagree"]:
                proto = _assessment_protocol(species)
                exp_id = _queue_experiment(con, meeting_runner, mid, card, proto,
                                           with_predictions=False)
                if exp_id:
                    con.execute("UPDATE programs SET status='assessing', "
                                "assessment_exp=? WHERE id=?", (exp_id, pid))
                    db.log_intervention(con, mid, "conservation",
                                        {"program": pid, "species": species,
                                         "stage": "assessment", "experiment": exp_id,
                                         "debate": w}, "credibility-weighted debate")
                    log(f"  [conservation] {pid}: debate carried "
                        f"({w['agree']:.2f} vs {w['disagree']:.2f}) -> "
                        f"stage-1 assessment {exp_id} queued")
            else:
                age = mid - prog["created_meeting"]
                if age >= DEBATE_MEETINGS_MAX:
                    con.execute("UPDATE programs SET status='rejected' WHERE id=?", (pid,))
                    log(f"  [conservation] {pid}: no majority after {age} meetings -> rejected")
                else:
                    log(f"  [conservation] {pid}: debate continues "
                        f"({w['agree']:.2f} vs {w['disagree']:.2f})")

        elif prog["status"] == "assessing":
            exp = con.execute("SELECT * FROM experiments WHERE id=?",
                              (prog["assessment_exp"],)).fetchone()
            if exp and exp["status"] == "done":
                if exp["verdict"] == "pass":
                    proto = _introduction_protocol(species)
                    exp_id = _queue_experiment(con, meeting_runner, mid, card, proto,
                                               with_predictions=True)
                    if exp_id:
                        con.execute("UPDATE programs SET status='introducing', stage="
                                    "'introduction', introduction_exp=? WHERE id=?",
                                    (exp_id, pid))
                        db.log_intervention(con, mid, "conservation",
                                            {"program": pid, "species": species,
                                             "stage": "introduction",
                                             "experiment": exp_id}, "assessment verdict")
                        log(f"  [conservation] {pid}: assessment passed -> "
                            f"stage-2 introduction {exp_id} queued with predictions")
                else:
                    con.execute("UPDATE programs SET status='assessment-failed' WHERE id=?",
                                (pid,))
                    con.execute("UPDATE hypotheses SET status='refuted' WHERE id=?", (hid,))
                    log(f"  [conservation] {pid}: assessment failed -> program halted, "
                        f"{hid} refuted (environment cannot support introduction)")

        elif prog["status"] == "introducing":
            exp = con.execute("SELECT * FROM experiments WHERE id=?",
                              (prog["introduction_exp"],)).fetchone()
            if exp and exp["status"] == "done":
                con.execute("UPDATE programs SET status='done' WHERE id=?", (pid,))
                log(f"  [conservation] {pid}: introduction {exp['verdict']} -> program done")
    con.commit()
