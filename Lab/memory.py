"""Registry ("the textbook"), promotion rules, notebooks, consolidation.

Nothing becomes knowledge without a preregistered prediction that passed;
these rules are enforced here in code, not by any model's judgment (PRD F5).

Promotion:
  conjecture -> supported : >=1 experiment on the card with a control arm and a
                            passed preregistered direction, and no Karla veto
                            standing on that experiment.
  supported  -> law       : >=2 passing experiments, together covering >=2
                            seeds, and Karla's latest stance on the card is not
                            'disagree' (her veto on law is absolute).
  any failed prediction   -> automatic demotion review: supported falls back to
                            conjecture; a conjecture whose preregistered
                            direction came out OPPOSITE beyond threshold is
                            refuted.
"""
import json

from . import db


def create_card(con, claim, mechanism, scope, proposer, meeting_id,
                status="conjecture", evidence_ids=()):
    hid = db.next_id(con, "hypotheses", "H")
    con.execute("INSERT INTO hypotheses VALUES(?,?,?,?,?,?,?)",
                (hid, claim, mechanism, scope, status, proposer, meeting_id))
    for eid in evidence_ids:
        if db.evidence_exists(con, eid):
            con.execute("INSERT OR IGNORE INTO hypothesis_evidence VALUES(?,?)", (hid, eid))
    return hid


def active_cards(con, limit):
    return con.execute(
        "SELECT * FROM hypotheses WHERE status IN ('conjecture','supported') "
        "ORDER BY id DESC LIMIT ?", (limit,)).fetchall()


def caveat_cards(con):
    return con.execute("SELECT * FROM hypotheses WHERE status='caveat'").fetchall()


def card_evidence_ids(con, hid):
    return [r["evidence_id"] for r in con.execute(
        "SELECT evidence_id FROM hypothesis_evidence WHERE hypothesis_id=?", (hid,))]


def latest_stance(con, hid, agent):
    row = con.execute(
        "SELECT stance FROM stances WHERE hypothesis_id=? AND agent=? "
        "ORDER BY id DESC LIMIT 1", (hid, agent)).fetchone()
    return row["stance"] if row else None


def _passing_experiments(con, hid):
    rows = con.execute(
        "SELECT * FROM experiments WHERE hypothesis_id=? AND status='done' AND verdict='pass'",
        (hid,)).fetchall()
    return rows


def apply_experiment_outcome(con, exp_id, log=print):
    """Called by the runner after the metric verdict is computed. Applies the
    promotion/demotion rules and resolves the docketed dispute, all in code."""
    exp = con.execute("SELECT * FROM experiments WHERE id=?", (exp_id,)).fetchone()
    card = con.execute("SELECT * FROM hypotheses WHERE id=?", (exp["hypothesis_id"],)).fetchone()
    if not card or card["status"] == "caveat":
        return
    hid, status, verdict = card["id"], card["status"], exp["verdict"]
    new_status = status

    if verdict == "pass":
        passing = _passing_experiments(con, hid)
        seeds = set()
        for e in passing:
            seeds.update(json.loads(e["protocol"]).get("seeds", []))
        karla_veto = latest_stance(con, hid, "Karla") == "disagree"
        if status == "conjecture":
            new_status = "supported"
        if len(passing) >= 2 and len(seeds) >= 2 and not karla_veto:
            new_status = "law"
        elif len(passing) >= 2 and karla_veto:
            log(f"  [registry] {hid}: law promotion blocked by standing Karla veto")
    elif verdict in ("fail", "opposite"):
        if status == "supported":
            new_status = "conjecture"
            log(f"  [registry] {hid}: demotion review -> back to conjecture")
        elif status == "conjecture" and verdict == "opposite":
            new_status = "refuted"

    if new_status != status:
        con.execute("UPDATE hypotheses SET status=? WHERE id=?", (new_status, hid))
        log(f"  [registry] {hid}: {status} -> {new_status} (experiment {exp_id}: {verdict})")

    con.execute(
        "UPDATE disputes SET resolution=? WHERE experiment_id=? AND resolution='open'",
        (f"resolved:{verdict}", exp_id))
    con.commit()


# --- notebooks ---------------------------------------------------------------

NOTEBOOK_CHAR_CAP = 2000   # ~500 tokens; Archie condenses beyond this


def append_notebook(con, agent, line):
    doc = db.get_notebook(con, agent)
    db.set_notebook(con, agent, (doc + "\n" + line).strip())


def condense_notebooks(con, log=print):
    """Consolidation ('sleep'): keep the newest lines under the cap. Oldest
    positions fall away; grudges survive as long as they stay recent enough
    to matter."""
    for row in con.execute("SELECT agent, doc FROM notebooks").fetchall():
        doc = row["doc"]
        if len(doc) <= NOTEBOOK_CHAR_CAP:
            continue
        lines = doc.splitlines()
        keep = []
        total = 0
        for ln in reversed(lines):
            total += len(ln) + 1
            if total > NOTEBOOK_CHAR_CAP:
                break
            keep.append(ln)
        db.set_notebook(con, row["agent"], "\n".join(reversed(keep)))
        log(f"  [consolidate] condensed {row['agent']}'s notebook "
            f"({len(lines)} -> {len(keep)} lines)")
    con.commit()


def textbook(con):
    """The inherited object: registry + open questions + proven protocols."""
    cards = con.execute("SELECT * FROM hypotheses ORDER BY id").fetchall()
    oq = con.execute("SELECT * FROM open_questions WHERE status='open'").fetchall()
    protos = con.execute(
        "SELECT id, hypothesis_id, protocol FROM experiments WHERE verdict='pass'").fetchall()
    return {"cards": [dict(c) for c in cards],
            "open_questions": [dict(q) for q in oq],
            "proven_protocols": [dict(p) for p in protos]}
