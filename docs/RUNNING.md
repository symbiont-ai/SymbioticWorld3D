# Running Symbiotic World on the Windows host

Everything needed to build the module, launch the world, drive it from the keyboard and get
CSV logs out of it. What the simulation *is* — the mechanisms, the modes and what has been
verified — lives in `README.md` and `DESIGN.md`; this file is the machine side.

Related: `docs/STREAMING.md` (watch and drive it from other computers), `docs/RECORDING.md`
(scripted demo takes), `docs/CONTROL_FILE.md` (perturb a running world from a script),
`docs/POLICY_API.md` (hand organisms to your own Python agents),
`docs/SCIENTIST_API.md` (queue runs over HTTP), `docs/CONTRIBUTING.md` (change the sim itself).

## Requirements

- Unreal Engine 5.7 (installed at `C:\Program Files\Epic Games\UE_5.7`)
- Visual Studio 2022 17.8+ or Visual Studio 2026 with the
  "Game development with C++" workload (VS 2026 / MSVC 14.50 verified)
- Python 3 with pandas, matplotlib, scipy for the analysis scripts
  (`python` works)

## Build

```bash
Tools/build.bat
```

It wraps the engine's `Build.bat` (SymbioticWorldEditor Win64 Development) and refuses to
run while any `UnrealEditor` / `UnrealEditor-Cmd` process exists: the module DLL is locked,
the link fails, and replacing the DLL under a live session crashes it. Pass
`-DisableAdaptiveUnity` when a build's codegen must match a clean checkout's exactly
(UBT compiles git-modified files outside the unity blob, which has produced runs that
differ from the same seed built the other way).

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
| V | scientist avatars on/off (field team of a `Lab.lab observe --embody` bridge; visual only, off by default) |
| G | follow the next scientist (cycles the field team in join order; F or any camera move releases) |
| WASD / QE, RMB drag, wheel | camera |

**M** cycles the four modes A / B / C / N and resets the run. What each mode is, and why C must
separate from N before anything counts as selection: `README.md` and `DESIGN.md` §3.

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

`--preset bank-cycle` bundles the bank-alternating regrowth regime with the settings that
make it survivable (`DESIGN.md` §4, "Bank cycle"); it is a research regime, off by default,
and not yet safe for long runs or droughts.

Each run writes `Saved/SymbioticWorld/<run_id>/{agents,births,deaths,population}.csv`
(plus `commands.csv` when a control file is watched; `docs/CONTROL_FILE.md`)
(Appendix A fields plus mode, energy bin, explore flag and the full 3×7 Q
table) and quits itself at `--duration` logical seconds. Since 2026-09-06
`agents.csv` ends with a `policy` column (`builtin`, or `ext:host:port` when an
external policy server chooses that organism's actions) and `population.csv`
ends with `ext_decisions,ext_fallbacks` (cumulative per species: decisions taken
from a server / server-assigned decisions the built-in bandit had to make). Both
are constant (`builtin`, `0,0`) in runs without `--policy`. Since 2026-09-18 the
river-crossing counter and positions are logged too: `agents.csv` adds
`river_crossings,x,y,river_dist,in_water`, `deaths.csv` adds
`river_crossings,mid_crossing`, and `population.csv` adds
`river_crossings,mean_river_dist,frac_in_water,active_bank,resource_A_pos,resource_A_neg`
(the last three describe the optional bank cycle; `active_bank` is 0 when it is off). Full
column lists: `docs/CONTRIBUTING.md` §5(vi). `analyze_run.py`
prints lifetime Q drift per agent, parent/child genome correlation,
per-generation means, and a Welch test of C vs N end-of-run mean α, plus a
summary PNG per run.

## Command-line flags understood by the sim (all optional)

```
-SWMode=A|B|C|N   -SWSeed=42   -SWSpeed=200   -SWDuration=600   -SWNoLogs=1
-SWSet="Settings.PatchRegenPerSec=5;Lumen.ReproThreshold=85;Tecton.MaxAge=400;Genome.Alpha=0.15"
-SWShot=5:60:120        # screenshots to Saved/Screenshots at these sim times (needs rendering; ':' because UE stops parsing at ',')
-SWAutoSelect=1         # select the youngest Lumen at start so screenshots show the inspector
-SWCam=x:y:z:pitch:yaw  # start camera for scripted shots (run_sim: --cam=x,y,z,pitch,yaw)
-SWSet="Look.bAuthoredCreatures=0"             # procedural creature bodies instead of the authored Lumen/Tecton skeletal meshes (docs/CREATURE_RENDERING.md)
-SWFollowSpecies=Lumen|Tecton|Leviathan         # chase camera on the selected organism of that species (selecting one if needed), or on the river predator; any camera key releases it
-SWFollowScientist=Vesper|any                   # chase camera on a field-team scientist once the avatar joins (needs Look.bScientistAvatars=1 and an --embody bridge); key G cycles the team live
-SWCreatureAudit                                # log creature pose-update ms + foot reach error at each -SWShot time
-RenderOffScreen        # run_sim: --offscreen; renders and screenshots without a visible window,
                        # so a scripted render never captures your keystrokes (M/P/1-3 would change the run)
-SWPolicy="host:port=Lumen|host:port=Tecton"   # external policy servers (run_sim: --policy); '|' and '=' only, no ',' or ';'
-SWPolicyTimeoutMs=200  -SWPolicyShare=1.0     # run_sim: --policy-timeout / --policy-share; see docs/POLICY_API.md
-SWSet="Settings.PolicyTimeoutBackoffAfter=3;Settings.PolicyBackoffStartSec=1;Settings.PolicyBackoffMaxSec=10"   # a server that misses 3 replies in a row is left alone (its organisms use the built-in bandit) and retried on a growing interval, so a wedged bridge never slows the world
-SWSet="Settings.bLeviathan=1"                  # river predator (off by default; DESIGN.md §4); live: control "set Settings.bLeviathan=1" then "reset"
-SWSet="Look.bScientistAvatars=1"               # Symbiotic Lab field-team avatars (visual only, off by default; docs/POLICY_API.md §5); live: control "set Look.bScientistAvatars=1"
-SWPolicyFile=Saved/policy_servers.txt         # server list file polled every 3 s while running (run_sim: --policy-file); edit it to add/remove servers
-SWControlFile=Saved/control.txt               # live control file polled every 2 s (run_sim: --control-file): append "drought=on", "set Lumen.MaxAge=200", "reset seed=3" ... (docs/CONTROL_FILE.md)
-SWSet="Look.bShowHUD=0"                       # hide the whole HUD for clean framing (live: control "set Look.bShowHUD=0"); scenario captions still draw
-SWSet="Look.CaptionSeconds=10"                # how long a control "caption=<text>" title stays up (Look.bShowCaptions=0 turns the layer off)
-SWSet="Look.bPredationEffects=0"              # no blood plume, sinking body, kill feed or minimap mark on a predation kill (default on)
-SWSet="Look.bShowLabPanel=1"                  # SYMBIOTIC LAB panel in the sim's HUD (the bridge's own log lines; OFF by default, the lab's dashboard `python -m Lab.lab ui` is the normal way to watch it)
```

`run_sim.py --windowed` opens a 1600x900 window; `--res WxH` (e.g. `--res 1920x1080`) changes it, which is
what a screen recorder captures.

## `-SWSet`: every setting by name, no recompile

`-SWSet` reaches any numeric/bool/colour/string field of `FSWRunSettings` (scope `Settings`),
`FSWSpeciesParams` (`Lumen` / `Tecton`), the founder `FSWGenome` (`Genome`) or the
visual `FSWLookSettings` (`Look`, colours as `r:g:b`) by name, so parameter and look
sweeps never need a recompile. Field names are in `Source/SymbioticWorld/SWTypes.h`.
Look examples: `Look.SunPitch=-6;Look.SunYaw=176;Look.FillIntensity=0.9` (lighting rig:
sun + shadowless fill), `Look.ArchCount=3` (massif sandstone arches), `Look.bCliffWalls=true`
(rim walls, off by default), `Look.RiverBranches=0` (main channel only; default 2 tributaries),
`Settings.WorldHalfSizeY=8000` (square arena again), `Look.bDroughtPreview=true`,
`Look.bArchFalls=false` (waterfalls off the arch crowns), `Look.FogMaxOpacity=0.7` (below 1 the
sun and sky show through the horizon haze), `Look.TraceOverlayIntensity=0.55` (ground trace
stains), `Look.WaterBrightness=0.6`, `Look.bMoon=false`, `Look.AuthoredTectonScale=3.0` (authored creature size,
docs/CREATURE_RENDERING.md).

Which settings take effect live and which need a reset: `docs/CONTROL_FILE.md`.

## Materials

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

The environment assets themselves are licensed and not in the repository; see "Credits" in
`README.md` and `docs/ASSET_DOWNLOADS.md`.
