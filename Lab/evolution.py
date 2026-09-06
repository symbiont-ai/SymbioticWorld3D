"""Evolution of the lab itself (spec M4, extended).

Two timescales, both driven by being right or wrong in public:

1. Within a generation — credibility (scorer.py) moves on every scored
   prediction AND on every resolved dispute (the side that called it gains,
   the side that fought it loses). Credibility is a live competitive
   advantage: it ranks the design docket, weights votes, and grants
   high-credibility observers an extra evidence slot.

2. Between generations — at a generation boundary each agent's fitness is its
   mean Brier over the generation's scored predictions (lower = fitter).
   Below-median agents mutate toward the fittest agent's parameters
   (temperature, mock base-confidence) with Gaussian noise, clamped; identity,
   role, and priors are kept — the lab evolves temperament, not personality.
   Parameters are stored per generation in agent_params and overlaid on the
   YAML profiles at load time.

Knowledge compounds in the textbook; egos are ranked every generation.
"""
import random

from . import db
from .profiles import TURN_ORDER

TEMP_MIN, TEMP_MAX = 0.1, 1.2
CONF_MIN, CONF_MAX = 0.35, 0.9
MUT_SIGMA = 0.05
PULL = 0.3        # fraction moved toward the fittest agent's value


def generation(con):
    return int(db.get_meta(con, "generation", 0))


def generation_fitness(con, gen):
    """Mean Brier per agent over every scored prediction to date. Lifetime
    accuracy, not per-generation windows: experiments often resolve across a
    generation boundary (queued in one session, scored on the sim machine
    later), and a windowed tally would drop exactly those."""
    rows = con.execute(
        "SELECT agent, AVG(brier) AS f, COUNT(*) AS n FROM predictions "
        "WHERE brier IS NOT NULL GROUP BY agent").fetchall()
    return {r["agent"]: (r["f"], r["n"]) for r in rows}


def _current_params(con, profiles, gen):
    out = {}
    for name in TURN_ORDER:
        if name == "Archie":
            continue
        row = con.execute("SELECT * FROM agent_params WHERE agent=? AND generation=?",
                          (name, gen)).fetchone()
        if row:
            out[name] = {"temperature": row["temperature"],
                         "base_confidence": row["base_confidence"]}
        else:
            p = profiles[name]
            out[name] = {"temperature": p.temperature,
                         "base_confidence": float(p.mock.get("base_confidence", 0.6))}
    return out


def evolve(con, profiles, rng=None, log=print):
    """Close the current generation: score fitness, mutate the below-median
    half toward the fittest agent, open generation g+1."""
    rng = rng or random.Random(1234 + generation(con))
    gen = generation(con)
    fitness = generation_fitness(con, gen)
    params = _current_params(con, profiles, gen)

    scored = {a: f for a, (f, n) in fitness.items() if a in params}
    if len(scored) < 2:
        log("  [evolution] fewer than 2 agents scored this generation; lab unchanged")
        db.set_meta(con, "generation", gen + 1)
        _persist(con, gen + 1, params, fitness)
        con.commit()
        return gen + 1

    best = min(scored, key=scored.get)
    median = sorted(scored.values())[len(scored) // 2]
    log(f"  [evolution] generation {gen} closes: fittest {best} "
        f"(Brier {scored[best]:.2f}); median {median:.2f}")

    for agent, f in scored.items():
        if agent == best or f <= median:
            continue
        for key, lo, hi in (("temperature", TEMP_MIN, TEMP_MAX),
                            ("base_confidence", CONF_MIN, CONF_MAX)):
            old = params[agent][key]
            target = params[best][key]
            new = old + PULL * (target - old) + rng.gauss(0, MUT_SIGMA)
            params[agent][key] = max(lo, min(hi, new))
        log(f"  [evolution] {agent} (Brier {f:.2f}) mutates toward {best}: "
            f"temp {params[agent]['temperature']:.2f}, "
            f"conf {params[agent]['base_confidence']:.2f}")

    db.set_meta(con, "generation", gen + 1)
    _persist(con, gen + 1, params, fitness)
    con.commit()
    return gen + 1


def _persist(con, new_gen, params, fitness):
    for agent, p in params.items():
        f = fitness.get(agent, (None, 0))[0]
        con.execute(
            "INSERT OR REPLACE INTO agent_params VALUES(?,?,?,?,?)",
            (agent, new_gen, p["temperature"], p["base_confidence"], f))
