"""Unit checks for the embodied field team's targeting and land-first routing (no sim needed).

    python -m pytest Lab/tests/test_embodiment.py -q
    python Lab/tests/test_embodiment.py
"""
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

from Lab.embodiment import (EmbodiedField, WaterMask, OBSERVE_DIST, RESUME_DIST,  # noqa: E402
                            SPEED, JETSKI_SPEED, WATER_COST, REPLANS_PER_STEP)
import time  # noqa: E402

CELL = 200.0


def mask(cols, rows, water_cells):
    data = ["".join("1" if (i, j) in water_cells else "0" for i in range(cols)) for j in range(rows)]
    return {"cell": CELL, "cols": cols, "rows": rows, "x0": -cols * CELL / 2, "y0": -rows * CELL / 2, "data": data}


def agent(id_, x, y, species="Lumen", age=10.0):
    return {"id": id_, "species": species, "age": age, "position": [x, y]}


def field(spec, run_id="run-test"):
    return EmbodiedField(run_id, spec["cols"] * CELL / 2 if spec else 4500.0,
                         spec["rows"] * CELL / 2 if spec else None, spec)


def test_route_prefers_a_short_land_detour():
    # A 3-cell-thick tributary stub, columns 9-11, rows 0..6 of 10. Straight across row 5 costs
    # 3 water cells (3 x WATER_COST) + 3 land = 12; round the tip through row 7 costs about 8.8.
    m = WaterMask(mask(20, 10, {(i, j) for i in (9, 10, 11) for j in range(7)}))
    sx, sy = m.center(7, 5)
    tx, ty = m.center(13, 5)
    pts = m.route(sx, sy, tx, ty)
    assert pts[-1] == (tx, ty)
    assert not any(m.is_water(x, y) for x, y in pts), pts


def test_diagonal_water_line_is_not_squeezed_through():
    # A one-cell-wide water line on the anti-diagonal (i + j == 11). Diagonal steps between two of its cells
    # used to cost land; now any crossing pays for water, so the route has a water waypoint.
    m = WaterMask(mask(12, 12, {(i, 11 - i) for i in range(12)}))
    pts = m.route(*m.center(2, 2), *m.center(9, 9))
    assert any(m.is_water(x, y) for x, y in pts), pts


def test_route_crosses_when_the_river_spans_the_arena():
    m = WaterMask(mask(20, 10, {(i, j) for i in (9, 10) for j in range(10)}))
    sx, sy = m.center(5, 5)
    tx, ty = m.center(15, 5)
    pts = m.route(sx, sy, tx, ty)
    assert any(m.is_water(x, y) for x, y in pts)
    assert len(pts) <= 11   # straight across, not a wander


def test_scientist_rides_across_and_stops_short_of_the_target():
    spec = mask(20, 10, {(i, j) for i in (9, 10) for j in range(10)})
    f = field(spec)
    vesper = f.team[0]
    vesper.x, vesper.y = f.water.center(5, 5)
    target = agent("L1", *f.water.center(15, 5))
    modes = set()
    t = 0.0
    for _ in range(60):
        f.step(t, [target])
        modes.add(vesper.mode)
        t += 0.5
    assert "jetski" in modes and "walk" in modes
    d = math.hypot(target["position"][0] - vesper.x, target["position"][1] - vesper.y)
    assert d <= OBSERVE_DIST + 1e-6, d
    x, y = vesper.x, vesper.y
    f.step(t, [target])
    assert (x, y) == (vesper.x, vesper.y)   # arrived: stands and watches


def test_rules_and_claims_spread_the_team():
    f = field(None)
    for s in f.team:
        s.x, s.y = 0.0, 0.0
    # Ages 1-5 are the five youngest (Mendel's pool); everyone else is 10.
    agents = [agent("L1", 300, 0, "Lumen"), agent("T1", 200, 0, "Tecton"),
              agent("L2", 1000, 0, "Lumen", age=1.0), agent("L3", -400, 0, "Lumen"),
              agent("T2", 0, 500, "Tecton", age=2.0), agent("L4", 0, -600, "Lumen", age=3.0),
              agent("L5", 700, 700, "Lumen", age=4.0), agent("L6", -800, -800, "Lumen", age=5.0)]
    f.step(0.0, agents)
    by = {s.name: s.target_id for s in f.team}
    assert by["Vesper"] == "L1"      # nearest Lumen, not the nearer Tecton
    assert by["Bastion"] == "T1"     # nearest Tecton
    assert by["Mendel"] == "T2"      # nearest of the five youngest (L1 at 300 is not among them)
    others = [by[n] for n in ("Ada", "Fisher", "Karla", "Archie", "Vega")]
    assert len(set(others)) == 5 and not set(others) & {"L1", "T1", "T2"}, by


def test_a_colleague_keeps_their_organism():
    # Two "any" scientists, two organisms. Once assigned, moving Fisher's organism next to Ada must
    # not let Ada (earlier in team order) take it and send Fisher off to the other one.
    # Vesper, Bastion and Mendel (earlier in team order, own rules) get their own organisms near the origin.
    f = field(None)
    ada = next(s for s in f.team if s.name == "Ada")
    fisher = next(s for s in f.team if s.name == "Fisher")
    for s in f.team:
        s.x, s.y = 0.0, 0.0
    ada.x, fisher.x = -1000.0, 1000.0
    others = [agent("V", 0, 100, "Lumen"), agent("T", 0, -100, "Tecton"), agent("Y", 100, 0, "Lumen", age=1.0)]
    f.step(0.0, others + [agent("A", -1200, 0), agent("B", 1200, 0)])
    assert ada.target_id == "A" and fisher.target_id == "B", (ada.target_id, fisher.target_id)
    f.step(0.5, others + [agent("A", -1200, 0), agent("B", -900, 0)])   # B now nearest to Ada as well
    assert ada.target_id == "A" and fisher.target_id == "B", (ada.target_id, fisher.target_id)


def test_target_is_kept_until_another_is_clearly_nearer():
    f = field(None)
    vesper = f.team[0]
    vesper.x, vesper.y = 0.0, 0.0
    f.step(0.0, [agent("L1", 1000, 0)])
    assert vesper.target_id == "L1"
    f.step(0.5, [agent("L1", 1000, 0), agent("L2", 0, 900)])   # 10 % nearer: keep L1
    assert vesper.target_id == "L1"
    f.step(1.0, [agent("L1", 1000, 0), agent("L2", 0, 300)])   # clearly nearer: switch
    assert vesper.target_id == "L2" and vesper.switches == 1


def test_watching_scientist_ignores_a_small_drift():
    f = field(None)
    vesper = f.team[0]
    vesper.x, vesper.y = 0.0, 0.0
    f.step(0.0, [agent("L1", 200, 0)])
    f.step(0.5, [agent("L1", 200, 0)])
    assert vesper.watching and (vesper.x, vesper.y) == (0.0, 0.0)
    f.step(1.0, [agent("L1", RESUME_DIST - 10, 0)])   # drifted, but within the leash: stay
    assert (vesper.x, vesper.y) == (0.0, 0.0)
    f.step(1.5, [agent("L1", RESUME_DIST + 10, 0)])   # past the leash: walk again
    assert vesper.x > 0.0 and not vesper.watching


def test_no_mask_walks_the_straight_line_at_walking_speed():
    f = field(None)
    vesper = f.team[0]
    vesper.x, vesper.y = 0.0, 0.0
    f.step(0.0, [agent("L1", 5000, 0)])
    f.step(1.0, [agent("L1", 5000, 0)])
    assert abs(vesper.x - SPEED) < 1e-6 and abs(vesper.y) < 1e-6
    assert vesper.mode == "walk"


def test_jetski_is_faster_on_water():
    spec = mask(20, 10, {(i, j) for i in range(20) for j in range(10)})   # all water
    f = field(spec)
    vesper = f.team[0]
    vesper.x, vesper.y = f.water.center(2, 5)   # on a cell centre: the row of centres ahead is a straight line
    x0, y0 = vesper.x, vesper.y
    target = agent("L1", *f.water.center(18, 5))
    f.step(0.0, [target])
    f.step(1.0, [target])
    assert abs(vesper.x - x0 - JETSKI_SPEED) < 1e-6 and abs(vesper.y - y0) < 1e-6
    assert vesper.mode == "jetski"


def test_full_size_mask_search_is_cheap_and_bounded_per_step():
    # The sim's real mask is 80 x 55 cells; a river band along X with two gaps-free banks. One search
    # must be milliseconds, and a step with 8 scientists must run at most REPLANS_PER_STEP searches.
    water = {(i, j) for i in range(80) for j in range(24, 33)}
    spec = mask(80, 55, water)
    m = WaterMask(spec)
    t0 = time.perf_counter()
    for k in range(20):
        m.route(*m.center(3 + k, 5), *m.center(70 - k, 50))
    per = (time.perf_counter() - t0) / 20
    assert per < 0.05, per   # generous: the sim's reply budget is 200 ms for everything
    f = field(spec)
    calls = []
    real = f.water.route
    f.water.route = lambda *a: (calls.append(1), real(*a))[1]
    for s in f.team:
        s.x, s.y = m.center(5, 5)
    # One Tecton so Bastion has a target too; the rest Lumens.
    agents = [agent(f"L{k}", *m.center(70, 45 + k), "Tecton" if k == 7 else "Lumen") for k in range(8)]
    f.step(0.0, agents)
    f.step(0.5, agents)   # first routes: one search each, by design unbounded
    assert len(calls) == 8 and all(s.path for s in f.team), len(calls)
    calls.clear()
    moved = [agent(f"L{k}", *m.center(60, 40 + k), "Tecton" if k == 7 else "Lumen") for k in range(8)]   # every target changes cell
    f.step(3.0, moved)   # 2.5 s later every route is stale: refreshes are rationed
    assert 0 < len(calls) <= REPLANS_PER_STEP, len(calls)
    for k in range(1, 5):
        f.step(3.0 + 0.5 * k, moved)
    assert all(s.planned_t >= 3.0 for s in f.team)   # everyone got a refresh within four more messages
    assert len(calls) <= 5 * REPLANS_PER_STEP


def test_due_subset_stream_keeps_targets_stable():
    # The sim sends each decide message with only the organisms due that substep (1 in 10 here). Once the
    # whole population has been seen, the targets must not flip from message to message.
    f = field(None)
    organisms = [agent(f"O{k}", 3000 * math.cos(2 * math.pi * k / 20), 3000 * math.sin(2 * math.pi * k / 20),
                       "Tecton" if k % 4 == 0 else "Lumen", age=1.0 + k) for k in range(20)]
    for k in range(10):   # one decision interval: everyone reports once
        f.step(0.1 * k, [o for i, o in enumerate(organisms) if i % 10 == k % 10])
    before = [s.target_id for s in f.team]
    assert all(before), before
    changes = 0
    for k in range(10, 40):
        f.step(0.1 * k, [o for i, o in enumerate(organisms) if i % 10 == k % 10])
        now = [s.target_id for s in f.team]
        changes += sum(x != y for x, y in zip(before, now))
        before = now
    assert changes <= 1, changes


def test_youngest_ranking_ages_remembered_reports_forward():
    # Ages arrive once per decision interval at each organism's birth phase, as whole intervals. Child 19
    # (born t=0.6) is younger than child 18 (born t=0.2) and must stay in Mendel's five-youngest pool
    # although for part of every interval both last reported the same age.
    f = field(None)
    mendel = next(s for s in f.team if s.name == "Mendel")
    mendel.x, mendel.y = 0.0, 0.0
    births = {18: (0.2, (300.0, 0.0)), 19: (0.6, (320.0, 0.0))}
    births.update({20 + k: (0.7 + 0.1 * k, (0.0, 3000.0 + 300.0 * k)) for k in range(4)})
    targets = set()
    for step in range(101):
        t = round(0.1 * step, 1)
        msg = []
        for aid, (born, pos) in births.items():
            since = round(t - born, 1)
            if since >= 0 and abs(since - round(since)) < 1e-6:   # due: whole intervals since birth
                msg.append({"id": aid, "species": "Lumen", "age": float(round(since)), "position": list(pos)})
        f.step(t, msg)
        if t >= 2.0:
            targets.add(mendel.target_id)
    assert targets == {19}, sorted(targets)


def test_an_organism_that_stops_deciding_is_forgotten():
    f = field(None)
    vesper = f.team[0]
    vesper.x, vesper.y = 0.0, 0.0
    f.step(0.0, [agent("L1", 1000, 0), agent("L2", 3000, 0)])
    f.step(1.0, [agent("L2", 3000, 0)])   # L1 not due this message: still known
    assert vesper.target_id == "L1"
    f.step(3.0, [agent("L2", 3000, 0)])   # 3 s of silence > 2.5 decision intervals: L1 died
    assert vesper.target_id == "L2"


def test_new_target_does_not_inherit_the_leash():
    f = field(None)
    vesper = f.team[0]
    vesper.x, vesper.y = 0.0, 0.0
    f.step(0.0, [agent("L1", 200, 0)])
    f.step(0.5, [agent("L1", 200, 0)])
    assert vesper.watching
    for t in (1.0, 1.5, 2.0, 2.5, 3.0):      # L1 not due, still remembered (and nearer): keep watching it
        f.step(t, [agent("L2", 390, 0)])
    assert vesper.target_id == "L1" and (vesper.x, vesper.y) == (0.0, 0.0)
    f.step(3.5, [agent("L2", 390, 0)])       # L1 forgotten; L2 is inside RESUME_DIST but was never approached
    assert vesper.target_id == "L2" and vesper.x > 0.0 and not vesper.watching   # walks up to it


def test_same_run_id_and_stream_reproduce_positions():
    spec = mask(20, 10, {(i, j) for i in (9, 10) for j in range(10)})
    stream = [[agent("L1", 1500 - 30 * k, 200 * math.sin(k / 5.0)), agent("T1", -1200, 400, "Tecton")]
              for k in range(40)]
    outs = []
    for _ in range(2):
        f = field(spec, "run-42")
        for k, agents in enumerate(stream):
            f.step(0.5 * k, agents)
        outs.append(f.team_positions())
    assert outs[0] == outs[1]
    assert all(p["mode"] in ("walk", "jetski") for p in outs[0])


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
            print("ok", name)
