"""Population management policy: the lab's hands on the live world.

When the bridge runs with --manage, every organism the sim assigns to us is
driven by this policy instead of its built-in bandit. The policy is
deterministic, table-free, and legible — the scientists' management doctrine
written as code, not a black box:

  goals    keep each species between a FLOOR and a TARGET; below the floor,
           everything is survival; between floor and target, feed and grow;
           at/above target, invest in the environment (Trace X marks, Trace Y
           soil work) and the information economy (signals) so the world's
           carrying capacity rises instead of the population overshooting.

  per organism (all inputs from the decide message; every choice is checked
  against the feasibility mask, and None hands the organism back to its own
  bandit for that decision):
    LOW energy      -> forage, else follow (signal/trail), else explore
    MID energy      -> below floor: forage/explore;
                       otherwise a deterministic slice of Lumen signal or mark
                       (Trace X) and Tecton work soil where a patch needs it
    HIGH energy     -> below target: keep foraging (energy-gated reproduction
                       does the rest); at target: Tecton soil work, Lumen
                       marks/signals, rest
    crowded + LOW   -> avoid (spread away from overgrazed ground)

Replies must stay under a few ms for the whole cohort (POLICY_API.md timing):
this is pure dict logic, no I/O, no model. Determinism caveat from the API
applies: a managed run reproduces only with the same manager version.
"""

ACTIONS = ["forage", "explore", "follow", "avoid", "signal", "rest", "modify"]
IDX = {a: i for i, a in enumerate(ACTIONS)}

FLOOR = {"Lumen": 20, "Tecton": 8}
TARGET = {"Lumen": 60, "Tecton": 25}
CROWD_DIST = 160.0        # uu; tighter than the sim's CrowdRadius on purpose
INVEST_SLICE = 3          # 1 in N of eligible mid-energy organisms invests


class PopulationManager:
    """Stateless per decision except the rolling population estimate and the
    doctrine. The doctrine (floors/targets) belongs to the SCIENTISTS: each
    meeting's MANAGEMENT round writes it to lab_meta, and the bridge reloads
    it here every evidence window — the lab deliberates on the minutes
    timescale, this policy executes on the milliseconds timescale."""

    def __init__(self):
        self.pop = {"Lumen": 0, "Tecton": 0}
        self.floor = dict(FLOOR)
        self.target = dict(TARGET)
        self._seen = {"Lumen": set(), "Tecton": set()}
        self._seen_t = 0.0

    def load_doctrine(self, con):
        from . import db
        changed = []
        for sp in ("Lumen", "Tecton"):
            f = db.get_meta(con, f"doctrine_floor_{sp}")
            t = db.get_meta(con, f"doctrine_target_{sp}")
            if f is not None and int(f) != self.floor[sp]:
                self.floor[sp] = int(f); changed.append(f"{sp} floor->{f}")
            if t is not None and int(t) != self.target[sp]:
                self.target[sp] = int(t); changed.append(f"{sp} target->{t}")
        return changed

    def observe_cohort(self, t, agents):
        """Refresh the live population estimate about once per sim-minute."""
        for a in agents:
            sp = a.get("species")
            if sp in self._seen:
                self._seen[sp].add(a.get("id"))
        if t - self._seen_t >= 60.0:
            for sp in self._seen:
                if self._seen[sp]:
                    self.pop[sp] = len(self._seen[sp])
                self._seen[sp] = set()
            self._seen_t = t

    def act(self, a):
        """Return an action name, or None to leave this decision to the
        organism's own bandit. Only ever returns feasible actions."""
        mask = a.get("mask") or [0] * 7
        sp = a.get("species")
        if sp not in FLOOR:
            return None
        ok = lambda name: bool(mask[IDX[name]])
        p = a.get("percept") or {}
        n, floor, target = self.pop.get(sp, 0), self.floor[sp], self.target[sp]
        binname = a.get("bin", "MID")

        if binname == "LOW":
            if ok("avoid") and (p.get("nearest_any_agent_dist") or 1e9) < CROWD_DIST \
                    and not p.get("resource_known"):
                return "avoid"                      # starving in a crowd: spread out
            for act in ("forage", "follow", "explore"):
                if ok(act):
                    return act
            return "rest" if ok("rest") else None

        invest_turn = (a.get("id", 0) + a.get("decisions", 0)) % INVEST_SLICE == 0

        if binname == "MID":
            if n < floor:
                return "forage" if ok("forage") else ("explore" if ok("explore") else None)
            if invest_turn:
                if sp == "Tecton" and ok("modify") and p.get("patch_in_cell_needs_soil"):
                    return "modify"                 # soil work where it pays
                if sp == "Lumen" and ok("signal"):
                    return "signal"                 # tell the others where food is
                if sp == "Lumen" and ok("modify") and (p.get("trace_x") or 0.0) < 0.3:
                    return "modify"                 # mark an unmarked food site
            return "forage" if ok("forage") else ("explore" if ok("explore") else None)

        # HIGH energy
        if n < target:
            return "forage" if ok("forage") else ("rest" if ok("rest") else None)
        if sp == "Tecton" and ok("modify"):
            return "modify"
        if sp == "Lumen" and invest_turn and ok("signal"):
            return "signal"
        if sp == "Lumen" and ok("modify") and (p.get("trace_x") or 0.0) < 0.5:
            return "modify"
        return "rest" if ok("rest") else None

    def summary(self):
        return (f"managing: Lumen {self.pop['Lumen']} "
                f"(floor {self.floor['Lumen']}/target {self.target['Lumen']}), "
                f"Tecton {self.pop['Tecton']} "
                f"(floor {self.floor['Tecton']}/target {self.target['Tecton']})")
