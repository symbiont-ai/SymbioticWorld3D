"""Victory conditions: the spec's "Minimum proof of challenge fit" (§1).

The six conditions the demo must earn, held as the lab's standing victory
conditions. Each is scored mechanically from the evidence and experiments
tables — met / partial / unmet, citing evidence IDs — printed in every lab
report, and every condition not yet met stands as an open question
(OQ-VC-*) so the meetings keep aiming experiments at it. The verdicts are
decided here in code; the scientists only argue about what to do next.

Demo-observability conditions (VC-1, VC-2) are scored by whether the data
that the demo step displays has actually been captured; adaptation claims
(VC-3..VC-6) additionally require the preregistered control contrast the
spec demands (mode A for lifetime learning, mode N for selection, a
regen-cut arm for drought — the documented drought proxy, DESIGN.md).
"""

# Thresholds (tunable in one place; provenance: README "What is verified")
DRIFT_MIN = 0.1        # median lifetime Q drift that counts as learning (B/C: 0.8-1.4)
STATIC_MAX = 0.01      # mode A drift must stay at effectively zero
CORR_MIN, CORR_MAX = 0.5, 0.995   # inheritance: related (>=0.5) but mutated (<=0.995)
BASELINE_REGEN = 6.0   # regen-6 baseline; any lower intervention arm is the drought proxy

CONDITIONS = [
    ("VC-1", "Select a live Lumen and display its initial action-policy values."),
    ("VC-2", "Let that same individual interact with the world for 30-60 seconds."),
    ("VC-3", "Show that its learned policy changed because of experience."),
    ("VC-4", "Show its offspring inheriting a related but mutated learning configuration."),
    ("VC-5", "Accelerate time and show a population-level shift in meta-learning parameters."),
    ("VC-6", "Trigger a drought and demonstrate adaptation continuing without offline retraining."),
]

ORIGIN = "Symbiotic_World_Spec.pdf §1 — minimum proof of challenge fit"


def _mode_of(con, run_id, _cache={}):
    """Mode letter for an evidence row's run_id; handles live window ids
    ('live:<run>:wN' -> runs row 'live:<run>') and experiment rows ('exp:X-...')."""
    if run_id in _cache:
        return _cache[run_id]
    row = con.execute("SELECT mode FROM runs WHERE run_id=?", (run_id,)).fetchone()
    if row is None and run_id.startswith("live:"):
        row = con.execute("SELECT mode FROM runs WHERE ? LIKE run_id || ':%'",
                          (run_id,)).fetchone()
    mode = (row["mode"][:1].upper() if row and row["mode"] else "?")
    _cache[run_id] = mode
    return mode


def _stat(con, stat, modes=None):
    """Evidence rows for one stat as (id, value, run_id, mode), mode-filtered."""
    out = []
    for e in con.execute("SELECT id, value, run_id FROM evidence WHERE stat=?", (stat,)):
        m = _mode_of(con, e["run_id"])
        if modes is None or m in modes:
            out.append((e["id"], e["value"], e["run_id"], m))
    return out


def _drought_experiments(con):
    """Done experiments whose intervention arm cuts PatchRegenPerSec below the
    regen-6 baseline — the documented drought proxy."""
    import json as _json
    hits = []
    for e in con.execute("SELECT * FROM experiments"):
        try:
            proto = _json.loads(e["protocol"])
        except (TypeError, ValueError):
            continue
        spec = proto.get("intervention_set") or ""
        for part in spec.split(";"):
            if "PatchRegenPerSec" in part and "=" in part:
                try:
                    if float(part.split("=", 1)[1]) < BASELINE_REGEN:
                        hits.append((e, proto))
                except ValueError:
                    pass
                break
    return hits


def _run_ids_of(exp):
    import json as _json
    from pathlib import PurePath
    try:
        dirs = _json.loads(exp["run_ids"] or "[]")
    except (TypeError, ValueError):
        return []
    return [PurePath(d).name for d in dirs]


# --- one check per condition; each returns (status, why, evidence_ids) ------

def _vc1(con):
    live = [r for r in _stat(con, "live_lumen_n") if r[1] > 0]
    ingested = [r for r in _stat(con, "lumen_end_n") if r[1] > 0]
    if live or ingested:
        rows = (live + ingested)[:3]
        return ("met", "per-organism Lumen policy data captured "
                f"({len(live)} live window(s), {len(ingested)} ingested run(s))",
                [r[0] for r in rows])
    tec = [r for r in _stat(con, "live_tecton_n") if r[1] > 0]
    if tec:
        return ("partial", "only Tecton organisms observed so far; no live Lumen "
                "has been captured with its policy table", [tec[0][0]])
    return ("unmet", "no run or live window with Lumen organisms ingested yet", [])


def _vc2(con):
    rows = _stat(con, "lumen_q_drift_median")
    long_enough = [r for r in rows
                   if (con.execute("SELECT sim_end FROM runs WHERE run_id=?",
                                   (r[2],)).fetchone() or {"sim_end": 0})["sim_end"] >= 60]
    if long_enough:
        return ("met", "per-lifetime policy tracking exists over runs of >= 60 sim-seconds",
                [r[0] for r in long_enough[:3]])
    if rows:
        return ("partial", "lifetime tracking exists but no ingested run reaches 60 sim-seconds",
                [rows[0][0]])
    return ("unmet", "no per-lifetime Q-drift evidence ingested yet", [])


def _vc3(con):
    learning = [r for r in _stat(con, "lumen_q_drift_median", modes={"B", "C"})
                if r[1] >= DRIFT_MIN]
    control = [r for r in _stat(con, "lumen_q_drift_median", modes={"A"})
               if r[1] <= STATIC_MAX]
    if learning and control:
        return ("met", f"median lifetime Q drift >= {DRIFT_MIN} in mode B/C while the "
                f"mode A control stays <= {STATIC_MAX}",
                [learning[0][0], control[0][0]])
    if learning:
        return ("partial", "policy drift shown in mode B/C, but the mode A "
                "learning-off control run is not in evidence", [learning[0][0]])
    return ("unmet", "no mode B/C run in evidence with median lifetime Q drift "
            f">= {DRIFT_MIN}", [])


def _vc4(con):
    ok, ids = [], []
    for stat in ("inherit_alpha_corr", "inherit_epsilon_corr", "inherit_env_effect_corr"):
        for r in _stat(con, stat, modes={"B", "C"}):
            if CORR_MIN <= r[1] <= CORR_MAX:
                ok.append(stat)
                ids.append(r[0])
                break
    if len(ok) >= 2:
        return ("met", "parent/child correlation is high but below 1 for "
                f"{', '.join(ok)} — inherited and mutated", ids[:3])
    if ok:
        return ("partial", f"inheritance-with-mutation shown only for {ok[0]}", ids)
    return ("unmet", "no parent/child genome correlation in "
            f"[{CORR_MIN}, {CORR_MAX}] in evidence", [])


def _vc5(con):
    import json as _json
    for e in con.execute("SELECT * FROM experiments WHERE status='done' AND verdict='pass'"):
        proto = _json.loads(e["protocol"])
        if (proto.get("intervention_mode") == "C" and proto.get("control_mode") == "N"
                and ("alpha" in proto.get("metric", "") or "epsilon" in proto.get("metric", "")
                     or "env_effect" in proto.get("metric", ""))):
            return ("met", f"experiment {e['id']} separated mode C from the "
                    "neutral-drift control on a meta-parameter metric", [])
    slopes = []
    for stat in ("alpha_gen_slope", "epsilon_gen_slope", "env_effect_gen_slope"):
        slopes += [(r, stat) for r in _stat(con, stat, modes={"C"}) if r[1] != 0]
    if slopes:
        return ("partial", "per-generation meta-parameter slope measured in mode C, "
                "but no passed C-vs-N experiment separates selection from drift "
                "(X-001 is that experiment)", [slopes[0][0][0]])
    return ("unmet", "no per-generation meta-parameter trend in evidence for mode C", [])


def _vc6(con):
    done = _drought_experiments(con)
    for exp, proto in done:
        if exp["status"] != "done":
            continue
        rids = _run_ids_of(exp)
        survived = [r for r in _stat(con, "lumen_min_n") if r[2] in rids and r[1] > 0]
        learning = [r for r in _stat(con, "lumen_q_drift_median")
                    if r[2] in rids and r[1] >= DRIFT_MIN]
        if survived and learning:
            return ("met", f"experiment {exp['id']}: population survived the regen-cut "
                    "drought proxy and lifetime Q drift continued during it",
                    [survived[0][0], learning[0][0]])
    pending = [e["id"] for e, _ in done if e["status"] in
               ("queued", "awaiting-sim", "proposed")]
    if pending:
        return ("partial", "drought-proxy experiment(s) designed but not run yet: "
                + ", ".join(pending), [])
    return ("unmet", "no experiment with a PatchRegenPerSec cut below the regen-6 "
            "baseline (the documented drought proxy) in the docket", [])


CHECKS = {"VC-1": _vc1, "VC-2": _vc2, "VC-3": _vc3,
          "VC-4": _vc4, "VC-5": _vc5, "VC-6": _vc6}


def scorecard(con):
    """[{id, text, status, why, evidence_ids}] for all six conditions."""
    out = []
    for vc_id, text in CONDITIONS:
        status, why, eids = CHECKS[vc_id](con)
        out.append({"id": vc_id, "text": text, "status": status,
                    "why": why, "evidence_ids": eids})
    return out


def sync_questions(con):
    """Keep one open question per condition not yet met, so the meetings'
    question-adaptation loop keeps designing experiments against the spec's
    victory conditions. Met conditions get their question marked addressed."""
    for row in scorecard(con):
        qid = f"OQ-{row['id']}"
        status = "addressed" if row["status"] == "met" else "open"
        text = f"[{row['status']}] {row['text']} ({row['why']})"
        if con.execute("SELECT 1 FROM open_questions WHERE id=?", (qid,)).fetchone():
            con.execute("UPDATE open_questions SET text=?, status=? WHERE id=?",
                        (text, status, qid))
        else:
            con.execute("INSERT INTO open_questions VALUES(?,?,?,?)",
                        (qid, text, ORIGIN, status))
    con.commit()


def report_section(con):
    """Markdown lines for the lab report."""
    icon = {"met": "MET", "partial": "PARTIAL", "unmet": "UNMET"}
    L = ["## Victory conditions (spec §1 — minimum proof of challenge fit)", "",
         "| id | status | condition | how it stands |", "|---|---|---|---|"]
    for row in scorecard(con):
        cite = (" [" + ", ".join(row["evidence_ids"]) + "]") if row["evidence_ids"] else ""
        L.append(f"| {row['id']} | **{icon[row['status']]}** | {row['text']} | "
                 f"{row['why']}{cite} |")
    L.append("")
    return L
