# Symbiotic World — mechanism definitions

Terminology is deliberately exact so the demo never over-claims. Everything
here is what the code does, not what the concept art suggests.

## 1. Lifetime learning (within one individual)

**Tabular contextual bandit.** Each agent holds `Q[c][a]` for context
`c ∈ {LOW, MID, HIGH}` (energy thirds) and action
`a ∈ {forage, explore, follow, avoid, signal, rest, modify}`.

Every `DecisionInterval` (1 logical s):

1. The reward for the *previous* action is credited:
   `r = wE · ΔEnergy / RewardScale + wN · novelty` (defaults: pure energy
   change; novelty weight 0 and documented as a bias if ever used).
2. `Q[c][a] += α · (r − Q[c][a])` with `α` from the agent's genome
   (forced to 0 in mode A).
3. A percept is built; a **feasibility mask** removes impossible actions
   (no forage without a stocked patch in range, no follow without a
   neighbour or fresh signal, no avoid unless crowded, signal only for
   Lumen with a known resource and a same-species listener, modify
   disabled until Trace X/Y exist).
4. ε-greedy over the feasible set, ε from the genome.

There is no bootstrapping from the next state (γ = 0), so this is **not
Q-learning**, not deep RL, and should not be called either. `Q` is
initialised `Uniform(0, QInitMax)` per cell; in mode A those random values
*are* the policy for life.

What the inspector shows: the initial `Q` table next to the current one, per
bin, with the current bin bracketed. Inherited values sit in a separate block
and do not change during a lifetime.

## 2. Evolution (across generations)

**Asexual reproduction with Gaussian mutation of the inherited genome**
`G = {α, ε, social, e}` (e added 2026-09-05 evening once Trace X/Y existed):

- Reproduce when `age ≥ MinReproAge` and `energy ≥ ReproThreshold`, paying
  `ReproCost` to the child. Population hard-capped at `MaxPopulation`.
- `G_child = clamp(G_parent + N(0, σ))` independently per parameter
  (σ = `MutationSigma`, default 0.03).
- Learned `Q` is **not** inherited (spec's λ-inheritance is P3).
- Death by starvation (`energy ≤ 0`) or age (`MaxAge`).

`e` (environmental-effect strength, spec §6) is inherited in `[0.05, 1.0]`,
default 0.5, mutated like the others and fixed for a lifetime. It scales a
`modify` decision's deposit (`base · e / 0.5`) and its energy cost
(`ModifyBurn · e / 0.5`), so evolution can strengthen or weaken how much a
lineage reshapes the shared environment, at a price. Dropped from the spec's
genome: `m` (memory persistence), redundant with α for a constant-step-size
update.

## 3. Modes

| Mode | α | genome | births | starvation | purpose |
|---|---|---|---|---|---|
| A | 0 | founders vary, children mutate | energy-gated | yes | learning-off baseline |
| B | genome | identical, copied exactly | energy-gated | yes | within-lifetime adaptation only |
| C | genome | inherited + mutated | energy-gated | yes | the full story |
| N | genome | inherited + mutated | uniformly random parent when below target | **no** | neutral drift control |

N exists because a shift in mean α under C is only evidence of selection if
it is distinguishable from what drift alone produces. `analyze_run.py`
performs that comparison across seeds.

## 4. Environment

- Square arena, `WorldHalfSize` half-width. Movement is kinematic and
  clamped to the arena.
- Resource patches: `Stock` with logistic regrowth
  `dS/dt = Regen · (1 − S/K)`; type A feeds Lumen, type B feeds Tecton.
- Drought: multiplies regen by `DroughtRegenMultiplier` and effective
  capacity by `DroughtCapacityMultiplier`; stock above the new capacity
  decays toward it.
- Signals: a signalling Lumen broadcasts its nearest known resource location
  to same-species neighbours within `NeighbourRange · (0.5 + social)`;
  receivers accept with probability `social`.
- **Trace X / Trace Y** (implemented 2026-09-05 evening, spec §7). Two scalar
  grids over the arena (`TraceCells` = 30 per side, cells of 300 uu), each
  decaying on the logical clock as `v *= 0.5^(dt / half-life)`; Trace X
  half-life 20 s, Trace Y 120 s; values clamped to `[0, TraceMax]`.
  - *Lumen `modify`* deposits `TraceXDeposit · e/0.5` at its cell (3×3 falloff; base 0.4 so the centre cell never clamps at TraceMax before e = 1).
    Feasible only when a stocked resource of its kind is in sense range, so
    a marker means "food near here". *Lumen `follow`*, when it has no fresh
    signal and no neighbour, climbs the local Trace X gradient if the local
    value is above `TraceXFollowMin`. Information becomes navigable.
  - *Tecton `modify`* deposits `TraceYDeposit · e/0.5` at its cell, costs
    `ModifyBurn` energy per second, feasible on land with > 25 % energy.
    Patch regrowth in a cell is multiplied by `1 + TraceYRegenGain · TraceY`,
    on top of the drought multiplier, so engineered ground keeps producing
    when the valley dries. No species is told to cooperate; whether Tecton
    soil work helps Lumen is an outcome.
  - *Documented bias:* the spec's `wI · UsefulInteraction` reward term is
    `WeightInteraction` (default 0.10, 0 disables). A deposit that lands
    where it is useful (Lumen: stocked resource in range; Tecton: a patch in
    the cell below half stock) adds `wI · (1 − field_before / TraceMax)` to
    that decision's reward, so repeating a deposit on an already-marked cell
    earns nothing (diminishing usefulness). Without
    it a γ = 0 bandit can never credit `modify`, whose benefit arrives later.
    Any claim about modify being learned must state this weight.
  - Logged: `trace_X_mean`, `trace_Y_mean` in population.csv; `trace_x`,
    `trace_y` (local) in agents.csv.
  - Visual: a translucent ground overlay (R = Trace X cyan, G = Trace Y
    amber) draped over the terrain, refreshed at 10 Hz from the fields; the
    Lumen trail ribbons are per-agent movement history, not the field.

## 5. Time

Reproducibility note: adding `e` (2026-09-05 evening) changed the number of
seeded draws per birth (4 Gaussians instead of 3), so runs recorded earlier in
PROGRESS.md do not reproduce number-for-number with the same seed. Determinism
within a build is unchanged (same seed + mode => byte-identical CSVs).


One seeded `FRandomStream` per run. The world advances in fixed
`LogicalSubstep` (0.1 s) steps from the manager's Tick; 1×/10×/50× (and
`-SWSpeed`) only change how many substeps run per rendered frame, never the
step size. Same seed + same mode ⇒ same run (up to floating-point order,
which is fixed because iteration order is deterministic).

## 6. Known balance state (2026-09-05)

Original default `PatchRegenPerSec = 1.6`: mode B/C, 300–600 s, Lumen
overshoot 40 → 53 then crash to 2–6 as Resource A is stripped (47 of 66 Lumen
deaths by starvation in B seed 42); Tecton stable at 13–18. Total Resource A
regen at empty stock was 22 × 1.6 = 35 energy/s against ~40 Lumen burning
1.5–2.1 energy/s each.

First sweep (`Tools/sweep.py`, mode C, seed 1, 600 s):

| override | Lumen min→final | Lumen starved | Tecton min→final | max gen |
|---|---|---|---|---|
| baseline (regen 1.6) | 2 → 2 | 40 | 6 → 6 | 8 |
| regen 4 | 23 → 34 | 56 | 12 → 16 | 11 |
| **regen 6 (new default)** | 40 → 63 | 78 | 12 → 25 | 12 |
| regen 6 + ReproThreshold 85 + MinReproAge 30 | 40 → 51 | 59 | 12 → 42 | 14 |
| regen 8 + 30 A patches | 41 → 109 | 109 | 12 → 74 | 17 |

`PatchRegenPerSec` default is now **6.0** (one seed, one duration; treat as a
starting point, not a result). Open questions for the research day: does the
stable band hold across seeds 1–5 and 1800 s; is turnover (births ≈ deaths ≈
300 per 600 s) fast enough to see selection; does drought at 0.3× regen from
this baseline cause a visible but survivable dip. Levers, all via `-SWSet`:
`Settings.PatchRegenPerSec`, `Settings.ResourcePatchesA/B`,
`Settings.PatchCapacity`, `Lumen.ReproThreshold`, `Lumen.MinReproAge`,
`Lumen.ForageRate`, `Settings.DroughtRegenMultiplier`.

Founders now start with staggered ages (`FounderAgeSpread = 0.6` × MaxAge),
which removed synchronised age-death cohorts and fixed mode N (it previously
produced zero births because the whole founding cohort died in one step).

## 7. Scientific guardrails (spec §13)

- Never label survival or recovery as intelligence.
- Never hard-code cooperation and then claim it emerged. There is no
  Lumen–Tecton coupling in the code today beyond shared space.
- Every shown run states its seed and mode (the HUD does; so do the logs).
- Always show a learning-off control (mode A) and, for evolution claims, the
  drift control (mode N).
- Inherited parameters and learned policy are visually separate in the
  inspector.
