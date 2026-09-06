"""Day-one seed content (PRD §5) — no cold start. Open questions come from
DESIGN.md §6; caveat cards are the sim's documented biases; the conjectures are
deliberately chosen so the profiles will fight over them. Idempotent."""
from . import memory

OPEN_QUESTIONS = [
    ("OQ-1", "Does the regen-6 stable band hold across seeds 1-5 at 1800 s?",
     "DESIGN.md §6"),
    ("OQ-2", "Is turnover (births ~ deaths ~ 300 per 600 s) fast enough to see "
             "selection on alpha within a run?", "DESIGN.md §6"),
    ("OQ-3", "Does drought at 0.3x regen from the regen-6 baseline cause a visible "
             "but survivable population dip?", "DESIGN.md §6"),
]

CAVEATS = [
    ("The wI interaction-reward weight (WeightInteraction=0.10) is what makes "
     "'modify' learnable at all for a gamma=0 bandit; any modify-learning claim "
     "must state it.", "DESIGN.md §4 documented bias"),
    ("Lifetime learning is a tabular contextual bandit with gamma=0 — it is not "
     "Q-learning and cannot credit delayed consequences on its own.", "DESIGN.md §1"),
    ("The genome parameter m (memory persistence) was dropped as redundant with "
     "alpha for a constant-step-size update; do not theorize about it.", "DESIGN.md §2"),
    ("The 2026-09-05 seed-stream change (4 Gaussians per birth, not 3) means runs "
     "recorded before it do not reproduce number-for-number with the same seed.",
     "DESIGN.md §5"),
]

CONJECTURES = [
    ("Drought selects for higher epsilon in Lumen lineages.",
     "Scarcer, shifting resources reward exploration; energy-gated reproduction "
     "then favors higher-epsilon genomes.",
     "Mode C, drought vs no-drought, >=2 seeds."),
    ("Tecton Trace Y engineering net-helps the Lumen population.",
     "Trace Y multiplies patch regrowth (1 + 1.5*TraceY); if Lumen forage the "
     "engineered cells, Tecton soil work raises Lumen carrying capacity.",
     "Mode C; the spec refuses to hard-code this — it must be shown or refuted."),
    ("High-e lineages win under drought but lose in times of plenty.",
     "e scales both the Trace deposit and its energy cost; the cost only pays "
     "off when regrowth is the binding constraint.",
     "Mode C, compare env_effect trajectories across resource regimes."),
]


def seed(con, log=print):
    from . import victory
    n = 0
    for qid, text, origin in OPEN_QUESTIONS:
        if not con.execute("SELECT 1 FROM open_questions WHERE id=?", (qid,)).fetchone():
            con.execute("INSERT INTO open_questions VALUES(?,?,?, 'open')", (qid, text, origin))
            n += 1
    existing = {r["claim"] for r in con.execute("SELECT claim FROM hypotheses")}
    for claim, origin in CAVEATS:
        if claim not in existing:
            memory.create_card(con, claim, "documented implementation bias", origin,
                               "Textbook", 0, status="caveat")
            n += 1
    for claim, mech, scope in CONJECTURES:
        if claim not in existing:
            memory.create_card(con, claim, mech, scope, "Textbook", 0, status="conjecture")
            n += 1
    con.commit()
    victory.sync_questions(con)   # spec §1 victory conditions -> standing open questions
    if n:
        log(f"  [seed] {n} textbook objects seeded")
