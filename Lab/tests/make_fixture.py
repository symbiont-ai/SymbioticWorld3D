#!/usr/bin/env python3
"""FIXTURE DATA GENERATOR — for testing the Lab pipeline only.

Writes synthetic run directories with the exact CSV headers SWLogger.cpp
produces, so evidence ingestion and metric computation can be tested on a
machine without the UE sim. The numbers are drawn from simple seeded
distributions with a built-in "ground truth" (mode C alpha drifts upward,
mode N does not; Q tables drift when alpha > 0) — they are NOT sim output
and must never be ingested into a real lab database.

  python3 Lab/tests/make_fixture.py <out_root>
"""
import sys
from pathlib import Path

import numpy as np
import pandas as pd

ACTIONS = ["forage", "explore", "follow", "avoid", "signal", "rest", "modify"]
BINS = ["low", "mid", "high"]
QCOLS = [f"Q_{b}_{a}" for b in BINS for a in ACTIONS]


def make_run(root, mode, seed, duration=600.0, alpha_trend=0.0, tecton_collapse=True):
    rng = np.random.RandomState(seed * 7919 + hash(mode) % 1000)
    run_id = f"FIXTURE_{mode}_s{seed}"
    d = Path(root) / run_id
    d.mkdir(parents=True, exist_ok=True)
    times = np.arange(0.0, duration + 1, 5.0)

    # population.csv
    rows = []
    births = deaths = 0
    for t in times:
        frac = t / duration
        for sp, n0 in (("Lumen", 40), ("Tecton", 12)):
            if sp == "Tecton" and tecton_collapse:
                # built-in ground truth: Tecton is endangered (12 -> ~3)
                n = max(2, int(n0 - 9 * frac + rng.randint(-1, 2)))
            else:
                n = max(2, int(n0 + 20 * frac + rng.randint(-3, 4)))
            mean_alpha = 0.12 + (alpha_trend * frac if sp == "Lumen" else 0) \
                + rng.normal(0, 0.003)
            rows.append(dict(
                run_id=run_id, seed=seed, mode=mode, sim_time=round(t, 2), species=sp,
                n=n, mean_alpha=round(mean_alpha, 4), sd_alpha=0.04,
                mean_epsilon=round(0.2 + rng.normal(0, 0.005), 4), sd_epsilon=0.05,
                mean_social=0.5, sd_social=0.1,
                mean_env_effect=round(0.5 + rng.normal(0, 0.01), 4), sd_env_effect=0.1,
                mean_generation=round(1 + 8 * frac, 2), max_generation=int(1 + 11 * frac),
                births=int(300 * frac), deaths=int(280 * frac),
                resource_A=round(1200 + rng.normal(0, 60), 1),
                resource_B=round(700 + rng.normal(0, 40), 1),
                drought_state=0, trace_X_mean=round(0.02 + 0.01 * frac, 4),
                trace_Y_mean=round(0.05 + 0.04 * frac, 4)))
        births, deaths = int(300 * frac), int(280 * frac)
    pd.DataFrame(rows).to_csv(d / "population.csv", index=False)

    # agents.csv — 30 agents, 10 samples each; Q drifts when learning is on
    arows = []
    learn = 0.0 if mode == "A_learning_off" else 1.0
    for aid in range(1, 31):
        q0 = rng.uniform(0, 0.05, len(QCOLS))
        alpha = float(np.clip(0.12 + rng.normal(0, 0.04), 0, 0.5))
        for k in range(10):
            t = k * duration / 10 + rng.uniform(0, 3)
            q = q0 + learn * alpha * k * rng.uniform(0.01, 0.06, len(QCOLS))
            r = dict(run_id=run_id, seed=seed, mode=mode, sim_time=round(t, 2),
                     generation=1 + k // 4, drought_state=0, agent_id=aid,
                     parent_id=max(0, aid - 10), species="Lumen" if aid <= 22 else "Tecton",
                     age=round(10 + k * 12.0, 2), energy=round(rng.uniform(20, 95), 3),
                     alpha=round(alpha, 4), epsilon=0.2, social=0.5, env_effect=0.5,
                     energy_bin=int(rng.randint(0, 3)),
                     current_action=ACTIONS[rng.randint(0, 7)],
                     explored=int(rng.rand() < 0.2), reward=round(rng.normal(0, 0.2), 4),
                     decisions=int(5 + k * 30), trace_x=0.02, trace_y=0.05)
            for c, v in zip(QCOLS, q):
                r[c] = round(float(v), 4)
            r.update(births=births, deaths=deaths, pop_lumen=50, pop_tecton=15,
                     resource_A=1200.0, resource_B=700.0)
            arows.append(r)
    pd.DataFrame(arows).to_csv(d / "agents.csv", index=False)

    # births.csv — heritable parameters: child = parent + N(0, 0.03)
    brows = []
    for i in range(1, 200):
        pa = float(np.clip(0.12 + alpha_trend * (i / 200) + rng.normal(0, 0.05), 0, 0.5))
        pe = float(np.clip(0.2 + rng.normal(0, 0.05), 0.01, 0.5))
        ps, pv = 0.5, 0.5
        brows.append(dict(
            run_id=run_id, sim_time=round(i * duration / 200, 2), parent_id=i,
            child_id=1000 + i, species="Lumen" if i % 4 else "Tecton",
            child_generation=1 + i // 25,
            child_alpha=round(float(np.clip(pa + rng.normal(0, 0.03), 0, 0.5)), 4),
            child_epsilon=round(float(np.clip(pe + rng.normal(0, 0.03), 0.01, 0.5)), 4),
            child_social=round(float(np.clip(ps + rng.normal(0, 0.03), 0, 1)), 4),
            child_env_effect=round(float(np.clip(pv + rng.normal(0, 0.03), 0.05, 1)), 4),
            parent_alpha=round(pa, 4), parent_epsilon=round(pe, 4),
            parent_social=ps, parent_env_effect=pv,
            parent_age=round(rng.uniform(25, 120), 2),
            parent_energy=round(rng.uniform(78, 100), 2)))
    pd.DataFrame(brows).to_csv(d / "births.csv", index=False)

    pd.DataFrame([dict(run_id=run_id, sim_time=duration, agent_id=5, species="Lumen",
                       generation=2, age=140.0, energy=0.0, cause="starvation",
                       alpha=0.12, epsilon=0.2, social=0.5, env_effect=0.5,
                       decisions=200)]).to_csv(d / "deaths.csv", index=False)
    return d


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "Lab/tests/fixtures"
    made = []
    for seed in (1, 2, 3):
        made.append(make_run(root, "C_learning_evolution", seed, alpha_trend=0.05))
        made.append(make_run(root, "N_neutral_control", seed, alpha_trend=0.0))
    for m in made:
        print(m)


if __name__ == "__main__":
    main()
