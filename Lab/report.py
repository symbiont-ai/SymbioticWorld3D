"""Session outputs (PRD F6): a markdown lab report and the full transcript,
both written to Lab/reports/. The report also emits exact run_sim.py commands
for experiments still awaiting the sim machine."""
import json
from datetime import datetime

from . import config, db


def _rows(con, q, *a):
    return con.execute(q, *a).fetchall()


def generate(con, session_label=None, annex_lines=None):
    config.REPORT_DIR.mkdir(parents=True, exist_ok=True)
    stamp = session_label or datetime.now().strftime("%Y%m%d_%H%M%S")
    report_path = config.REPORT_DIR / f"lab_report_{stamp}.md"
    transcript_path = config.REPORT_DIR / f"transcript_{stamp}.md"

    gen = db.get_meta(con, "generation", 0)
    L = [f"# Symbiotic Lab — session report {stamp}", ""]

    runs = _rows(con, "SELECT * FROM runs ORDER BY ingested_at")
    L += [f"**Lab generation:** {gen}   **Runs ingested:** {len(runs)}   "
          f"**Evidence objects:** {con.execute('SELECT COUNT(*) FROM evidence').fetchone()[0]}", ""]

    L += ["## Registry (the textbook)", "",
          "| id | status | claim | proposer |", "|---|---|---|---|"]
    for h in _rows(con, "SELECT * FROM hypotheses ORDER BY id"):
        L.append(f"| {h['id']} | **{h['status']}** | {h['claim']} | {h['proposer']} |")
    L.append("")

    exps = _rows(con, "SELECT * FROM experiments ORDER BY id")
    if exps:
        L += ["## Experiments", ""]
        for e in exps:
            proto = json.loads(e["protocol"])
            L.append(f"### {e['id']} — {e['status']}"
                     + (f" ({e['verdict']})" if e["verdict"] else ""))
            L.append(f"- card: {e['hypothesis_id']}  ·  metric: `{proto['metric']}`  ·  "
                     f"threshold {proto['threshold']}  ·  expected if true: "
                     f"{proto['direction_if_claim_true']}")
            L.append(f"- arms: [{proto['intervention_mode']}"
                     + (f" +set '{proto['intervention_set']}'" if proto['intervention_set'] else "")
                     + f"] vs [{proto['control_mode']}"
                     + (f" +set '{proto['control_set']}'" if proto['control_set'] else "")
                     + f"], seeds {proto['seeds']}, {proto['duration']:.0f}s")
            if e["metric_result"]:
                try:
                    r = json.loads(e["metric_result"])
                    L.append(f"- result: treatment {r['treatment_mean']:.4g} vs control "
                             f"{r['control_mean']:.4g} (diff {r['diff']:+.4g}) -> {r['actual']}")
                except (json.JSONDecodeError, TypeError, KeyError):
                    L.append(f"- result: {e['metric_result']}")
            preds = _rows(con, "SELECT * FROM predictions WHERE experiment_id=? ORDER BY agent",
                          (e["id"],))
            if preds:
                L.append("- preregistered predictions: "
                         + "; ".join(f"{p['agent']} {p['predicted']} ({p['confidence']:.2f}"
                                     + (f", Brier {p['brier']:.2f}" if p["brier"] is not None else "")
                                     + ")" for p in preds))
            L.append("")

    disputes = _rows(con, "SELECT * FROM disputes ORDER BY id")
    if disputes:
        L += ["## Disputes", ""]
        for d in disputes:
            sides = json.loads(d["sides"])
            L.append(f"- **{d['id']}** on {d['hypothesis_id']}: "
                     f"{', '.join(sides.get('agree', []))} vs "
                     f"{', '.join(sides.get('disagree', []))} — {d['resolution']}"
                     + (f" (experiment {d['experiment_id']})" if d["experiment_id"] else ""))
        L.append("")

    progs = _rows(con, "SELECT * FROM programs ORDER BY id")
    if progs:
        L += ["## Conservation programs", ""]
        for p in progs:
            L.append(f"- **{p['id']}** ({p['species']}, card {p['card_id']}): "
                     f"**{p['status']}**"
                     + (f" — assessment {p['assessment_exp']}" if p["assessment_exp"] else "")
                     + (f", introduction {p['introduction_exp']}" if p["introduction_exp"] else ""))
        L += ["", "_Stage 1 assesses resource headroom in code before any introduction; "
              "introduction is run-level (boosted founders at t=0) because the sim has "
              "no mid-run spawn verb._", ""]

    ap = _rows(con, "SELECT * FROM agent_params ORDER BY generation, agent")
    if ap:
        L += ["## Lab evolution (agent parameters by generation)", "",
              "| generation | agent | temperature | base confidence | fitness (mean Brier, prior gen) |",
              "|---|---|---|---|---|"]
        for a in ap:
            f = f"{a['fitness']:.2f}" if a["fitness"] is not None else "—"
            L.append(f"| {a['generation']} | {a['agent']} | {a['temperature']:.2f} | "
                     f"{a['base_confidence']:.2f} | {f} |")
        L.append("")

    cred = _rows(con, "SELECT * FROM credibility ORDER BY agent, domain")
    if cred:
        L += ["## Credibility (Brier-updated vote weights)", "",
              "| agent | domain | weight | scored |", "|---|---|---|---|"]
        for c in cred:
            L.append(f"| {c['agent']} | {c['domain']} | {c['weight']:.2f} | {c['n_scored']} |")
        L.append("")

    L += ["## Open questions", ""]
    for q in _rows(con, "SELECT * FROM open_questions ORDER BY id"):
        L.append(f"- {q['id']} ({q['status']}): {q['text']}")
    L.append("")

    waiting = [e for e in exps if e["status"] == "awaiting-sim"]
    if waiting:
        L += ["## Awaiting the sim machine", "",
              "Run these on the machine with UE 5.7, then "
              "`python -m Lab.lab run-queued`:", "", "```"]
        for e in waiting:
            p = json.loads(e["protocol"])
            for mode, spec in ((p["intervention_mode"], p["intervention_set"]),
                               (p["control_mode"], p["control_set"])):
                seeds = " ".join(str(s) for s in p["seeds"])
                cmd = (f"python Tools/run_sim.py --mode {mode} --seed {seeds} "
                       f"--duration {p['duration']:.0f}")
                if spec:
                    cmd += f' --set "{spec}"'
                L.append(f"{cmd}   # {e['id']} arm")
        L += ["```", ""]

    L += ["## Minutes", ""]
    for m in _rows(con, "SELECT * FROM meetings ORDER BY id"):
        L.append(f"**Meeting {m['id']}** ({m['kind']}, {m['started_at']}): {m['minutes']}")
    L.append("")

    if annex_lines:
        L += annex_lines

    report_path.write_text("\n".join(L))

    # --- transcript ---
    T = [f"# Symbiotic Lab — transcript {stamp}", ""]
    cur_meeting = None
    for t in _rows(con, "SELECT * FROM transcript ORDER BY meeting_id, id"):
        if t["meeting_id"] != cur_meeting:
            cur_meeting = t["meeting_id"]
            T += [f"## Meeting {cur_meeting}", ""]
        payload = json.loads(t["payload"])
        T.append(f"**[{t['round']}] {t['agent']}:**")
        T.append("```json")
        T.append(json.dumps(payload, indent=2))
        T.append("```")
        T.append("")
    # stance ledger inline (the discourse core, human-readable)
    T += ["## Stance ledger", ""]
    for s in _rows(con, "SELECT * FROM stances ORDER BY meeting_id, hypothesis_id, id"):
        flag = " (downgraded to abstain: citation rule)" if s["downgraded"] else ""
        T.append(f"- m{s['meeting_id']} {s['hypothesis_id']} {s['agent']}: "
                 f"**{s['stance']}** ({s['confidence']:.2f}) — {s['reason']} "
                 f"[{', '.join(json.loads(s['evidence_ids']))}]{flag}")
    transcript_path.write_text("\n".join(T))
    return report_path, transcript_path
