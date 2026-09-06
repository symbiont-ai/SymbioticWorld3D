# Symbiotic World — build checklist

Manager-loop style: each phase has an exit condition; do not start the next
phase until the current one is met. Mirrors §11 of the spec plus the prep
work done the day before.

## Day -1 (prep, 2026-09-05)

- [x] Scaffold moved into repo, engine association bumped to 5.7, gitignore
- [x] SWPlayerController + SWHUD written (last two missing classes)
- [x] **Compiles** with UBT (`Build.bat SymbioticWorldEditor Win64 Development`)
- [x] Empty `/Game/Maps/Valley` level exists and is the default map
- [x] Windowed `-game` run shows floor, sun, sky, fog, cylinders (patches), spheres
      (agents) moving; HUD draws; auto-select shows the inspector (self-screenshots)
- [ ] **Editor opens** and Play-in-Editor works; LMB selects; Tab cycles; 1/2/3 change
      speed; P drought; M mode — needs a human at the keyboard (not verifiable headless)
- [x] Headless run works: `python Tools/run_sim.py --mode B --seed 42 --duration 300`
      writes `Saved/SymbioticWorld/<run_id>/*.csv` and exits on its own
- [x] `Analysis/analyze_run.py` reports lifetime Q drift ~0 in mode A, > 0 in mode B
- [x] 3 seeds × {C, N} run; comparison printed (selection vs drift) — even if
      not yet significant, the pipeline works
- [x] Balance pass: population does not crash to 0 or pin at cap within 600 s in C
- [ ] README build steps verified from a clean `git clone`
- [x] Manager Loop: `.claude/agents/implementer.md`, `.claude/agents/tester.md`, `/phase` skill
- [x] Mode N produces births (founder age stagger); determinism: same seed → identical population.csv

## Day 0 (hack, 2026-09-06) — order agreed on the evening of 2026-09-05

Priority order from the user: (1) asset review + environment and actors as
good as they can be, (2) the learning / evolving research, (3) the AI scientist
only once everything works. Exit conditions are what the tester checks.

| # | Block | Deliverable | Exit condition |
|---|---|---|---|
| 1 | Asset review (DONE 2026-09-05 evening: 274 packages / 3.4 GB migrated, roles resolve) | `docs/ED_ASSET_REVIEW.md`, `AssetSources/ed_manifest.json`, migrated Electric Dreams subset (<= 6 GB) | every role in the manifest resolves to a StaticMesh/material in our project; `Imported dressing: yes [manifest]` in the log |
| 2 | Environment from roles (DONE 2026-09-05 evening; perf ladder still to measure) | cliffs, arch rocks, boulders, river stones, groundcover, shrubs, trees, ground/water material, mist wired through `ASWEnvironment` | wide + close self-screenshots judged closer to the plates than `SW_20260905-110358_*`; 1x frame <= 16.7 ms at 55 agents (`stat unit` still) |
| 3 | Actors (Lumen dotted/legs + Tecton cracks DONE; head crest, translucency, dust optional) | Lumen sleeker (dotted spine, longer legs), Tecton plates + cracks kept, gait; optional Blender FBX swap only if two iterations fail | close-up screenshot; determinism unchanged (same seed => identical CSVs) |
| 4 | Perf ladder | Lumen GI off (done in ini), clouds decision, VSM, screen %, counts | 50x with 220 agents <= 22 ms |
| 5 | Trace X / Trace Y (DONE 2026-09-05 evening; tune half-lives, deposits, wI on the research day) | Lumen information field + Tecton terrain/moisture field, `modify` action feasible, fields logged | `trace_X_mean`, `trace_Y_mean` in population.csv; each species measurably changes a field |
| 6 | Learning / evolution research | balance across seeds 1-5 and 1800 s; selection on alpha vs drift (C vs N) with p < 0.05 or an honest negative | `Analysis/analyze_run.py` cross-run table + plots in `docs/` |
| 7 | Drought | visible event (warm grade, water drop, straw vegetation) + measurable behaviour shift | before/after action distributions in the log; screenshot pair |
| 8 | Demo | camera rail (`-SWDemo=1`), 90 s story, backup video via `-benchmark -dumpmovie` | rehearsed twice from the demo seed |
| 9 | AI scientist (P3, last) | intervention/narration agent over the CSV logs | only after 1-8 are green |

## Cut order (spec §11)

P0 same individual learns + visible; reproduction; inherited/mutated
meta-parameters; population change across generations.
P1 two species; environmental modification; drought; speeds; control mode.
P2 inspector; evolution chart; polished HUD; VFX.
P3 goal drift; social learning; policy inheritance; AI scientist; Python analysis.

If behind: cut P3, then P2 VFX, then Trace Y deformation. Never cut lifetime
learning or inherited learning parameters.
