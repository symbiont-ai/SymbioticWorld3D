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
Contributing (Mac collaborators, coding agents, recipes for actions/percepts/species/learners/perturbations/analysis): `docs/CONTRIBUTING.md`.

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
table) and quits itself at `--duration` logical seconds. Since 2026-09-06
`agents.csv` ends with a `policy` column (`builtin`, or `ext:host:port` when an
external policy server chooses that organism's actions) and `population.csv`
ends with `ext_decisions,ext_fallbacks` (cumulative per species: decisions taken
from a server / server-assigned decisions the built-in bandit had to make). Both
are constant (`builtin`, `0,0`) in runs without `--policy`. `analyze_run.py`
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
-SWPolicy="host:port=Lumen|host:port=Tecton"   # external policy servers (run_sim: --policy); '|' and '=' only, no ',' or ';'
-SWPolicyTimeoutMs=200  -SWPolicyShare=1.0     # run_sim: --policy-timeout / --policy-share; see docs/POLICY_API.md
-SWSet="Settings.bLeviathan=1"                  # river predator (off by default; DESIGN.md §4); live: control "set Settings.bLeviathan=1" then "reset"
-SWPolicyFile=Saved/policy_servers.txt         # server list file polled every 3 s while running (run_sim: --policy-file); edit it to add/remove servers
-SWControlFile=Saved/control.txt               # live control file polled every 2 s (run_sim: --control-file): append "drought=on", "set Lumen.MaxAge=200", "reset seed=3" ... (docs/CONTROL_FILE.md)
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

## Bring your own agent (Python, any OS)

Collaborators on the same wifi write their own agents in Python and have them
control organisms inside the one world running on the Windows host. No Unreal, no
pip installs: `Tools/policy_server.py` is standard library only (Python 3.9+, runs on
macOS). The sim connects to *their* machine, sends each organism's percept, feasibility
mask and its own bandit table once per decision, and acts on the reply; anything late
or infeasible falls back to the organism's built-in bandit and is counted. Protocol,
field list and fallback rules: `docs/POLICY_API.md`. To go beyond the policy API and
change the sim itself (C++ the host builds for you): `docs/CONTRIBUTING.md`.

On the Mac (4 commands, nothing to install):

```bash
git clone <this repo> && cd Symbiotic_Word_3D          # or just copy Tools/policy_server.py
python3 Tools/policy_server.py --agent my --port 9000   # edit MyAgent.act() in that file; see the agent list below
python3 Tools/policy_client_check.py --port 9000        # optional self-test from a second terminal
ipconfig getifaddr en0                                  # tell the host this IP
```

Example agents in `Tools/policy_server.py` (`--agent`):

* `random`: uniform over the feasible actions, the floor to beat.
* `bandit`: the sim's own tabular contextual bandit (γ = 0) re-implemented in Python, α/ε from the genome.
* `heuristic`: fixed rules on the percept (forage when food is known and energy is not HIGH, avoid other-species neighbours when LOW, rest when LOW with nothing known, else explore).
* `tracefollower`: Lumen follow up a strong Trace X gradient, Tecton modify (Trace Y) on land at HIGH energy, else forage/explore.
* `my`: yours.

No Unreal on your machine? `python3 Tools/policy_server.py --record run.jsonl` logs every
exchange, `python3 Tools/policy_replay.py --agent my --file docs/samples/decide_sample.jsonl`
replays a recording against your class offline (mask violations, action distribution,
timing), and the repo ships a 300-exchange sample. See "Offline development on a Mac" in
`docs/POLICY_API.md`.

On the host, while the sim runs (no restart): add one line per server to
`Saved/policy_servers.txt` and save. The sim polls the file every 3 s, connects new servers,
closes removed ones and rebinds organisms (`docs/POLICY_API.md`, "Adding servers while the
sim runs"; template `Tools/policy_servers.example.txt`). `Tools/policy_probe.py` finds the
servers on the wifi and can append them for you:

```bash
python Tools/policy_probe.py --write Saved/policy_servers.txt   # scans the host's own /24 for port 9000, appends "host:port=Both" lines
python Tools/run_sim.py --mode C --seed 7 --duration 600 --speed 20 --windowed                     # the default file is watched automatically
python Tools/run_sim.py --mode C --seed 7 --duration 600 --speed 20 --windowed --policy "10.228.152.5:9000=Lumen|10.228.152.7:9000=Tecton"   # launch-time alternative
```

Species per server: `Lumen`, `Tecton` or `Both`; several servers share one world. The
HUD title shows `ext N/M` (organisms currently bound to a server / total), the inspector shows
`policy: external host:port`, and the UE log prints connect / hello / timeout lines
once per state change plus a round-trip summary every 10 s. Allow `UnrealEditor.exe`
through Windows Firewall on private networks if the host cannot reach a Mac.

## Watching and driving the sim from other computers (Pixel Streaming)

The sim runs once, on the Windows machine; anyone on the same network watches and
controls it from a browser (Chrome or Safari, including Macs). One-time setup:
fetch Epic's signalling server with the engine's script
`Engine/Plugins/Media/PixelStreaming2/Resources/WebServers/get_ps_servers.bat`
(the `PixelStreaming2` plugin is enabled in the `.uproject`). Then, in two terminals:

```bash
Tools\start_stream_server.bat
Tools\stream_keepalive.bat
```

`stream_keepalive.bat` runs `run_sim.py --stream --offscreen` for the day and relaunches it
after any exit (create `Saved\stop_stream` to stop the loop). Viewers open
`http://<host LAN IP>/?AFKDetection=false&HoveringMouse=true` and click to start. The two
parameters matter: the player page's idle timeout disconnects a still viewer after 120 s and
that disconnect has aborted the Pixel Streaming media layer three times (exit 0xC0000409,
no crash report); hovering-mouse mode makes clicks select organisms instead of grabbing the
camera. Mouse and keyboard from the
browser reach the sim (select, follow, drought, speed, camera), so agree on one driver at a
time. Allow `node.exe` on private networks when Windows Firewall asks. The stream is
encoded on the host GPU (NVENC on NVIDIA); hackathon wifi that isolates clients from each
other blocks it, in which case fall back to screen sharing.

To perturb the running world from a script or a second terminal instead of its keyboard (drought,
speed, pause, any `--set` parameter, reset, mode), append lines to `Saved/control.txt`:
`python Tools/control.py "drought=on"`; every executed command is logged to the run's `commands.csv`.
Grammar and which settings take effect live: `docs/CONTROL_FILE.md`.

## Scientist agents: experiment service

`python Tools/experiment_service.py` (host, port 8800, no authentication: LAN only) turns the
closed loop into a JSON API for scientist agents on other machines: `POST /runs` queues headless
runs (mode x seeds, `--set` overrides, one at a time next to the demo), `GET /runs/<id>` returns
each run's statistics (population, lifetime bandit-table drift, parent/child correlation,
per-generation means, and a Welch test of C vs N end-of-run mean α when a job has both modes),
`GET /runs/<id>/files/population.csv` the raw logs, `GET /live` the running world's latest
population rows, `POST /control` appends a validated line to `Saved/control.txt`, `POST /notes`
keeps a shared notebook. Client: `python3 Tools/scientist_client.py --host <host-ip> runs --mode C N
--seeds 1 2 3 --duration 600 --wait`. Endpoints, summary fields, control grammar and an
experiment recipe: `docs/SCIENTIST_API.md`.

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
  SWPolicyClient.*   TCP client for external policy servers (docs/POLICY_API.md)
  SWGameMode.*       runtime environment + manager spawn
  SWCameraPawn.*     observer camera
  SWPlayerController.* key bindings
  SWHUD.*            canvas HUD: global stats, meta-parameter strip, inspector
Config/              legacy input mappings, renderer settings (Lumen GI, VSM, TSR)
Content/Maps/Valley  empty startup level
Tools/               run_sim.py (launcher), sweep.py (parameter sweeps), make_valley_map.py,
                     policy_server.py (reference agents: random / bandit / heuristic / tracefollower / MyAgent stub),
                     policy_client_check.py (one fake exchange), policy_replay.py (offline replay of a --record file),
                     policy_probe.py (finds servers on the wifi, writes the server list file), policy_servers.example.txt,
                     control.py (appends validated commands to the live control file, --tail shows what ran; docs/CONTROL_FILE.md),
                     experiment_service.py (HTTP API for scientist agents: queued runs, summaries, live view, control, notes),
                     scientist_client.py (stdlib client + CLI for it; docs/SCIENTIST_API.md)
.claude/             agents/implementer.md, agents/tester.md, skills/phase (Manager Loop)
Analysis/            analyze_run.py
```

## Credits

- Environment dressing uses assets migrated from Epic Games' **Electric Dreams Environment** sample (Epic Games; Quixel Megascans; RealityScan), used under the Unreal Engine EULA / Epic Content Licence (Unreal Engine projects only). Those assets are **not in this repository**: install the sample from the Epic launcher and run `Tools/migrate_ed.py` with the role list in `AssetSources/ed_manifest.json`. A fresh clone without them runs procedural-only (the environment logs it and falls back), and `M_SW_TerrainED`, `M_SW_Sandstone` and `M_SW_Scan` render with default textures until the migration has run. Poly Haven scans (CC0) are likewise fetched, not committed (`Tools/fetch_polyhaven.py`, `Tools/import_assets.py`).
- The concept plates in `docs/plates/` come from this project's own specification (Appendix B) and are illustrations, not screenshots.
- CC0 scans from **Poly Haven** (polyhaven.com), fetched by `Tools/fetch_polyhaven.py`.
- Everything else (terrain, arches, creatures, materials, HUD) is generated by the code in this repo.

## License

Code, generated content and documentation in this repository: MIT (see `LICENSE`).
Third-party assets referenced above (Electric Dreams / Quixel Megascans under the
Unreal Engine EULA, Poly Haven under CC0) are not part of the repository and keep
their own licences.
