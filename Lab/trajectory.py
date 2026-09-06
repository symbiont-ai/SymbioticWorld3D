"""Discourse trajectory: is the conversation getting better, measurably?

Improvement is never asserted, it is computed from what the lab already
records — nothing here asks a model anything:

  per meeting     findings presented and citations that survived validation,
                  stance mix, DOWNGRADES (agree/disagree that failed the
                  citation rules and were recorded abstain — the discourse
                  error rate), new cards, disputes, the experiment designed.
  per experiment  (in design order) the team's mean Brier and its best/worst
                  call — the calibration curve of the lab's predictions.
  per generation  credibility spread (differentiated floor time) and the
                  temperament mutations evolution applied.

Reading it: a healthier conversation shows a falling downgrade rate (fewer
invalid citations), disputes that resolve instead of accumulating, a falling
mean Brier (better-calibrated predictions), and a credibility spread that
widens away from 1.0 (being right in public started paying).
"""
import json

from . import db


def meeting_rows(con):
    out = []
    for m in con.execute("SELECT * FROM meetings ORDER BY id"):
        mid = m["id"]
        findings = n_cited = 0
        for t in con.execute(
                "SELECT payload FROM transcript WHERE meeting_id=? AND round='EVIDENCE'", (mid,)):
            try:
                kept = json.loads(t["payload"])
            except (TypeError, ValueError):
                continue
            findings += len(kept)
            n_cited += sum(len(f.get("evidence_ids", [])) for f in kept)
        st = con.execute(
            "SELECT COUNT(*) AS n,"
            " SUM(stance='agree') AS agree, SUM(stance='disagree') AS disagree,"
            " SUM(stance='abstain') AS abstain, SUM(downgraded) AS downgraded "
            "FROM stances WHERE meeting_id=?", (mid,)).fetchone()
        cards = con.execute("SELECT COUNT(*) FROM hypotheses WHERE created_meeting=?",
                            (mid,)).fetchone()[0]
        exp = con.execute("SELECT id FROM experiments WHERE created_meeting=?",
                          (mid,)).fetchone()
        out.append({
            "meeting": mid, "kind": m["kind"], "findings": findings,
            "citations": n_cited, "stances": st["n"] or 0,
            "disagree": st["disagree"] or 0,
            "downgraded": st["downgraded"] or 0,
            "downgrade_rate": (st["downgraded"] or 0) / st["n"] if st["n"] else 0.0,
            "new_cards": cards, "experiment": exp["id"] if exp else "",
        })
    return out


def calibration_rows(con):
    out = []
    for e in con.execute("SELECT * FROM experiments WHERE verdict IS NOT NULL "
                         "ORDER BY id"):
        preds = con.execute(
            "SELECT agent, brier FROM predictions WHERE experiment_id=? AND brier "
            "IS NOT NULL ORDER BY brier", (e["id"],)).fetchall()
        if not preds:
            continue
        briers = [p["brier"] for p in preds]
        out.append({
            "experiment": e["id"], "verdict": e["verdict"], "n": len(preds),
            "mean_brier": sum(briers) / len(briers),
            "best": f"{preds[0]['agent']} {preds[0]['brier']:.2f}",
            "worst": f"{preds[-1]['agent']} {preds[-1]['brier']:.2f}",
        })
    return out


def credibility_spread(con):
    ws = [r["weight"] for r in con.execute("SELECT weight FROM credibility")]
    if not ws:
        return None
    mean = sum(ws) / len(ws)
    var = sum((w - mean) ** 2 for w in ws) / len(ws)
    return {"n": len(ws), "mean": mean, "sd": var ** 0.5,
            "min": min(ws), "max": max(ws)}


def report_section(con):
    L = ["## Discourse trajectory (computed, not asserted)", ""]

    rows = meeting_rows(con)
    if rows:
        L += ["Per meeting — a healthier conversation shows a falling downgrade "
              "rate (invalid citations) and disputes that become experiments:", "",
              "| meeting | kind | findings | citations | stances | disagree | "
              "downgraded | new cards | experiment |",
              "|---|---|---|---|---|---|---|---|---|"]
        for r in rows:
            L.append(f"| {r['meeting']} | {r['kind']} | {r['findings']} | "
                     f"{r['citations']} | {r['stances']} | {r['disagree']} | "
                     f"{r['downgraded']} ({r['downgrade_rate']:.0%}) | "
                     f"{r['new_cards']} | {r['experiment']} |")
        L.append("")

    cal = calibration_rows(con)
    if cal:
        L += ["Calibration, in design order — the falling (or not) mean Brier of "
              "the team's preregistered predictions:", "",
              "| experiment | verdict | mean Brier | best call | worst call |",
              "|---|---|---|---|---|"]
        for r in cal:
            L.append(f"| {r['experiment']} | {r['verdict']} | {r['mean_brier']:.3f} "
                     f"| {r['best']} | {r['worst']} |")
        L.append("")

    cs = credibility_spread(con)
    if cs:
        L += [f"Credibility spread: {cs['n']} agent-domain weights, mean "
              f"{cs['mean']:.2f}, sd {cs['sd']:.2f}, range {cs['min']:.2f}-"
              f"{cs['max']:.2f}. A spread away from a flat 1.0 means being "
              "right in public is buying floor time and docket priority.", ""]
    return L
