"""Embodied field observers: eight bodies (the seven voting scientists plus Vega) in the arena.

Enabled with `python -m Lab.lab observe --embody` (a mode toggle; off keeps
the plain instrument-style observer). Each scientist gets a position in the
world, a target rule, a walking speed and a sense radius. The positions
advance on the sim's own logical clock (the `t` of each decide message), and
a scientist WITNESSES only the organisms inside their radius that decision.
Witnessed windows are minted as per-scientist evidence whose provenance
starts with "witnessed by <Name>", and the meetings' evidence round only
hands each observer their own witnessed rows (plus the shared instrument
evidence everyone can read), so findings differ because sampling differed —
not because anything was scripted.

Where they go (since 2026-09-14): each scientist walks to the nearest organism
that fits their rule (Vesper the nearest Lumen, Bastion the nearest Tecton,
Mendel the nearest of the five youngest, everyone else the nearest organism
nobody on the team is already watching — a colleague's organism is never
taken from them), stops OBSERVE_DIST short of it and
follows it when it moves, switching only when another candidate is clearly
nearer; once there they stand and watch until it has drifted RESUME_DIST
away, so a slowly moving organism does not make them shuffle after it. They
keep to land: the sim's hello carries a coarse water mask
(`water_mask`, 200 uu cells from the same terrain test as the organisms'
`on_land` percept) and the route is planned over it with water cells costing
WATER_COST times land, so a short detour along the bank beats a crossing and
a crossing happens only when the target is on the other side. On water they
ride a jet ski at JETSKI_SPEED (the sim draws it from its own water test).
Route searches (A*) are the only real work per decide message and are capped
at REPLANS_PER_STEP, granted round-robin: the sim waits at most 200 ms for the
bridge's reply, and a slow field team would stall the whole run.
Without a mask (an older sim) the route is the straight line, as before.

Candidates come from every organism the decide stream has shown, not only the
current message: a decide message carries just the organisms due that substep
(about a tenth of the population), so the field keeps each organism's latest
report and forgets it after STALE_DECISIONS decision intervals of silence.

Movement is deterministic per run (the start position is seeded from the run
id; everything after is a pure function of the decide stream), so an embodied
run is reproducible like everything else. No organism is touched: embodiment
is a pure read of the decide stream; driving organisms remains the manage
doctrine's job.
"""
import hashlib
import heapq
import math

from . import db

SQRT2 = math.sqrt(2.0)
NEIGHBOURS = tuple((di, dj, SQRT2 if di and dj else 1.0)
                   for di in (-1, 0, 1) for dj in (-1, 0, 1) if di or dj)

SPEED = 260.0          # uu per logical second on land (walking)
JETSKI_SPEED = 600.0   # uu per logical second on water
SENSE_RADIUS = 1200.0  # uu; a bit beyond an organism's NeighbourRange (900)
OBSERVE_DIST = 250.0   # uu; stand this far from the target and watch
RESUME_DIST = 400.0    # uu; once watching, move again only when the target has drifted this far
SWITCH_RATIO = 0.6     # retarget only when another candidate is nearer than this fraction of the current distance
WATER_COST = 3.0       # route cost of a water cell relative to land: a detour up to 3x longer beats a crossing
REPLAN_S = 2.0         # logical seconds after which a route is replanned even if the target's cell has not changed
REPLAN_MOVED_S = 0.5   # ... and the soonest a replan follows a target cell change
STALE_DECISIONS = 2.5  # an organism silent for this many decision intervals has died: forget it
REPLANS_PER_STEP = 2   # refreshes of existing routes per decide message, granted round-robin: the sim waits
                       # at most 200 ms for the bridge's reply, so the field team's work per message must stay
                       # small (a scientist with no route at all always gets a search: once at the start and
                       # once after each watch)
YOUNGEST_N = 5         # Mendel's candidate pool

# name -> (rule, argument); see EmbodiedScientist.candidates. "any" scientists avoid an organism
# another scientist already watches when there is a choice, so the team spreads over the population.
TARGETS = {
    "Vesper":  ("species", "Lumen"),    # Lumen ethologist: the nearest Lumen
    "Bastion": ("species", "Tecton"),   # Tecton engineer-watcher: the nearest Tecton
    "Mendel":  ("youngest", YOUNGEST_N),   # geneticist: the nearest of the newest births
    "Ada":     ("any", None),           # theorist
    "Fisher":  ("any", None),           # statistician
    "Karla":   ("any", None),           # skeptic
    "Archie":  ("any", None),           # archivist
    "Vega":    ("any", None),           # data scientist; report-only presence
}

# Vega is non-voting and report-only by construction (Lab/README.md): her body
# walks and is rendered like the others, but she mints NO witnessed evidence,
# so nothing she sees can enter a stance, a finding, or the registry.
NO_EVIDENCE = {"Vega"}


def _id_key(aid):
    """Sort key for organism ids: the sim's are ints (a newer organism has a larger id) and sort
    numerically; anything else sorts after them as text."""
    return (0, aid, "") if isinstance(aid, (int, float)) else (1, 0, str(aid))


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


class WaterMask:
    """The sim's coarse water grid (hello `water_mask`): membership test and land-first routing."""

    def __init__(self, spec):
        self.cell = float(spec["cell"])
        self.cols = int(spec["cols"])
        self.rows = int(spec["rows"])
        self.x0 = float(spec["x0"])
        self.y0 = float(spec["y0"])
        data = spec["data"]
        if self.cell <= 0 or self.cols < 1 or self.rows < 1 or len(data) != self.rows \
                or any(len(row) != self.cols for row in data):
            raise ValueError("water_mask shape")
        self.water = [[ch == "1" for ch in row] for row in data]
        self.n_water = sum(row.count(True) for row in self.water)
        self.cost = [WATER_COST if w else 1.0 for row in self.water for w in row]   # flat, index j * cols + i

    def cell_of(self, x, y):
        i = int((x - self.x0) // self.cell)
        j = int((y - self.y0) // self.cell)
        return (min(max(i, 0), self.cols - 1), min(max(j, 0), self.rows - 1))

    def center(self, i, j):
        return (self.x0 + (i + 0.5) * self.cell, self.y0 + (j + 0.5) * self.cell)

    def is_water(self, x, y):
        i, j = self.cell_of(x, y)
        return self.water[j][i]

    def route(self, x0, y0, x1, y1):
        """Waypoints (cell centres, then the exact target) from (x0, y0) to (x1, y1): A* over the
        8-neighbour grid with an octile heuristic (admissible: a land cell costs 1), entering a water
        cell costs WATER_COST x a land cell. Deterministic: ties break on (f, g, cell index)."""
        start, goal = self.cell_of(x0, y0), self.cell_of(x1, y1)
        if start == goal:
            return [(x1, y1)]
        cols, rows, cost = self.cols, self.rows, self.cost
        s_idx = start[1] * cols + start[0]
        g_idx = goal[1] * cols + goal[0]
        gi, gj = goal

        def h(idx):
            dx, dy = abs(idx % cols - gi), abs(idx // cols - gj)
            return dx + dy + (SQRT2 - 2.0) * min(dx, dy)

        dist = {s_idx: 0.0}
        prev = {}
        closed = set()
        heap = [(h(s_idx), 0.0, s_idx)]
        while heap:
            _, d, n = heapq.heappop(heap)
            if n == g_idx:
                break
            if n in closed:
                continue
            closed.add(n)
            i, j = n % cols, n // cols
            for di, dj, step in NEIGHBOURS:
                ni, nj = i + di, j + dj
                if ni < 0 or nj < 0 or ni >= cols or nj >= rows:
                    continue
                m = nj * cols + ni
                c = cost[m]
                if di and dj:   # a diagonal brushes both orthogonal neighbours: no free squeeze between water cells
                    c = max(c, cost[j * cols + ni], cost[nj * cols + i])
                nd = d + step * c
                if nd < dist.get(m, math.inf):
                    dist[m] = nd
                    prev[m] = n
                    heapq.heappush(heap, (nd + h(m), nd, m))
        if g_idx not in prev:
            return [(x1, y1)]
        cells = []
        c = g_idx
        while c != s_idx:
            cells.append(c)
            c = prev[c]
        cells.reverse()
        pts = [self.center(c % cols, c // cols) for c in cells[:-1]]   # the goal cell's centre is replaced by the exact target
        pts.append((x1, y1))
        return pts


class EmbodiedScientist:
    def __init__(self, name, run_id, half, half_y=None, water=None):
        self.name = name
        self.half = half              # arena half-length along the valley (X)
        self.half_y = half_y or half  # half-width across it (Y); the sim's hello carries both
        self.water = water            # WaterMask or None (straight lines)
        self.rng = _Rng(_seed_of(run_id, name))
        self.rule, self.rule_arg = TARGETS[name]
        # Start on land when the mask says where that is: a scientist does not spawn mid-river.
        for _ in range(12):
            self.x = self.rng.uniform(-half * 0.5, half * 0.5)
            self.y = self.rng.uniform(-self.half_y * 0.5, self.half_y * 0.5)
            if water is None or not water.is_water(self.x, self.y):
                break
        self.target_id = None
        self.watching = False         # arrived: standing OBSERVE_DIST from the target
        self.path = []                # waypoints toward the target
        self.planned_cell = None      # target cell the path was planned for
        self.planned_t = -math.inf
        self.t = 0.0
        # per-window accumulators
        self.reset_window()

    def reset_window(self):
        self.seen = {"Lumen": set(), "Tecton": set()}
        self.alpha = {"Lumen": [], "Tecton": []}
        self.rewards = {"Lumen": [], "Tecton": []}
        self.actions = {"Lumen": {}, "Tecton": {}}
        self.switches = 0   # target changes this window (reported by flush; a flapping target shows here)

    # ---------------------------------------------------------------- motion

    def on_water(self):
        return self.water is not None and self.water.is_water(self.x, self.y)

    @property
    def mode(self):
        return "jetski" if self.on_water() else "walk"

    def candidates(self, agents, claimed):
        pool = [a for a in agents if a.get("position")]
        if self.rule == "species":
            pool = [a for a in pool if a.get("species") == self.rule_arg]
        elif self.rule == "youngest":
            # Ages arrive once per decision interval at each organism's own phase, as whole intervals, and
            # the population mixes reports from different substeps: age each report forward to now.
            pool = sorted(pool, key=lambda a: (a.get("age", math.inf) + (self.t - a.get("_seen_t", self.t)),
                                               _id_key(a.get("id"))))[:self.rule_arg]
        else:   # "any": an organism nobody else on the team watches, when there is one
            free = [a for a in pool if a.get("id") not in claimed or a.get("id") == self.target_id]
            if free:
                pool = free
        return pool

    def _dist(self, a):
        p = a["position"]
        return math.hypot(p[0] - self.x, p[1] - self.y)

    def pick_target(self, agents, claimed):
        """The nearest candidate; the current target is kept unless another is clearly nearer."""
        pool = self.candidates(agents, claimed)
        if not pool:
            self.target_id = None
            self.watching = False
            return None
        nearest = min(pool, key=lambda a: (self._dist(a), str(a.get("id"))))
        current = next((a for a in pool if a.get("id") == self.target_id), None)
        if current is not None and self._dist(nearest) >= SWITCH_RATIO * self._dist(current):
            return current
        if nearest.get("id") != self.target_id:
            self.watching = False   # the leash belongs to the organism the scientist walked up to
            if self.target_id is not None:
                self.switches += 1
        self.target_id = nearest.get("id")
        return nearest

    def needs_plan(self, tx, ty):
        """2: no route to (tx, ty) at all (must search); 1: a refresh is due (the target moved to another
        cell, at most every REPLAN_MOVED_S, or the route is REPLAN_S old); 0: the route is fine."""
        if not self.path:
            return 2
        if self.water is None:
            return 2   # a straight line costs nothing to refresh
        cell = self.water.cell_of(tx, ty)
        age = self.t - self.planned_t
        if age >= REPLAN_S or (cell != self.planned_cell and age >= REPLAN_MOVED_S):
            return 1
        return 0

    def _plan(self, tx, ty):
        self.path = self.water.route(self.x, self.y, tx, ty) if self.water else [(tx, ty)]
        self.planned_cell = self.water.cell_of(tx, ty) if self.water else None
        self.planned_t = self.t

    def choose(self, agents, t, claimed):
        """Pick this step's target and decide whether a route search is wanted (see EmbodiedField.step).
        Returns the target position or None (no target, or standing and watching)."""
        self.t = t
        target = self.pick_target(agents, claimed)
        if target is None:
            return None
        claimed.add(target.get("id"))
        tx, ty = target["position"][0], target["position"][1]
        d = math.hypot(tx - self.x, ty - self.y)
        if d <= OBSERVE_DIST or (self.watching and d <= RESUME_DIST):
            self.watching = True   # arrived, or the target has not drifted far: stand and watch
            self.path = []
            return None
        self.watching = False
        return (tx, ty)

    def move(self, goal, dt, replan):
        """Advance dt logical seconds toward goal along the planned route (searched now if replan)."""
        if goal is None or dt <= 0:
            return
        tx, ty = goal
        if replan:
            self._plan(tx, ty)
        if not self.path:
            return   # no route yet (the search budget went to someone else this step): wait a step
        time_left = dt
        while time_left > 1e-9 and self.path:
            speed = JETSKI_SPEED if self.on_water() else SPEED
            wx, wy = self.path[0]
            dx, dy = wx - self.x, wy - self.y
            d = math.hypot(dx, dy)
            if d <= speed * time_left:
                self.x, self.y = wx, wy
                time_left -= d / speed
                self.path.pop(0)
            else:
                step = speed * time_left
                self.x += dx / d * step
                self.y += dy / d * step
                time_left = 0.0
            if math.hypot(tx - self.x, ty - self.y) <= OBSERVE_DIST:
                self.watching = True
                self.path = []
                break
        self.x = max(-self.half, min(self.half, self.x))
        self.y = max(-self.half_y, min(self.half_y, self.y))

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
    """All eight bodies (Vega mints no evidence) plus per-window evidence minting."""

    def __init__(self, run_id, world_half_size, world_half_size_y=None, water_mask=None,
                 decision_interval=None):
        self.run_id = run_id
        self.water = None
        if water_mask:
            try:
                self.water = WaterMask(water_mask)
                print(f"[embody] water mask {self.water.cols}x{self.water.rows} cells of "
                      f"{self.water.cell:.0f} uu, {self.water.n_water} water; routing on land")
            except (KeyError, TypeError, ValueError) as e:
                print(f"[embody] water_mask ignored ({e!r}); routing straight lines")
        self.team = [EmbodiedScientist(n, run_id, float(world_half_size or 4500.0),
                                       float(world_half_size_y) if world_half_size_y else None,
                                       self.water)
                     for n in TARGETS]
        self.last_t = None
        self.plan_offset = 0
        # Latest report of every organism seen (id -> agent dict) and when: see the module docstring.
        self.known = {}
        self.known_t = {}
        self.stale_s = STALE_DECISIONS * float(decision_interval or 1.0)

    def step(self, t, agents):
        dt = 0.0 if self.last_t is None else max(0.0, t - self.last_t)
        self.last_t = t
        for a in agents:
            aid = a.get("id")
            if aid is not None and a.get("position"):
                self.known[aid] = dict(a, _seen_t=t)   # when this report was made, for ageing it forward
                self.known_t[aid] = t
        for aid in [k for k, seen in self.known_t.items() if t - seen > self.stale_s]:
            del self.known[aid]
            del self.known_t[aid]
        population = list(self.known.values())
        # Organisms the team already watches are off limits to the others (when there is a choice), so a
        # colleague's target is never taken from them and nobody is sent off after the next free one.
        claimed = {s.target_id for s in self.team if s.target_id is not None}
        goals = [s.choose(population, t, claimed) for s in self.team]
        # Route searches are the only costly work here. A scientist with no route always searches
        # (once at the start, once after each watch); refreshes of existing routes are capped at
        # REPLANS_PER_STEP per message, granted from a rotating member so nobody starves.
        n = len(self.team)
        grants = [False] * n
        if dt > 0:
            budget = REPLANS_PER_STEP
            for k in range(n):
                i = (self.plan_offset + k) % n
                if goals[i] is None:
                    continue
                need = self.team[i].needs_plan(*goals[i])
                if need == 2:
                    grants[i] = True
                elif need == 1 and budget > 0:
                    grants[i] = True
                    budget -= 1
            self.plan_offset = (self.plan_offset + 1) % n
        for s, goal, grant in zip(self.team, goals, grants):
            s.move(goal, dt, grant)
            s.witness(agents)

    def team_positions(self):
        """[{name, x, y, mode}] for the sim's avatar layer (docs/POLICY_API.md); mode is informational."""
        return [{"name": s.name, "x": round(s.x, 1), "y": round(s.y, 1), "mode": s.mode}
                for s in self.team]

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
                if n == 0 or s.name in NO_EVIDENCE:
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
            lines.append(f"{s.name}@({s.x:.0f},{s.y:.0f}) saw {n_tot}, {s.switches} switches")
            s.reset_window()
        con.commit()
        return "field: " + ", ".join(lines)
