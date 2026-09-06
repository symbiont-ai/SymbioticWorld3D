# Symbiotic World

Sundai Hack 139 entry. A persistent 3D ecosystem in Unreal Engine 5.7 with two
populations of primitive learning agents, **Lumen** (fast cyan scouts) and
**Tecton** (slow amber ecosystem engineers). Every individual learns online
during its lifetime with a tabular contextual bandit; descendants inherit
mutated learning parameters (α, ε, social) and an environmental-effect strength (e). The claim the demo has to earn:
*training was only generation zero.*

Spec: `docs/SPEC_TEXT.txt` (text of the hack-day specification; concept plates in
`docs/plates/`). Mechanism definitions: `DESIGN.md`.
Build plan with exit conditions: `CHECKLIST.md`.

## Requirements

- Unreal Engine 5.7 (installed at `C:\Program Files\Epic Games\UE_5.7`)
- Visual Studio 2022 17.8+ or Visual Studio 2026 with the
  "Game development with C++" workload (VS 2026 / MSVC 14.50 verified)
- Python 3 with pandas, matplotlib, scipy for the analysis scripts
  (`python` works)

## Build

```bash
"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" SymbioticWorldEditor Win64 Development -Project="%CD%/SymbioticWorld.uproject" -WaitMutex
```

First build ~2 min, incremental ~10 s. Then double-click `SymbioticWorld.uproject`
or open it from the Epic launcher. The startup map is `/Game/Maps/Valley`
(deliberately empty: the game mode spawns floor, sun, sky, fog and the world
manager at runtime). Press Play.

If `Content/Maps/Valley.umap` is missing, recreate it:

```bash
"C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "%CD%/SymbioticWorld.uproject" -run=pythonscript -script="%CD%/Tools/make_valley_map.py"
```

## Controls

| Key | Action |
|---|---|
| LMB | select organism (opens inspector) |
| Tab | select the youngest Lumen |
| F | follow selected organism |
| 1 / 2 / 3 | 1× / 10× / 50× logical time |
| Space | pause |
| P | drought on/off |
| M | cycle mode A → B → C → N (resets run) |
| R | reset run (same seed) |
| H | hide/show help |
| WASD / QE, RMB drag, wheel | camera |

Modes: **A** learning off (α = 0) · **B** learning on, genome fixed ·
**C** learning + evolution · **N** neutral-drift control (random parent, no
starvation death). C must separate from N across seeds before you may claim
selection on learning parameters.

## Headless runs and analysis (closed loop)

```bash
python Tools/run_sim.py --mode B --seed 42 --duration 300 --analyze
python Tools/run_sim.py --mode C N --seed 1 2 3 --duration 900 --analyze
python Analysis/analyze_run.py --root Saved/SymbioticWorld
python Tools/sweep.py --mode C --seed 1 2 --duration 600 --baseline --set "Settings.PatchRegenPerSec=6"
python Tools/run_sim.py --mode C --seed 1 --duration 45 --speed 1 --windowed --shot 4,40 --auto-select --no-logs
```

`sweep.py` runs one headless sim per (override spec × mode × seed) and prints
min/final population, starvation deaths, max generation and end-of-run mean α
per row, plus a CSV in `Saved/`. `run_sim.py --windowed --shot` makes the sim
screenshot itself (closed-loop visual check without a human at the keyboard).

Each run writes `Saved/SymbioticWorld/<run_id>/{agents,births,deaths,population}.csv`
(Appendix A fields plus mode, energy bin, explore flag and the full 3×7 Q
table) and quits itself at `--duration` logical seconds. `analyze_run.py`
prints lifetime Q drift per agent, parent/child genome correlation,
per-generation means, and a Welch test of C vs N end-of-run mean α, plus a
summary PNG per run.

Command-line flags understood by the sim (all optional):

```
-SWMode=A|B|C|N   -SWSeed=42   -SWSpeed=200   -SWDuration=600   -SWNoLogs=1
-SWSet="Settings.PatchRegenPerSec=5;Lumen.ReproThreshold=85;Tecton.MaxAge=400;Genome.Alpha=0.15"
-SWShot=5:60:120        # screenshots to Saved/Screenshots at these sim times (needs rendering; ':' because UE stops parsing at ',')
-SWAutoSelect=1         # select the youngest Lumen at start so screenshots show the inspector
-SWCam=x:y:z:pitch:yaw  # start camera for scripted shots (run_sim: --cam=x,y,z,pitch,yaw)
-RenderOffScreen        # run_sim: --offscreen; renders and screenshots without a visible window,
                        # so a scripted render never captures your keystrokes (M/P/1-3 would change the run)
```

`-SWSet` reaches any numeric/bool/colour/string field of `FSWRunSettings` (scope `Settings`),
`FSWSpeciesParams` (`Lumen` / `Tecton`), the founder `FSWGenome` (`Genome`) or the
visual `FSWLookSettings` (`Look`, colours as `r:g:b`) by name, so parameter and look
sweeps never need a recompile. Field names are in `Source/SymbioticWorld/SWTypes.h`.
Look examples: `Look.SunPitch=-6;Look.SunYaw=176;Look.FillIntensity=0.9` (lighting rig:
sun + shadowless fill), `Look.ArchCount=3` (massif sandstone arches), `Look.bCliffWalls=true`
(rim walls, off by default: the valley is only 280 m across), `Look.bDroughtPreview=true`,
`Look.bArchFalls=false` (waterfalls off the arch crowns), `Look.FogMaxOpacity=0.7` (below 1 the
sun and sky show through the horizon haze), `Look.TraceOverlayIntensity=0.55` (ground trace
stains), `Look.WaterBrightness=0.6`, `Look.bMoon=false`.

Materials that go onto instanced components (`M_SW_Scan`, `M_SW_Rock`, `M_SW_Glow`) carry
`used_with_instanced_static_meshes`; without it the runtime logs
`missing bUsedWithInstancedStaticMeshes=True` and silently draws the default grey material.

Generated materials live in `Content/Materials` and are rebuilt from
`Tools/make_materials.py` (`M_SW_Terrain/Rock/Water/Creature/Glow/Trail/Waterfall/Moon`,
`M_SW_TerrainED` from the migrated Megascans surface sets, `M_SW_Sandstone` for the
arches, and `M_SW_Scan`, which the environment binds at spawn to every Electric Dreams
scan mesh from its own Albedo/Normal/DR textures). Delete a `.uasset` and re-run the
generator to rebuild it; then grep `Saved/Logs/SymbioticWorld.log` for
`Failed to compile Material`, which is how a bad sampler type shows up.

## What is verified (2026-09-05)

| Claim | Evidence |
|---|---|
| Same individual learns | mode A: median Q drift 0.000, greedy never changes; mode B/C: drift 0.8–1.4, greedy changes in 60–85% of Lumen |
| Inherited ≠ learned | inspector shows α/ε/social fixed across a lifetime while the Q table moves (self-screenshots at t=4 and t=40) |
| Inheritance with mutation | parent/child correlation 0.80–0.94 for α, ε, social, e; mean |Δ| ≈ 0.022–0.024 (σ = 0.03) |
| Neutral control works | mode N: ~175 births / 600 s, population held at target, no starvation |
| Reproducible | same mode + seed ⇒ byte-identical CSVs |
| Population viable | default regen 6: Lumen 40→63 (min 40), Tecton 12→25, 12 generations / 600 s, seed 1 |

Not yet verified: interactive Play-in-Editor keys (needs a human), selection on
α distinguishable from drift (needs more seeds / longer runs after balance),
Trace X/Y, drought tuning.

## Layout

```
Source/SymbioticWorld/
  SWTypes.*          enums, genome, species params, run settings, percept
  SWLearner.*        FSWContextualBandit (the lifetime learner)
  SWAgent.*          one organism: sense → gate → select → act → reward → update
  SWResourcePatch.*  logistic-regrowth resource patches (A = Lumen, B = Tecton)
  SWWorldManager.*   seeded RNG, fixed logical step, reproduction, modes, stats
  SWLogger.*         CSV run logs
  SWGameMode.*       runtime environment + manager spawn
  SWCameraPawn.*     observer camera
  SWPlayerController.* key bindings
  SWHUD.*            canvas HUD: global stats, meta-parameter strip, inspector
Config/              legacy input mappings, renderer settings (Lumen GI, VSM, TSR)
Content/Maps/Valley  empty startup level
Tools/               run_sim.py (launcher), sweep.py (parameter sweeps), make_valley_map.py
.claude/             agents/implementer.md, agents/tester.md, skills/phase (Manager Loop)
Analysis/            analyze_run.py
```

## Credits

- Environment dressing uses assets migrated from Epic Games' **Electric Dreams Environment** sample (Epic Games; Quixel Megascans; RealityScan), used under the Unreal Engine EULA / Epic Content Licence (Unreal Engine projects only). Those assets are **not in this repository**: install the sample from the Epic launcher and run `Tools/migrate_ed.py` with the role list in `AssetSources/ed_manifest.json`. A fresh clone without them runs procedural-only (the environment logs it and falls back), and `M_SW_TerrainED`, `M_SW_Sandstone` and `M_SW_Scan` render with default textures until the migration has run. Poly Haven scans (CC0) are likewise fetched, not committed (`Tools/fetch_polyhaven.py`, `Tools/import_assets.py`).
- The concept plates in `docs/plates/` come from this project's own specification (Appendix B) and are illustrations, not screenshots.
- CC0 scans from **Poly Haven** (polyhaven.com), fetched by `Tools/fetch_polyhaven.py`.
- Everything else (terrain, arches, creatures, materials, HUD) is generated by the code in this repo.
