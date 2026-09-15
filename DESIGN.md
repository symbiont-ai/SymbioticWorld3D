# Symbiotic World — mechanism definitions

Terminology is deliberately exact so the demo never over-claims. Everything
here is what the code does, not what the concept art suggests.

## 1. Lifetime learning (within one individual)

**Tabular contextual bandit.** Each agent holds `Q[c][a]` for context
`c ∈ {LOW, MID, HIGH}` (energy thirds) and action
`a ∈ {forage, explore, follow, avoid, signal, rest, modify}`.

Every `DecisionInterval` (1 logical s):

1. The reward for the *previous* action is credited:
   `r = wE · ΔEnergy / RewardScale + wN · novelty` (defaults: `wE` 1, `wN`
   0.2 since 2026-09-11, a documented bias: novelty = first visit of a cell of
   the 24 x 24 novelty grid per decision interval. The hack build used 0;
   §6b records why 0.2 was chosen and that 0.3 destabilised Lumen).
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

- Rectangular arena centred at the origin, `WorldHalfSize` half-length along
  the valley (X) and `WorldHalfSizeY` half-width across it (0 = square):
  8000 x 5500 uu since 2026-09-11 (160 x 110 m; the hack build was a 4500
  square, less than half the area, and its organisms read as a crowd on one bank).
  Movement is kinematic and clamped to the arena. Trace grids are
  `TraceCells` x `TraceCells` over the rectangle (rectangular cells).
- River: the main channel is a sinusoid in X (`Look.RiverAmp/Wavelength`),
  carved as a Gaussian of half-width `RiverWidth` (1000 uu) and depth
  `RiverDepth` (400 uu, so the water is about 36 m wide and 4 m deep), plus
  `RiverBranches` (2) tributaries: Bezier polylines from the valley sides to
  confluences on the main channel, carved at `RiverBranchWidth/Depth` x the
  main values (`SWProc::RiverBranches`, no random draws). Organisms never sink
  with the bed (`SWProc::GroundZ` floors them 8 uu under the surface): the
  depth only matters to the predator and to the eye. `bOnLand` is
  `TerrainHeight > WaterLevel`, so an organism in a tributary senses "not on
  land" as well; only the main channel is hunted (the predator patrols
  `RiverCenterY`), so the tributaries are wet but safe.
- Patches are placed by seeded draws that reject any channel (1.9x its width)
  and prefer at least `PatchMinSpacing` (1100 uu) from every patch already
  placed (best of 12 draws), so herds spread over the floor instead of
  stacking; counts `ResourcePatchesA/B` 34 / 10 for the larger arena (B is the
  Tecton ceiling, §6).
- Resource patches: `Stock` with logistic regrowth
  `dS/dt = Regen · (1 − S/K)`; type A feeds Lumen, type B feeds Tecton.
- Drought: multiplies regen by `DroughtRegenMultiplier` and effective
  capacity by `DroughtCapacityMultiplier`; stock above the new capacity
  decays toward it.
- **Leviathan** (river predation, contributed 2026-09-06, off by default). A
  perturbation of the environment, not a species: no genome, no learner, no
  `ESWSpecies` entry, no new action, so the external policy protocol is
  unchanged. `Settings.bLeviathan` spawns `LeviathanCount` animals that patrol
  the river channel on the fixed substep and kill any
  organism within `LeviathanStrikeRadius` (horizontal) whose position is in the
  water (`FSWPercept::bOnLand` false, widened by `LeviathanWaterMargin`), at
  most one kill per `LeviathanStrikeCooldown` s per animal. The cooldown is
  the death-rate dial: encounters are far more frequent than it, so one animal
  takes at most 60/cooldown organisms per logical minute (default 5 s ->
  12/min; raise it if the population crashes). `LeviathanTarget`
  (`ESWLeviathanTarget`: `Both` default, `Lumen`, `Tecton`) restricts the
  victims to one species; it is a filter on the predator, not an `ESWSpecies`
  entry, and `Both` is the default because a single-species predator is a
  species handicap rather than a shared pressure. `bLeviathanPauseInDrought`
  (default true) stops strikes while the drought is active so the two
  perturbations never overlap; the animal keeps patrolling. Kills are logged
  in `deaths.csv` with cause `predation` and shown in a sixth, red HUD stat
  card (`LEVIATHAN`, "N taken" with the split per prey species underneath,
  "paused: drought" during a paused drought; the drought
  banner then reads "(predation paused)"). Movement and surfacing
  (`LeviathanSubmersion`, `LeviathanBreachRise`, `LeviathanSurfaceInterval`,
  `LeviathanSurfaceDuration`) are visual and use their own `LookSeed` stream.
  Movement (2026-09-11): the animal hunts, chasing an organism that is in the
  water within `LeviathanSenseRadius` (2400 uu) at `LeviathanChaseSpeed`
  (1500 uu/s) and steering across the channel toward it, up to 1.2 x
  `RiverWidth` off the centreline (the water reaches ~1.8 x each side and the jaw
  needs `LeviathanStrikeRadius`, so nothing it can see is out of reach). It keeps
  that target until the organism leaves the water, dies or passes
  `LeviathanPreyHold` x the sense radius, and it does not hunt while the strike
  cooldown runs or while a drought pauses predation. The chase brakes into the
  prey (the step is clamped to the distance left) and never translates against
  its facing: inside `LeviathanChaseBand` (250 uu, floored at 1.5 x one
  substep's travel) it holds station rather than slide backwards under a prey
  that has walked past, and it commits to a new direction only once the prey
  leaves that band. The heading then turns at `LeviathanTurnRate` (45 deg per
  logical s) with travel scaled by how far the body still has to turn, so a
  reversal is a slow turn near station rather than a snap. History: the first
  version re-picked the nearest prey every substep and flipped direction
  whenever it was more than 60 uu away along X while one substep moves 150 uu;
  it could not settle inside its own deadband and snapped its 18 m body
  end-for-end at the substep rate (an offline replication of that rule against
  synthetic prey tracks counted ~47 reversals per 10 s of chase). The first fix
  added the brake without the no-backwards rule; an adversarial review
  (146 agents, 13 of 54 claims upheld) found that the heading then froze on
  acquisition while the animal tracked the prey's X backwards, and that the
  interpolated body was drawn toward the world origin before the first substep.
  Both are fixed. The belly clamp follows the riverbed at
  `LeviathanBedFollowRate` for the same reason -- sampled raw, the bed's noise
  bobbed the body between substeps. The rendered body is interpolated between
  substeps (`ASWLeviathan::UpdateVisual`, as `ASWAgent` does for its authored
  body); the actor transform the strike test reads is never interpolated.
  With no prey in range the animal patrols at `LeviathanSpeed`
  with seeded random decisions every `LeviathanTurnInterval` s (x 0.5-1.5):
  reverse with `LeviathanTurnChance`, loiter with `LeviathanLoiterChance`, else
  cruise at 0.8-1.2x. Those draws come from the manager's stream in substep
  order, so a predator run is reproducible for its seed (and, as before,
  differs from the predator-off run of the same seed);
  `Look.LeviathanBody/Glow/Scale/GlowScale` style it, red by default to match
  the drought banner (red = perturbation on the HUD). Enum settings such as
  `Settings.LeviathanTarget` are settable by name through `-SWSet` and the
  control file's `set`. Selection pressure it creates: staying out of the
  channel, which both the built-in bandit and an external policy can learn
  from the existing `on_land` percept and the `avoid` action. With
  `bLeviathan` false every run is byte-identical to the pre-predator build
  (verified seed 7, 120 s).
- **Scientist avatars** (Symbiotic Lab field team, contributed 2026-09-06, off by
  default). Not a mechanism: no genome, no learner, no `ESWSpecies` entry, no
  percept field, no trace deposit, no collision, no draw from the seeded stream,
  so `hello`/`decide`/`actions` are unchanged. `Look.bScientistAvatars` (default
  false; `-SWSet "Look.bScientistAvatars=1"` at launch, `set Look.bScientistAvatars=1`
  in the control file, or key V) renders one body per entry of the
  bridge's `scientists` side message (docs/POLICY_API.md §5), positions the Lab
  computes from the decide stream. The body is Epic's mannequin at true human
  scale (about 1.8 m, against a 3.4 m Lumen and a 7 m Tecton), its paint tinted in
  the scientist's colour, playing idle / walk / jog clips; the name is a
  screen-space HUD tag so the team reads from the start camera (human researcher
  avatars and their jet ski are allowed by the 2026-09-14 and 2026-09-15 amendments
  in docs/SPEC_TEXT.txt). Over water the body rides a jet ski built from engine
  shapes on the visible water line: the organisms' `on_land` terrain test, run at the
  rendered position against the drawn, drought-lowered surface, so during a drought
  the exposed bed is land for the avatar although `on_land` and the hello mask still
  call it water (the Lab may then report `jetski` while the body walks); the Lab routes the team on land using the hello's
  `water_mask` (200 uu cells) and crosses only when the target is on the other
  side, each scientist heading for the nearest organism that fits their rule. Bodies spawn, move (wall-clock smoothing,
  like the camera) and retire on the rendered frame, never inside a substep, and
  hide when no report has arrived for 20 logical seconds. Determinism: with no
  policy server the layer never spawns, so a run with the flag on is byte-identical
  to one with it off; with a bridge attached the run already depends on that
  server's replies (§ fallback rules), and the avatars add nothing to that.
- **Authored creature bodies** (2026-09-11, on by default when the content is
  present). Not a mechanism: the organism's visible body is the imported
  skeletal mesh from `Content/Characters/Symbiotic` (SK_Lumen / SK_Tecton,
  idle + walk clips, authored material) instead of the procedural `SWProc`
  mesh. The clip, its play rate (distance moved per logical step over the
  authored stride) and the rendered position (interpolated between the last
  two substeps) are derived from simulation state on the rendered frame; the
  legs are grounded on the terrain after animation evaluation
  (`USWCreatureMeshComponent`, visual pose only). No sim state, percept,
  collision or seeded draw depends on which body is shown, so a run with
  `Look.bAuthoredCreatures` off (or without the content, which falls back to
  the procedural bodies) is byte-identical to one with it on (verified seed 7,
  120 s). `Look.AuthoredLumenScale` / `AuthoredTectonScale` multiply the
  species' `MeshScale`; a hidden box on the visual mesh is the click target.
  docs/CREATURE_RENDERING.md has the import and inspection steps.
- Signals: a signalling Lumen broadcasts its nearest known resource location
  to same-species neighbours within `NeighbourRange · (0.5 + social)`;
  receivers accept with probability `social`.
- **Trace X / Trace Y** (implemented 2026-09-05 evening, spec §7). Two scalar
  grids over the arena (`TraceCells` = 60 per side over the 8000 x 5500
  rectangle, cells of about 267 x 183 uu; the hack build used 30 per side on
  the 4500 square, 300 uu cells), each
  decaying on the logical clock as `v *= 0.5^(dt / half-life)`; Trace X
  half-life 20 s, Trace Y 120 s; values clamped to `[0, TraceMax]`.
  - *Lumen `modify`* deposits `TraceXDeposit · e/0.5` at its cell (3×3 falloff; base 0.4 so the centre cell never clamps at TraceMax before e = 1).
    Feasible only when a stocked resource of its kind is in sense range, so
    a marker means "food near here". *Lumen `follow`*, when it has no fresh
    signal and no neighbour, climbs the local Trace X gradient if the local
    value is above `TraceXFollowMin`. Information becomes navigable.
  - *Tecton `modify`* deposits `TraceYDeposit · e/0.5` at its cell, costs
    `ModifyBurn` energy per second, feasible on land with > 25 % energy.
    Patch regrowth is multiplied by `1 + TraceYRegenGain · TraceY_reach`, where
    `TraceY_reach` is the strongest Trace Y within `Tecton.ForageRadius` of the
    patch (radius rule since 2026-09-11: Tecton graze from 600 uu, wider than a
    267 x 183 uu cell),
    on top of the drought multiplier, so engineered ground keeps producing
    when the valley dries. No species is told to cooperate; whether Tecton
    soil work helps Lumen is an outcome.
  - *Documented bias:* the spec's `wI · UsefulInteraction` reward term is
    `WeightInteraction` (default 0.10, 0 disables). A deposit that lands
    where it is useful (Lumen: stocked resource in range; Tecton: a patch in
    reach below half stock) adds `wI · (1 − field_before / TraceMax)` to
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
which is fixed because iteration order is deterministic). Verified 2026-09-14
to hold across compile partitions as well (UBT's adaptive unity build compiles
git-modified files outside the unity blob): the one cross-partition mismatch
ever traced was an unspecified evaluation order in patch placement, not
rounding; every probed value matched to 9 significant digits under `/fp:fast`
(docs/CONTRIBUTING.md §3.13).

Live control file (`Settings.ControlFile`, `Settings.ControlFilePollSec`;
`docs/CONTROL_FILE.md`): commands appended to that file while the sim runs are
executed between frames at wall-clock poll times, through the same functions
as the keys (drought toggle, time scale, pause, `-SWSet` reflection, reset,
mode), never inside a substep and never drawing from the seeded stream. A run
that received no command is unchanged by the watch; a run that did is
reproducible only from its `commands.csv`, not from seed + mode alone.

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

### 6b. Balance after the bigger world (2026-09-11)

The arena grew to 8000 x 5500 uu (§4), the river to 30 m x 4 m with two
tributaries, and the authored bodies made the Tecton 11.6 m long, so the old
herd (12 -> 95 Tecton by 600 s on seed 1, all stacked on ten patches) read as a
rock mountain. Sweeps with the dependency-free runner (mode C, seeds 1-3; each
cell is min -> final population, means over the seeds; explore share = fraction
of logged `current_action` samples):

| change (on the new world) | Lumen 600 s | Tecton 600 s | Tecton 1800 s | explore L / T |
|---|---|---|---|---|
| new world, old species params | 40 -> 84 | 12 -> 77 | (not run) | 7.0 % / 7.7 % |
| `ResourcePatchesB` 16 -> 12 | 40 -> 62 | 12 -> 83 | 106 | 7.3 / 7.0 |
| `Tecton.ForageRate` 5 -> 3.5 | 40 -> 68 | 12 -> 59 | | 6.9 / 6.8 |
| ForageRate 3.5 + `MinReproAge` 110 | 40 -> 73 | 12 -> 43 | 111 (B = 16) | 6.9 / 6.5 |
| + `ResourcePatchesB` 10 + `Tecton.MaxAge` 260 (first cut) | 40 -> ~53 | 12 -> ~45 | 60 (39 / 81 / 61) | 7.3 / 6.2 |
| **new default**: the row above + `InitialTecton` 16, `MinReproAge` 100, trace-gradient fix, radius soil rule | 36 -> 107 (93 / 119 / 110) | 12 -> 23 (30 / 19 / 19), min 9 | 62 (104 / 36 / 47), min 9 | 6.7 / 7.7 |
| `Lumen.SenseRange` 900 + `Tecton.SenseRange` 1400 | 40 -> 74 | 12 -> 82 | | 8.0 / 8.0 |
| `Settings.WeightNovelty` 0.3 | 28 -> 46 (seed 1 crashed to 14) | 12 -> 103 | | 10.1 / 9.7 |
| predator on, 1800 s, `Lumen/Tecton.SenseRange` 4000 | 35 -> 110 | 12 -> 40 | kills L 0/0/0, T 4/3/3 | 6.6 / 6.3 |
| predator on, 1800 s, **`Settings.WeightNovelty` 0.2 (new default)** | 38 -> 100 | 12 -> 40 | kills L 3/5/3, T 5/2/3 | 7.5 / 7.9 |
| predator on, 1800 s, no lever | 35 -> 101 | 12 -> 45 | kills L 2/6/2, T 4/1/3 | 6.4 / 5.9 |

Reading: the Tecton boom was intake-driven, not patch-driven (fewer B patches
alone changed nothing at 600 s because B was still at 60 % of capacity); the
intake rate and the age of first reproduction set the growth rate, and B patch
count plus lifespan set the ceiling. New Tecton defaults: `ForageRate` 3.5,
`ForageRadius` 600, `CrowdRadius` 900, `MinReproAge` 100, `ReproThreshold` 145,
`MaxAge` 260; `ResourcePatchesB` 10, `InitialTecton` 16 (twelve founders
reached 4 on one seed before their first offspring). Lumen parameters are
unchanged, but two review fixes lifted Lumen: the trace gradient is now
world-space on the rectangular cells (trail-following works again) and the
soil rule is radius-based (§4). Exploration
did not move with the bigger arena (patches are still inside `SenseRange`).
On the user's request (2026-09-11, "make them more adventurous, cross the
river") `Settings.WeightNovelty` became 0.2: explore share 6.4 -> 7.5 %, more
predator kills (the river-crossing proxy), Lumen never below 38 on seeds 1-3
(0.3 had crashed one seed). A wider `SenseRange` (4000) was rejected: with a
patch always in range the forced random walk never fires and Lumen stopped
entering the water at all. Predation does not select for caution: victims'
epsilon sits at or below the population's, and mean epsilon rises with or
without the predator (`docs/RESEARCH_BACKLOG.md`).
The 2026-09-05 table above is kept as the record of the hack-build balance.

## 7. Scientific guardrails (spec §13)

- Never label survival or recovery as intelligence.
- Never hard-code cooperation and then claim it emerged. There is no
  Lumen–Tecton coupling in the code today beyond shared space.
- Every shown run states its seed and mode (the HUD does; so do the logs).
- Always show a learning-off control (mode A) and, for evolution claims, the
  drift control (mode N).
- Inherited parameters and learned policy are visually separate in the
  inspector.
