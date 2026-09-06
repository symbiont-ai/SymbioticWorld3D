"""Prediction scoring (PRD F3: stances are predictions).

Three-way Brier score over {treatment_higher, treatment_lower, no_difference}:
the agent's stated confidence goes on its predicted outcome, the remainder is
split over the other two. Lower is better; 0 = confident and right,
~2 = confident and wrong.

Credibility: per (agent, domain), starts at 1.0, multiplicative update
clamped to [0.25, 2.0]. A Brier of 0.25 (decent) leaves weight ~unchanged;
being confidently wrong shrinks it. Being wrong publicly and often gets you
outvoted — the lab's version of selection.
"""
OUTCOMES = ("treatment_higher", "treatment_lower", "no_difference")


def brier(predicted, confidence, actual):
    confidence = max(0.0, min(1.0, float(confidence)))
    rest = (1.0 - confidence) / 2.0
    p = {o: (confidence if o == predicted else rest) for o in OUTCOMES}
    return sum((p[o] - (1.0 if o == actual else 0.0)) ** 2 for o in OUTCOMES)


def update_credibility(con, agent, domains, brier_score):
    from . import db
    factor = max(0.6, min(1.25, 1.25 - brier_score))
    for d in domains or ["behavior"]:
        row = con.execute("SELECT weight, n_scored FROM credibility WHERE agent=? AND domain=?",
                          (agent, d)).fetchone()
        w, n = (row["weight"], row["n_scored"]) if row else (1.0, 0)
        db.set_credibility(con, agent, d, max(0.25, min(2.0, w * factor)), n + 1)


def adjust_for_stance(con, agent, domains, was_right):
    """Discourse has consequences: when an experiment resolves a card, the side
    that called it gains credibility and the side that fought it loses some.
    Smaller than the Brier update — stances are cheaper talk than predictions."""
    from . import db
    factor = 1.08 if was_right else 0.92
    for d in domains or ["behavior"]:
        row = con.execute("SELECT weight, n_scored FROM credibility WHERE agent=? AND domain=?",
                          (agent, d)).fetchone()
        w, n = (row["weight"], row["n_scored"]) if row else (1.0, 0)
        db.set_credibility(con, agent, d, max(0.25, min(2.0, w * factor)), n)


def vote_weight(con, agent, domains):
    from . import db
    ds = domains or ["behavior"]
    return sum(db.get_credibility(con, agent, d) for d in ds) / len(ds)
