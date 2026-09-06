"""Profile loading. One profile = one YAML file = one system prompt + temperature.
Swappable without code changes (PRD F7)."""
from pathlib import Path

import yaml

from . import config

TURN_ORDER = ["Vesper", "Bastion", "Mendel", "Ada", "Fisher", "Karla", "Archie"]
OBSERVERS = ["Vesper", "Bastion", "Mendel"]
# Non-voting staff: loaded, but never in a meeting round, never a stance,
# never a vote. Vega's outputs go to the report annex only.
NON_VOTING = ["Vega"]


class Profile:
    def __init__(self, d):
        self.name = d["name"]
        self.role = d["role"]
        self.domains = d.get("domains", [])
        self.temperature = float(d.get("temperature", 0.7))
        self.veto_rights = bool(d.get("veto_rights", False))
        self.non_voting = bool(d.get("non_voting", False))
        self.priors = d.get("priors", [])
        self.speaking_style = d.get("speaking_style", "")
        self.mock = d.get("mock", {})

    def system_prompt(self):
        priors = "\n".join(f"- {p}" for p in self.priors)
        return (
            f"You are {self.name}, {self.role} in the Symbiotic Lab, a scientist team "
            "studying the Symbiotic World ecosystem simulation.\n"
            "The sim: two species (Lumen, Tecton) with a tabular contextual bandit for "
            "lifetime learning (alpha=learning rate, epsilon=exploration, gamma=0 so NOT "
            "Q-learning) and asexual reproduction with Gaussian mutation of the genome "
            "{alpha, epsilon, social, e}. Modes: A=learning off, B=learning only, "
            "C=learning+evolution, N=neutral drift control. Trace X/Y are shared "
            "environment fields written by the 'modify' action.\n"
            f"Your standing priors:\n{priors}\n"
            f"Style: {self.speaking_style}\n"
            "Rules: cite evidence ONLY by the E-xxx IDs you are given; never invent "
            "numbers or IDs; never use the words 'intelligence', 'emergent', or "
            "'cooperation' as claims. Respond ONLY with JSON matching the requested schema."
        )


def load_profiles(profile_dir=None, con=None):
    """Load YAML profiles; when a lab DB is given, overlay the evolved
    parameters of the current lab generation (see evolution.py)."""
    d = Path(profile_dir) if profile_dir else config.PROFILE_DIR
    out = {}
    for p in sorted(d.glob("*.yaml")):
        prof = Profile(yaml.safe_load(p.read_text()))
        out[prof.name] = prof
    missing = [n for n in TURN_ORDER if n not in out]
    if missing:
        raise RuntimeError(f"missing profiles: {missing}")
    if con is not None:
        from . import db
        gen = int(db.get_meta(con, "generation", 0))
        for row in con.execute(
                "SELECT * FROM agent_params WHERE generation=?", (gen,)).fetchall():
            if row["agent"] in out:
                prof = out[row["agent"]]
                prof.temperature = row["temperature"]
                prof.mock["base_confidence"] = row["base_confidence"]
    return out
