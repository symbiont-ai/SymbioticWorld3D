# Research backlog (open questions for the scientist team)

## Exploration is structurally low (noted 2026-09-06 17:40; decided 2026-09-11)

**Decision 2026-09-11:** `Settings.WeightNovelty` is 0.2 by default (DESIGN.md §1 / §6b, the documented
bias); `SenseRange` was tried at 4000 and rejected (it removed river crossings). Measured with the predator
on, seeds 1-3, 1800 s: explore share 6.4 -> 7.5 %; predation victims are not more exploratory than the
population (epsilon difference -0.045 to +0.008) and mean epsilon rises 0.19 -> 0.25-0.32 with or without
the predator, so "the adventurous ones get eaten" is not what the logs show. The rest of this note is the
original analysis.

Observed on the live world: Lumen explore 11-15 % of decisions, forage 40-48 %; Tecton explore ~20 %.

Mechanism: reward is pure energy change (`WeightEnergy` 1, `WeightNovelty` 0). Forage pays whenever a
stocked patch is inside `SenseRange` (1600 uu in a 9000 uu arena, so almost always), explore costs
`MoveBurn` and never pays by itself, so the greedy choice becomes forage within a few dozen decisions.
Explore then only happens through epsilon (0.2 over 5-6 feasible actions, ~3 %) plus the forced explore
when no resource is known.

Levers that exist today, no build (control file `set ...` live, or `--set` per run):

| lever | field | note |
|---|---|---|
| optimistic initial values | `Settings.QInitMax` 0.05 -> ~0.5 | unbiased, deterministic; newborns (or reset) |
| curiosity reward | `Settings.WeightNovelty` 0 -> ~0.3 | novelty = first visit of a 300 uu cell per interval; strongest; **a documented bias** (DESIGN.md §1) |
| founder epsilon | `Genome.Epsilon` 0.2 -> 0.35 | next reset; evolution then answers whether selection pushes epsilon back down |
| sense range | `Lumen.SenseRange` 1600 -> ~700 | forced explore fires more; newborns |
| scarcity | `Settings.ResourcePatchesA`, `PatchCapacity` | next reset; crash risk |
| explore cost | `Lumen.MoveBurn` 1.2 -> 0.6 | newborns |

Not acceptable: an explore quota or any scripted behaviour (spec: learn it, do not script it).

Proposed experiment (experiment service, `docs/SCIENTIST_API.md`): four conditions x modes C and N x
3 seeds x 600 s: baseline, `QInitMax=0.5`, `WeightNovelty=0.3`, `Genome.Epsilon=0.35`. Read: explore share,
cells visited per organism, final population, per-generation mean epsilon (selection on exploration).

## River crossings: the bank cycle (2026-09-18)

**Result.** Crossing follows food, and food is only across the river when the world puts it there. The bank
cycle (DESIGN.md §4, `Settings.bBankCycle`, off by default) alternates Lumen-food regrowth between the two banks
of the main channel every 600 s after a 300 s warm-up, so under the preset (home bank emptied at once, Lumen
`SenseRange` 8000) the forage rule - the nearest patch of the preferred type with stock > 0.5 inside
`SenseRange` - walks across. The preset as first ported from `claude/river-levers` failed: mode C, seeds 1-5 x
1800 s, every seed fell below 20 Lumen, 2 of 5 went extinct, and per-switch Lumen survival (minimum Lumen in the
300 s after a switch over Lumen at the switch) was 0.06-0.17 at the second switch on every seed. The diagnosis
(9-agent workflow, 4 lenses + adversarial verify) found a famine on the trip, not an empty destination: the
strongest predictor of starvation was energy per metre at the start of the trip (logistic z -7.1; 100% starved
below 0.6 energy/m, 71% at 0.9-1.2); the switched-off bank took about 10 s to empty and the forage rule kept residents chasing it; 36% of the
switch-time Lumen were within 60 s of `Lumen.MaxAge` 150; gamma = 0 erosion of the forage value during the walk was real but secondary
(late wave only). More crossings did not go with better per-switch survival. Ruled out: an empty
destination bank, crowding at the destination. A screen on held-out seeds 6-15 (promote only if 0 extinct,
Lumen never below 20, >= 12 Lumen crossings per switch in the 300 s after each switch, Tecton never extinct)
promoted `Settings.bBankCycleHardOff` (switched-off patches emptied at once) + `Lumen.MaxAge` 250: 0 extinct,
lowest 22, 50.4 crossings per switch. It was chosen over the also-promoted variant with `Look.RiverWidth` 700
(lowest 20) because it keeps the river unchanged. The preregistered exit test (mode C, seeds 1-5 x 1800 s)
PASSED: Lumen lowest 32/29/28/31/34 (as ported 10/0/0/4/8), Tecton lowest 16/16/14/14/16, Lumen crossings per
switch (counted from one switch to the next) 57.0/53.7/62.0/57.0/65.3 (as ported 15.3/11.3/22.7/24.7/16.3),
per-switch survival median 0.34 (worst 0.20), energy on arrival median 28.1 (as ported 22.9). That valley was
crowded (Lumen up to ~170, Tecton up to ~125, the shared 220 cap reached on 3 of 5 seeds), so new per-species
caps `Settings.MaxTecton` 30 and `Settings.MaxLumen` 150 joined the preset; with them the same exit test passes
(Lumen lowest 26/29/28/32/36, Tecton never above 30, Lumen + Tecton peak 152-180, crossings per switch
52.0/48.0/55.0/51.0/69.7). `Tools/run_sim.py --preset bank-cycle` carries all of it.

**Tecton in the cycle: no.** Four preregistered screens (seeds 6-15, scope `Both`) found no setting that keeps
both species safe. Seeing further gets Tecton across (0.1 -> 6.0 crossings per switch at `SenseRange` 2200 ->
12000) but 5 of 10 seeds still go extinct: a stranded Tecton is a median 116 s into its 260 s life, takes ~71 s
to cross and lands far below its reproduction threshold, so it dies of age before breeding. A longer Tecton life
(520 s) saves the Tecton but crowds the valley, and capping them then costs Lumen one seed below 20 per arm.

**What it is not.** Nothing is signalled to the organisms: no percept, action or reward term reads the bank or
the phase (outside the patch loop only the HUD and the logger do). The crossings are the forage rule following
food, not learned or inherited behaviour, and must not be presented as either. The preset's
`Lumen.SenseRange` 8000 does not reverse the 2026-09-11 rejection of a wider sense range above: in the default
world a wider range only keeps a home-bank patch in view, while under the cycle the home bank is empty and the
nearest stocked patch is across.

**Open.** (0) The pass covers 1800 s (three switches) and no drought. A live 7200 s run of the capped preset dipped to
17 Lumen after the ninth switch, and a drought toggled at 6210 s took Lumen from 82 to 1 with no recovery while
food piled up: the drought cut the grazed active bank right after a switch. The next gate is a long run (at least 10
switches) with a drought. (1) The pass leans on the Lumen lifespan inside the regime (`MaxAge` 250; the default world keeps
150): on the screen the hard cut alone left 10/10 seeds below 20 Lumen and `MaxAge` 250 alone 6/10. Each
switch still kills about two thirds of the Lumen before they recover (per-switch survival median 0.34-0.35).
(2) Learner changes, e.g. anything that would let a trip be credited against the gamma = 0 erosion seen in the
late wave, or leaving ReproCost out of the reward, are the owner's decision; the pass does not depend on them.
(3) Tecton levers left untried: `ReproThreshold`, and a `ResourceB`-only cycle with a longer period.
(4) Before any claim beyond "the food moved and foraging followed", run the learning-off (mode A) and drift
(mode N) controls under the same preset (DESIGN.md §7); predator + cycle is a separate experiment (the preset
leaves `bLeviathan` off).
