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
    ("lumen_min_n and tecton_min_n are the opening-seconds trough, not a late-run outcome: "
     "they cannot test a claim about foraging, exploration or energy spent over a run. "
     "Experiment X-020 preregistered lumen_min_n for the novelty bonus and both arms returned "
     "exactly 40.0.", "measured 2026-09-17, experiment X-020"),
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
    # The Leviathan, the mutation width and the novelty bonus all landed in the sim after the
    # first three conjectures were written, so nothing on the docket could reach them: a claim
    # nobody states is a claim the design bench never gets to test.
    ("Leviathan predation, not starvation, sets the Lumen population floor.",
     "A strike removes a forager within 600 uu on a 5 s cooldown; if predation is what caps "
     "the population, the low-water mark has to move when the predator is switched off.",
     "Mode C, bLeviathan on vs off, >=3 seeds; lumen_min_n."),
    ("Predation selects for higher epsilon in Lumen lineages.",
     "Predation prices hesitation: foragers that linger in the strike zone die before they "
     "reproduce, so the surviving lineages should carry higher epsilon.",
     "Mode C, bLeviathan on vs off; inherited epsilon at run end."),
    ("The novelty bonus (WeightNovelty=0.2) spends the Lumen energy budget without buying "
     "exploration.",
     "wN rewards unvisited cells, but the patch dry margin puts the food at the valley rims: "
     "the explore leg may cost more energy than the novelty ever finds.",
     "Mode C, wN=0 vs 0.2; motivated by the 2026-09-14 river-crossing counter-result."),
    ("Inheritance fidelity sets the speed of selection: widening MutationSigma decouples "
     "parent and offspring alpha.",
     "alpha is inherited with Gaussian noise of width MutationSigma=0.03, measured "
     "parent/child correlation 0.76; doubling the width should break that correlation.",
     "Mode C, sigma 0.06 vs 0.03; inherit_alpha_corr."),
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
