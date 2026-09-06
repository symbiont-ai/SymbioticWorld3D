# Research backlog (open questions for the scientist team)

## Exploration is structurally low (noted 2026-09-06 17:40, to discuss)

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
