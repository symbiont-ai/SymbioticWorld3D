"""Embodied field observers: the seven scientists as virtual bodies in the arena.

Enabled with `python -m Lab.lab observe --embody` (a mode toggle; off keeps
the plain instrument-style observer). Each scientist gets a position in the
world, a per-profile movement style, a walking speed and a sense radius. The
positions advance on the sim's own logical clock (the `t` of each decide
message), and a scientist WITNESSES only the organisms inside their radius
that decision. Witnessed windows are minted as per-scientist evidence whose
provenance starts with "witnessed by <Name>", and the meetings' evidence
round only hands each observer their own witnessed rows (plus the shared
instrument evidence everyone can read), so findings differ because sampling
differed — not because anything was scripted.

Movement is deterministic per run (seeded from the run id), so an embodied
run is reproducible like everything else. No organism is touched: embodiment
is a pure read of the decide stream; driving organisms remains the manage
doctrine's job.
"""
import hashlib
import math

from . import db

SPEED = 260.0          # uu per logical second, all scientists
SENSE_RADIUS = 1200.0  # uu; a bit beyond an organism's NeighbourRange (900)
ARRIVE = 150.0         # uu; close enough to a waypoint

# name -> movement style (each style is a method on EmbodiedScientist)
STYLES = {
    "Vesper":  ("follow_species", "Lumen"),    # Lumen ethologist shadows the Lumen cluster
    "Bastion": ("follow_species", "Tecton"),   # Tecton engineer-watcher shadows Tecton
    "Mendel":  ("follow_youngest", None),      # geneticist chases the newest births
    "Ada":     ("seek_resources", None),       # theorist camps where food is reported
    "Fisher":  ("grid_sweep", None),           # statistician runs a systematic transect
    "Karla":   ("random_waypoints", None),     # skeptic samples where nobody chose to look
    "Archie":  ("spiral", None),               # archivist walks a slow outward spiral
}


def _seed_of(run_id, name):
    return int(hashlib.md5(f"{run_id}|{name}".encode()).hexdigest()[:8], 16)


class _Rng:
    """Tiny deterministic LCG so embodiment never touches global random state."""

    def __init__(self, seed):
        self.s = seed & 0x7FFFFFFF or 1

    def next(self):
        self.s = (self.s * 48271) % 0x7FFFFFFF
        return self.s / 0x7FFFFFFF

    def uniform(self, a, b):
        return a + (b - a) * self.next()


class EmbodiedScientist:
    def __init__(self, name, run_id, half):
        self.name = name
        self.half = half
        self.rng = _Rng(_seed_of(run_id, name))
        self.x = self.rng.uniform(-half * 0.5, half * 0.5)
        self.y = self.rng.uniform(-half * 0.5, half * 0.5)
        self.style, self.style_arg = STYLES[name]
        self.wp = None                       # current waypoint for waypoint styles
        self.grid_row = 0
        self.spiral_a = self.rng.uniform(0, 2 * math.pi)
        # per-window accumulators
        self.reset_window()

    def reset_window(self):
        self.seen = {"Lumen": set(), "Tecton": set()}
        self.alpha = {"Lumen": [], "Tecton": []}
        self.rewards = {"Lumen": [], "Tecton": []}
        self.actions = {"Lumen": {}, "Tecton": {}}

    # ---------------------------------------------------------------- motion

    def _toward(self, tx, ty, dt):
        dx, dy = tx - self.x, ty - self.y
        d = math.hypot(dx, dy)
        if d < 1e-6:
            return
        step = min(SPEED * dt, d)
        self.x += dx / d * step
        self.y += dy / d * step
        h = self.half
        self.x = max(-h, min(h, self.x))
        self.y = max(-h, min(h, self.y))

    def _centroid(self, agents, species):
        pts = [a["position"] for a in agents
               if a.get("species") == species and a.get("position")]
        if not pts:
            return None
        return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))

    def move(self, agents, dt):
        if dt <= 0:
            return
        if self.style == "follow_species":
            c = self._centroid(agents, self.style_arg)
            if c:
                self._toward(c[0], c[1], dt)
                return
        elif self.style == "follow_youngest":
            young = min((a for a in agents if a.get("position")),
                        key=lambda a: a.get("age", 1e9), default=None)
            if young:
                self._toward(young["position"][0], young["position"][1], dt)
                return
        elif self.style == "seek_resources":
            locs = [a["percept"]["resource_loc"] for a in agents
                    if (a.get("percept") or {}).get("resource_loc")]
            if locs:
                self._toward(sum(p[0] for p in locs) / len(locs),
                             sum(p[1] for p in locs) / len(locs), dt)
                return
        elif self.style == "grid_sweep":
            self._grid(dt)
            return
        elif self.style == "spiral":
            self.spiral_a += dt * SPEED / max(400.0, math.hypot(self.x, self.y) + 400.0)
            r = min(self.half * 0.9, math.hypot(self.x, self.y) + 30.0 * dt)
            self._toward(r * math.cos(self.spiral_a), r * math.sin(self.spiral_a), dt)
            return
        # default + random_waypoints + fallbacks: seeded waypoint wandering
        if self.wp is None or math.hypot(self.wp[0] - self.x, self.wp[1] - self.y) < ARRIVE:
            self.wp = (self.rng.uniform(-self.half, self.half),
                       self.rng.uniform(-self.half, self.half))
        self._toward(self.wp[0], self.wp[1], dt)

    def _grid(self, dt):
        h = self.half * 0.9
        rows = 8
        ty = -h + (2 * h) * (self.grid_row % rows) / (rows - 1)
        tx = h if self.grid_row % 2 == 0 else -h
        if math.hypot(tx - self.x, ty - self.y) < ARRIVE:
            self.grid_row += 1
            return
        self._toward(tx, ty, dt)

    # -------------------------------------------------------------- sensing

    def witness(self, agents):
        for a in agents:
            sp, pos = a.get("species"), a.get("position")
            if sp not in self.seen or not pos:
                continue
            if math.hypot(pos[0] - self.x, pos[1] - self.y) > SENSE_RADIUS:
                continue
            self.seen[sp].add(a.get("id"))
            g = a.get("genome") or {}
            if "alpha" in g:
                self.alpha[sp].append(g["alpha"])
            if a.get("last_reward") is not None:
                self.rewards[sp].append(a["last_reward"])
            la = a.get("last_action")
            if la:
                self.actions[sp][la] = self.actions[sp].get(la, 0) + 1


class EmbodiedField:
    """All seven bodies plus per-window evidence minting."""

    def __init__(self, run_id, world_half_size):
        self.run_id = run_id
        self.team = [EmbodiedScientist(n, run_id, float(world_half_size or 4500.0))
                     for n in STYLES]
        self.last_t = None

    def step(self, t, agents):
        dt = 0.0 if self.last_t is None else max(0.0, t - self.last_t)
        self.last_t = t
        for s in self.team:
            s.move(agents, dt)
            s.witness(agents)

    def flush(self, con, window_index, t0, t1):
        """Mint per-scientist witnessed evidence + a track row; reset windows."""
        lines = []
        for s in self.team:
            wid = f"live:{self.run_id}:w{window_index}:{s.name.lower()}"

            def _prov(desc):
                # the "witnessed by <Name> " prefix is the digest filter's key
                return (f"witnessed by {s.name} — {desc}, within "
                        f"{SENSE_RADIUS:.0f} uu of ({s.x:.0f},{s.y:.0f}), "
                        f"sim window {t0:.0f}-{t1:.0f}s, run {self.run_id}")

            n_tot = 0
            for sp in ("Lumen", "Tecton"):
                n = len(s.seen[sp])
                n_tot += n
                if n == 0:
                    continue
                pre = f"obs_{sp.lower()}"
                db.add_evidence(con, wid, f"{pre}_n", float(n),
                                _prov(f"distinct {sp}"))
                if s.alpha[sp]:
                    db.add_evidence(con, wid, f"{pre}_mean_alpha",
                                    sum(s.alpha[sp]) / len(s.alpha[sp]),
                                    _prov(f"mean inherited alpha of {sp}"))
                if s.rewards[sp]:
                    db.add_evidence(con, wid, f"{pre}_mean_reward",
                                    sum(s.rewards[sp]) / len(s.rewards[sp]),
                                    _prov(f"mean per-decision reward of {sp}"))
                acts = s.actions[sp]
                total = sum(acts.values())
                if total:
                    top = max(acts, key=acts.get)
                    db.add_evidence(con, wid, f"{pre}_top_action_{top}_frac",
                                    acts[top] / total,
                                    _prov(f"most-chosen action of witnessed {sp} "
                                          f"({total} decisions)"))
            con.execute(
                "INSERT INTO embodiment(run_id, window, agent, x, y, seen, t) "
                "VALUES(?,?,?,?,?,?,?)",
                (f"live:{self.run_id}", window_index, s.name,
                 s.x, s.y, n_tot, t1))
            lines.append(f"{s.name}@({s.x:.0f},{s.y:.0f}) saw {n_tot}")
            s.reset_window()
        con.commit()
        return "field: " + ", ".join(lines)
