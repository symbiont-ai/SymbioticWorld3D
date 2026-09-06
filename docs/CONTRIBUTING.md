# Contributing to Symbiotic World

For people (and the coding agents they use) who want to change the sim without
owning a Windows machine with Unreal on it. Read `README.md` (build/run),
`DESIGN.md` (what each mechanism is and is not) and `CLAUDE.md` (hard rules)
first; this file tells you where things live and how a change gets verified.
`<repo>` below means the repository root.

## 1. What runs where, and the two lanes

| Machine | Runs | Owner |
|---|---|---|
| Windows host | Unreal Engine 5.7, the C++ build (`Tools/build.bat`), headless and windowed runs (`Tools/run_sim.py`), the live Pixel Streaming demo | the host maintainer |
| Macs (or anything with Python 3.9+) | policy agents (`Tools/policy_server.py`), the offline self-test and replay, analysis of CSV logs (`Analysis/analyze_run.py`) | you |

The host builds and runs C++ changes on collaborators' behalf: you write the
change, the host pulls, builds, runs the checks in §6 and reports the numbers.
Nobody builds on a Mac (the project is Windows-only today: `Tools/run_sim.py`
hard-codes the engine path and `Tools/build.bat` is a Windows batch file).

**Lane A: external agents through the policy API.** No Unreal, no build. Your
Python process decides actions for organisms in the running world; the sim
keeps sensing, gating, rewarding, reproducing and logging. Protocol:
`docs/POLICY_API.md`. Reference server: `Tools/policy_server.py`.

**Lane B: changes to the simulation in C++.** Anything under
`Source/SymbioticWorld`. The host builds it. Every such change ships with the
verification numbers from §6.

| I want to ... | lane | files |
|---|---|---|
| write a smarter or different controller for organisms | A | `Tools/policy_server.py` (`MyAgent.act/learn`), `docs/POLICY_API.md` |
| test my agent without the sim | A | `Tools/policy_client_check.py` (one live exchange), `Tools/policy_replay.py` on a `--record` file or `docs/samples/decide_sample.jsonl` (see `docs/POLICY_API.md`, "Offline development") |
| join a world that is already running | A | ask the host to add `your-ip:9000=Species` to `Saved/policy_servers.txt` (polled every 3 s; `Tools/policy_probe.py --write` finds you), see `docs/POLICY_API.md`, "Adding servers while the sim runs" |
| ask a new question of existing runs | A (Python) | `Analysis/analyze_run.py`, CSVs from the host |
| change a parameter or the look for one run | neither: `--set` | `Tools/run_sim.py --set "Settings.X=..;Lumen.Y=..;Look.Z=.."`, field names in `SWTypes.h` |
| sweep parameters | neither: `--set` | `Tools/sweep.py` |
| add an action, a percept field, a gene, a perturbation | B | `SWTypes.h`, `SWAgent.*`, `SWWorldManager.*`, `SWLogger.*`, `SWHUD.cpp`, `DESIGN.md` (§5 recipes) |
| replace the learner | B | `SWLearner.*`, `SWAgent.cpp`, `DESIGN.md` §1 |
| add a species | B (large) | see §5 (iii) before starting |
| change the valley's look | B, but usually `--set Look.*` first | `SWEnvironment.*`, `SWProcMesh.*`, `Tools/make_materials.py` |

## 2. Repo map

```
Source/SymbioticWorld/
  SWTypes.h/.cpp        the contract: ESWSpecies, ESWAction, ESWLearningMode, FSWGenome (+Mutated),
                        FSWSpeciesParams, FSWLookSettings, FSWRunSettings, FSWPercept, SWActionName()
  SWLearner.h/.cpp      FSWContextualBandit: Q[3][7], InitRandom, Select (eps-greedy over the mask), Update, Greedy
  SWAgent.h/.cpp        ASWAgent: Step, Decide, PrepareDecision, BuildFeasibleMask, ResolveDecision, ApplyAction
  SWWorldManager.h/.cpp ASWWorldManager: seeded Rng, StartRun, Tick -> StepWorld, BuildPercept, TryReproduce,
                        NeutralBirthStep, stats, ApplyCommandLineOverrides / ApplyParameterOverrides (-SWSet),
                        PolicyExchange, BuildHelloLine, BuildDecideLine
  SWResourcePatch.*     ASWResourcePatch: logistic regrowth, Take; type 0 = Resource A (Lumen), 1 = B (Tecton)
  SWTraceField.*        FSWTraceField: decaying grid (Trace X / Trace Y), Deposit, Sample, Gradient
  SWLogger.*            FSWRunLogger: agents/births/deaths/population CSV schemas
  SWPolicyClient.*      FSWPolicyClient: TCP client for external policy servers
  SWHUD.*               canvas HUD: title, stat cards, species panels, inspector, minimap, drought banner
  SWPlayerController.*  key bindings (names in Config/DefaultInput.ini)
  SWCameraPawn.*        observer camera, -SWCam
  SWGameMode.*          spawns manager + environment at runtime into the empty map
  SWEnvironment.*       the rendered valley; ApplyDrought; reads the trace fields for the ground overlay
  SWProcMesh.*          procedural terrain, arches, rocks, BuildLumen / BuildTecton bodies, glow clusters
Tools/
  build.bat             the only allowed build entry point (refuses while any UnrealEditor process runs)
  run_sim.py            launcher: headless / windowed / offscreen, --set, --shot, --policy, --policy-file, --stream
  sweep.py              one headless run per (override x mode x seed), summary table + CSV in Saved/
  policy_server.py      reference policy server (stdlib only): RandomAgent, BanditAgent, HeuristicAgent,
                        TraceFollowerAgent, MyAgent stub; --record writes every exchange as JSON lines
  policy_client_check.py  pretends to be the sim for one exchange against your server
  policy_replay.py      replays a --record file through an agent class offline, no sim
                        (docs/POLICY_API.md, "Offline development")
  policy_probe.py       scans a /24 for policy servers (real hello + decide exchange per open port) and prints /
                        appends "host:port=Both" lines for the server list file (--write Saved/policy_servers.txt)
  policy_servers.example.txt   template for Saved/policy_servers.txt, the file the running sim polls every 3 s
                        to add / change / remove servers without a restart (docs/POLICY_API.md)
  make_materials.py, make_valley_map.py, migrate_ed.py, import_assets.py, fetch_polyhaven.py,
  fix_foliage_materials.py, registry_dump.py   content generation / asset import (host only)
  start_stream_server.bat   Pixel Streaming signalling server
Analysis/
  analyze_run.py        lifetime learning, inheritance, per-generation means, C vs N Welch test, summary PNG
docs/
  POLICY_API.md         the policy protocol (hello / decide / actions / log, fallback rules)
  CONTRIBUTING.md       this file
  samples/decide_sample.jsonl   a recorded decide/actions trace for policy_replay.py
  SPEC_TEXT.txt, plates/   the hack-day spec and concept plates
  VISUAL_PLAN.md, ED_ASSET_REVIEW.md, ASSET_DOWNLOADS.md   visual track notes
Content/
  Maps/Valley.umap      the empty startup level (everything is spawned at runtime)
  Materials/M_SW_*.uasset   generated by Tools/make_materials.py (committed)
  Megascans/, Assets/, Meshes/, ...   licensed or fetched assets: gitignored, never committed
Config/                 DefaultEngine.ini (renderer), DefaultInput.ini (keys), DefaultGame.ini
CHECKLIST.md, PROGRESS.md   phases with exit conditions; one line per verified change
```

## 3. The sim loop

1. `ASWWorldManager::Tick` accumulates `DeltaSeconds * TimeScale` and runs
   `StepWorld(Dt)` in fixed `Settings.LogicalSubstep` (0.1 s) steps; speed only
   changes how many substeps run per frame.
2. `StepWorld`: trace fields decay (`FSWTraceField::Decay`), patches regrow
   (`ASWResourcePatch::Step`, regen x drought multiplier x `1 + TraceYRegenGain * TraceY`).
3. Each agent runs `ASWAgent::Step(Dt)`: age, `ApplyAction(Dt)` (movement, foraging, energy burn), death checks.
4. Every `Settings.DecisionInterval` (1 s) `ASWAgent::Decide()` first credits the previous action:
   `r = WeightEnergy * dEnergy / RewardScale + WeightNovelty * novelty + WeightInteraction * interaction`,
   then `FSWContextualBandit::Update(context, action, r, EffectiveAlpha())` (alpha = 0 in mode A).
5. `PrepareDecision()`: `ASWWorldManager::BuildPercept` fills `FSWPercept`; context = `FSWPercept::EnergyBin()`
   (energy thirds); `BuildFeasibleMask()` removes impossible actions (bit per `ESWAction`).
6. `ResolveDecision()`: `FSWContextualBandit::Select` (epsilon-greedy over the feasible set) or the external
   server's action if one arrived (`PolicyExchange`, after the agent loop); `Modify` deposits Trace X/Y here.
7. Signals: every signalling Lumen broadcasts its known patch to same-species neighbours (`ReceiveSignal`).
8. Reproduction: `Step` calls `ASWWorldManager::TryReproduce` when `age >= MinReproAge` and
   `energy >= ReproThreshold`; child genome = `MakeChildGenome` = `FSWGenome::Mutated(Rng, MutationSigma)`
   (exact copy in mode B); children are admitted from `PendingSpawns` at the end of the substep.
9. Mode N: `NeutralBirthStep` picks a uniformly random parent while below target; starvation is disabled in `Step`.
10. `RecomputeStats` every 0.5 s; `LogTick` writes agents rows every `AgentLogInterval` and population rows every 5 s.
11. Randomness: one `FRandomStream Rng` in `ASWWorldManager`, seeded in `StartRun()` from `Settings.Seed`.
    Every sim draw goes through `Manager->GetRng()`: founder/child genomes, Q init, explore headings,
    signal acceptance, patch placement, neutral parents, policy share. Never `FMath::RandRange` in sim code.
12. Visuals use their own streams seeded from `Look.LookSeed` (`ASWAgent::BuildBody`, `ASWResourcePatch::Init`,
    `ASWEnvironment`), so the look never perturbs the simulation.
13. Iteration order is deterministic (arrays, `RemoveAtSwap`), so same seed + mode => byte-identical CSVs.
14. External policies do not touch `Rng`; a fallback (timeout, infeasible reply) does, so runs with `--policy`
    are reproducible only if the server is.
15. `-SWDuration` ends the run exactly at that logical time (`Tick` checks before each substep).

## 4. Guardrails reviewers enforce

- Lifetime learning is a **tabular contextual bandit** (gamma = 0). Not "Q-learning", not "RL". Evolution is
  asexual **Gaussian mutation** of `{alpha, epsilon, social, e}`. Use these words in code, HUD, logs and docs.
- Never write "intelligence", "emergent" or "cooperation" into user-facing strings (HUD, CSV, log lines, hello).
  There is no Lumen-Tecton coupling in the code beyond shared space; do not add one and call it cooperation.
- All sim logic is C++ under `Source/SymbioticWorld`. No Blueprint logic, no UMG.
- The contract is `SWTypes.h` + `DESIGN.md`. Consume it; never redefine an enum, a mode or the reward.
  Any change to `SWTypes.h` that alters behaviour lands with the matching `DESIGN.md` edit in the same PR.
- All randomness through the manager's seeded `FRandomStream` (§3.11). Fixed substep.
- The no-policy path stays **byte-identical**: same seed + mode twice must give identical CSVs (after stripping
  `run_id`). If your change legitimately alters the draw sequence (e.g. one more Gaussian per birth), say so in
  the PR and add the reproducibility note to `DESIGN.md` §5, as was done for `e`.
- Every change is verified with numbers: `Tools/run_sim.py` + `Analysis/analyze_run.py` output in the PR,
  plus a screenshot for anything visible. Adjectives are not evidence.
- Every shown run states seed and mode; learning claims come with the mode A control, evolution claims with mode N.
- No licensed content in commits: Electric Dreams / Megascans / Poly Haven assets are gitignored. Only
  `Content/Maps` and generated `Content/Materials/M_SW_*` are committed.

## 5. Recipes

Each step names `file:function`. "Verify" is what goes into the PR.

### (i) Add an action

1. `SWTypes.h: ESWAction`: add the value before `COUNT` (`SW_NUM_ACTIONS` follows). Add its lowercase name to
   `SWActionName()`. That one name feeds the CSV headers (`SWLogger.cpp: FSWRunLogger::Open` builds
   `Q_<bin>_<action>`), the HUD species bars and inspector rows (`SWHUD.cpp: DrawSpeciesPanel`,
   `DrawInspector` loop over `SW_NUM_ACTIONS`) and the policy hello (`SWWorldManager.cpp: BuildHelloLine`).
   Also automatic: name-to-index parsing of server replies (`SWPolicyClient.cpp: FSWPolicyClient::ParseAction`
   compares against `SWActionName`), the `mask` and `q` arrays in `BuildDecideLine`, `FSWSpeciesStats::ActionCounts`
   (`SWWorldManager.h`), and every loop in `SWLearner.cpp`. The only C++ file with the number written out is the
   header comment of `SWLearner.h` ("7, of which a subset is feasible"); fix it by hand. `grep -rn "SW_NUM_ACTIONS"
   Source/SymbioticWorld` lists every place that iterates actions; none of them needs an edit.
2. `SWAgent.cpp: ASWAgent::BuildFeasibleMask()`: set the bit only when the action is possible now.
3. `SWAgent.cpp: ASWAgent::ApplyAction(float Dt)`: the per-substep effect and its energy burn. The `switch` has a
   `default: break;`, so a new value compiles and silently does nothing (basal burn only) until you add its `case`.
   A once-per-decision effect (like Modify's deposit) goes in `ResolveDecision()` right after the action is chosen.
4. Reward: the default is pure energy change (`Decide()`). If the action's payoff is delayed, a gamma = 0 bandit
   cannot credit it; either accept that or add an immediate term via `InteractionThisInterval` with a
   `FSWRunSettings` weight, and document it as a bias in `DESIGN.md` §4 like `WeightInteraction`.
5. Q-table: `FSWContextualBandit::Q[SW_NUM_ENERGY_BINS][SW_NUM_ACTIONS]` and `Visits` resize automatically; the mask
   is a `uint32`, so up to 32 actions.
6. CSV: `agents.csv` gains `Q_low_<name>`, `Q_mid_<name>`, `Q_high_<name>` automatically, and the column positions
   shift. Python side, every place that knows the count (grep for `ACTIONS`, `range(7)`, `[:7]`, `0..6`, `< 7`):
   - `Analysis/analyze_run.py`: the `ACTIONS` list, **and** `lifetime_learning()`, which slices `first[:7]` /
     `last[:7]` (the LOW-bin block) for `greedy_changed`; change both slices to `len(ACTIONS)`.
   - `Tools/policy_server.py`: `ACTIONS` (its `on_hello` asserts the list equals the hello's, so an old server
     refuses a new sim), and the docstrings that say `[7 ints]`, `[3][7]`, `index 0..6`.
   - `Tools/policy_client_check.py`: `HELLO["actions"]`, the hand-written 7-entry `mask` and `range(7)` for `q` in
     `fake_agent()`, and the `0 <= int(act) < 7` check in `main()`.
   - `Tools/policy_replay.py` derives everything from `ACTIONS` (only the "index 0..6" message text is literal).
   - `docs/samples/decide_sample.jsonl` was recorded with 7-entry masks and `[3][7]` tables. Replaying it against an
     agent that returns the new action raises `IndexError` in the mask check, so re-record the sample after the
     change (`docs/POLICY_API.md`, "Record") and say so in the PR; agents that never return the new action still
     replay against the old file.
7. Docs and HUD: `docs/POLICY_API.md` (mask length "7 ints", `q` shape `[3][7]`, indices `0..6`, the `hello`
   example), `DESIGN.md` §1 action set and feasibility rules, `README.md` ("3×7 Q table"). `SWHUD.cpp` has two fixed
   panel heights that do not grow with `SW_NUM_ACTIONS`: `DrawSpeciesPanel` (`H = 232.f`, 14 px per action row)
   and `DrawInspector` (`403.f`, one row per action); add 14 px to each or the last row draws outside the panel.
   Check a screenshot.
8. Verify: build; `run_sim.py --mode A B --seed 42 --duration 300 --analyze` (A drift still 0.000, B > 0);
   `agents.csv` header contains the three new columns; action share in `current_action` > 0 for the new action in
   some bin; §6 determinism pair; `policy_client_check.py` against `policy_server.py` still exchanges; a fresh
   `--record` file replays with `policy_replay.py --agent random` and exit code 0.

### (ii) Add a percept field

1. `SWTypes.h: FSWPercept`: add the field with a default.
2. `SWWorldManager.cpp: ASWWorldManager::BuildPercept()`: fill it. It is `const` and must draw nothing from `Rng`
   (`PrepareDecision` is documented as "No randomness").
3. Consumers: gate an action in `ASWAgent::BuildFeasibleMask()`, steer movement in `ApplyAction()`.
4. If it should be part of the bandit's **state**: the context is `FSWPercept::EnergyBin()` and
   `SW_NUM_ENERGY_BINS` (`SWTypes.h`). Changing the context changes the Q shape, `BinNames` in
   `SWLogger.cpp`, the `BinName()` helper and `bins` in `BuildHelloLine` (both `SWWorldManager.cpp`), the
   LOW/MID/HIGH ternaries in `SWHUD.cpp: DrawInspector`, `BINS` in `analyze_run.py` and `policy_server.py`, the
   `bin_index` arithmetic in `policy_client_check.py: fake_agent()`, and `DESIGN.md` §1. That is a contract
   change: agree it in an issue first.
5. Policy JSON: `SWWorldManager.cpp: BuildDecideLine()` writes every percept key by hand in the `"percept":{...}`
   block; add yours (use `null` when there is nothing, like `resource_loc`). Then `PERCEPT_FIELDS` in
   `Tools/policy_server.py`, `fake_agent()` in `Tools/policy_client_check.py`, and the field table in
   `docs/POLICY_API.md`.
6. Optional: log it (`FSWRunLogger::LogAgent` row + the header in `Open`), show it in the inspector.
7. Verify: a field that only feeds the JSON/CSV must leave the §6 determinism pair byte-identical (before and after
   your change, same seed). A field that feeds the mask changes behaviour: report the population/analysis numbers
   before and after and say why the change is wanted.

### (iii) Add a species or agent type

Honest note first: `ESWSpecies` has two values and much of the code assumes exactly two:
`SWSpeciesName()` is a ternary; the manager holds `LumenParams`/`TectonParams`, `LumenStats`/`TectonStats`,
`Settings.InitialLumen`/`InitialTecton`, `ExtDecisions[2]`; `ApplyParameterOverrides` knows the scopes
`lumen`/`tecton`; `ASWAgent::BuildBody` picks `SWProc::BuildLumen` or `BuildTecton`; the HUD species panels,
evolution strip and minimap are written per species; `FSWPolicyServer::bLumen/bTecton` and `Configure` parse
`Lumen|Tecton|Both`; `analyze_run.py` and `sweep.py` loop over `("Lumen", "Tecton")`; resources are two types
(`ResourcePatchesA/B`, `ResourceTotalA/B`, `PreferredResourceType` 0/1); `Signal` is Lumen-only and `Modify`
splits by species in `BuildFeasibleMask` and `ResolveDecision`. Expect about a dozen files and a day. If what you
want is a *variant* (a slower Lumen, a hungrier Tecton), do it with `--set "Lumen.MoveSpeed=200"` or by adding a
field to `FSWSpeciesParams` instead. If you still want a third species, in this order:

1. `SWTypes.h`: `ESWSpecies` value, `SWSpeciesName()`, and (if it eats something new) a third resource type.
2. `SWWorldManager.h/.cpp`: params defaults in the constructor, `Settings.Initial<Name>`, `SpawnFounders`,
   `MakeFounderGenome` (unchanged), stats slot + `RecomputeStats`, `GetStats`, `ExtDecisions/ExtFallbacks`
   size, a `<name>` scope in `ApplyParameterOverrides`, `NeutralBirthStep` target, `LogTick` population rows.
3. `SWResourcePatch.*` / `SpawnPatches` / `BuildPercept` if a new resource type exists (`GetResourceTotal(Type)`).
4. `SWAgent.cpp`: species branches in `BuildFeasibleMask`, `ResolveDecision` (which trace it writes), `BuildBody`.
5. `SWProcMesh.*`: a `Build<Name>` body; colours in `FSWLookSettings`.
6. `SWHUD.cpp`: `DrawSpeciesPanel` call and layout, `DrawEvolutionStrip` bars, minimap marker.
7. `SWLogger.cpp`: nothing structural (species is a string column), but the population CSV gets a third row per
   sample; `analyze_run.py` species loops; `sweep.py` `(("Lumen","L"),("Tecton","T"))`.
8. `SWPolicyClient.cpp: Configure` species parsing; `BuildHelloLine` `species` and `controls`; `POLICY_API.md`.
9. `DESIGN.md` §4 (what it eats, what it modifies) and `README.md`.
10. Verify: 600 s mode C on seeds 1-3 with no extinction; determinism pair; HUD screenshot with three panels;
    mode A / B drift numbers unchanged for Lumen and Tecton.

### (iv) Replace or add a learner

1. Interface in `SWLearner.h: FSWContextualBandit`: `InitRandom(Rng, QInitMax)`, `Select(Context, FeasibleMask,
   Epsilon, Rng, bOutExplored)`, `Update(Context, Action, Reward, Alpha)`, `Value(Context, Action)`,
   `Greedy(Context, Mask)`. Callers: `ASWAgent::Init` (init + copy to `InitialBandit`), `ASWAgent::Decide`
   (update with `EffectiveAlpha()`), `ASWAgent::ResolveDecision` (select). The HUD inspector and the logger read
   `Value()` for the initial-vs-now table and the `Q_*` columns; the policy JSON sends the table as `q`.
2. Keep the mode semantics: **A** `ASWAgent::EffectiveAlpha()` returns 0, so the random initial values are the
   policy for life; **B** `MakeFounderGenome`/`MakeChildGenome` copy the genome exactly (no variation); **C** genome
   inherited with mutation, energy-gated births, starvation; **N** `NeutralBirthStep` random parent, no starvation.
   A new learner must still produce "A drift 0.000, B drift > 0" in `analyze_run.py`, or say why not.
3. Learner parameters that should evolve must live in `FSWGenome` (`SWTypes.h`), be clamped in `FSWGenome::Clamp`
   and mutated in `FSWGenome::Mutated` (`SWTypes.cpp`, one Gaussian per gene, sigma = `Settings.MutationSigma`).
   Adding a gene also means: `FSWSpeciesStats` + `ComputeSpeciesStats`, `LogAgent`/`LogBirth`/`LogDeath`/
   `LogPopulation` rows and headers, `DrawInspector` + `DrawEvolutionStrip` + the `inherited:` line in
   `DrawSpeciesPanel`, the `genome` block in
   `BuildDecideLine`, the parameter list in `analyze_run.py: inheritance()`, `DESIGN.md` §2 and §5 (draw count).
4. All draws through the `FRandomStream&` passed in. No statics, no wall clock.
5. Terminology: if you add bootstrapping from the next state (gamma > 0) it *is* Q-learning, and every string
   must change with it: the `SWLearner.h` header comment, the `"learning"` text in `BuildHelloLine`, `DESIGN.md`
   §1, README. Do not keep the bandit wording for something that is not one.
6. Verify: `run_sim.py --mode A B --seed 42 --duration 300 --analyze`; births.csv inheritance corr > 0.7 and
   mean |delta| near sigma; `--mode C N --seed 1 2 3 --duration 900 --analyze` and report the Welch p as printed.

### (v) Add a perturbation (drought is the template)

How drought works today: `ASWWorldManager::ToggleDrought()` flips `bDrought` (reset in `StartRun`, key `P` via
`SWPlayerController.cpp: BindAction("ToggleDrought")` and `Config/DefaultInput.ini`); `StepWorld` step 1 passes
`Settings.DroughtRegenMultiplier` / `DroughtCapacityMultiplier` to `ASWResourcePatch::Step`; the environment
lerps `DroughtFactor` toward `Manager->IsDrought()` over `Look.DroughtBlendSeconds` in `ASWEnvironment::Tick` and
applies it in `ASWEnvironment::ApplyDrought` (sun, fog, water drop, waterfalls, terrain dryness);
`ASWHUD::DrawHUD` draws the "PERTURBATION ACTIVE:  DROUGHT" banner; `drought_state` is a column in `agents.csv`
and `population.csv`, and `analyze_run.py: plot_run` shades it. There is no command-line onset yet, so a headless
drought test needs step 2.

1. Settings: put every number in `FSWRunSettings` (`SWTypes.h`) so `-SWSet` reaches it without a rebuild.
2. Manager: a flag + `Toggle<Name>()` / `Is<Name>()` in `SWWorldManager.h`, reset in `StartRun`, one `UE_LOG` per
   state change. For headless tests add `-SW<Name>At=t0:t1` in `ApplyCommandLineOverrides` (parse like `SWShot`,
   `':'` separated) and switch it in `Tick` on `SimTime`.
3. Effect: in `StepWorld` (resources, fields) or `BuildPercept` (what agents sense). Nothing in `Tick` itself.
4. Key: `SWPlayerController` action + `Config/DefaultInput.ini` mapping; add it to the `DrawHelp` line in `SWHUD.cpp`.
5. Visual: `ASWEnvironment::ApplyDrought`-style blend driven from the manager flag; keep a `Look.b<Name>Preview`.
6. HUD banner in `ASWHUD::DrawHUD`; CSV column in `FSWRunLogger::LogAgent` and `LogPopulation` (+ headers) and
   the corresponding read in `analyze_run.py`; `DESIGN.md` §4.
7. Verify: two runs, same seed, with and without the perturbation; report population min/final per species, the
   `current_action` distribution before vs after onset (from `agents.csv`), and a screenshot pair (`--offscreen
   --shot`). This is CHECKLIST item 7's exit condition.

### (vi) Add an analysis

1. Structure of `Analysis/analyze_run.py`: `load_run(run_dir)` returns a dict with DataFrames `agents`, `births`,
   `deaths`, `population` plus `mode`, `seed`; `lifetime_learning(agents)`, `inheritance(births)`,
   `per_generation(births, species)`, `population_trajectory(pop, species)`; `report_run(run, plots, out_dir)`
   prints and calls `plot_run`; `compare(results)` does the C vs N Welch test; `main()` takes run dirs or `--root`,
   `--out`, `--no-plots`.
2. CSV schemas (`SWLogger.cpp: FSWRunLogger::Open`):
   - `agents.csv`: `run_id,seed,mode,sim_time,generation,drought_state,agent_id,parent_id,species,age,energy,alpha,
     epsilon,social,env_effect,energy_bin,current_action,explored,reward,decisions,trace_x,trace_y,Q_<bin>_<action>
     x21,births,deaths,pop_lumen,pop_tecton,resource_A,resource_B,policy` (one row per living agent per
     `AgentLogInterval`).
   - `births.csv`: `run_id,sim_time,parent_id,child_id,species,child_generation,child_{alpha,epsilon,social,env_effect},
     parent_{alpha,epsilon,social,env_effect},parent_age,parent_energy`.
   - `deaths.csv`: `run_id,sim_time,agent_id,species,generation,age,energy,cause,alpha,epsilon,social,env_effect,decisions`
     (`cause` = `starvation` | `age`).
   - `population.csv`: `run_id,seed,mode,sim_time,species,n,mean_alpha,sd_alpha,mean_epsilon,sd_epsilon,mean_social,
     sd_social,mean_env_effect,sd_env_effect,mean_generation,max_generation,births,deaths,resource_A,resource_B,
     drought_state,trace_X_mean,trace_Y_mean,ext_decisions,ext_fallbacks` (one row per species every 5 s).
3. Write one function that takes a DataFrame and returns numbers; call it from `report_run`; print numbers, not
   verdicts. Tolerate missing columns from older runs (see `q_columns`) and keep plotting inside the existing
   try/except so a plot failure never kills the report.
4. Dependencies: pandas, numpy, matplotlib, scipy (`pip install pandas numpy matplotlib scipy` on the Mac).
   `Saved/` is gitignored; ask the host for a zip of `Saved/SymbioticWorld/<run_id>/` or of the whole root.
5. Verify: run it on one mode A and one mode B run and paste the lines; the existing lifetime-learning line must
   still read ~0.000 for A.

## 6. The host loop for any C++ change

1. `tasklist | findstr UnrealEditor` — if anything is listed, wait. `Tools/build.bat` refuses to build while any
   UnrealEditor / UnrealEditor-Cmd process exists (the DLL is locked, and relinking under a live session crashed
   the stream once). Never call `Build.bat` directly. Paste the `Result:` line.
2. Headless run: `python Tools/run_sim.py --mode C --seed 1 --duration 600` writes
   `<repo>/Saved/SymbioticWorld/<run_id>/{agents,births,deaths,population}.csv` and exits on its own
   (a 60 s run takes about 10 s of wall time; headless runs are fine while a windowed demo is up).
3. Determinism: `python Tools/run_sim.py --mode C --seed 7 7 --duration 120`, then compare the two newest run dirs
   with the first column (`run_id`, timestamped) stripped:
   ```python
   import pathlib
   a, b = sorted(pathlib.Path("Saved/SymbioticWorld").glob("*_seed7_C_*"))[-2:]
   strip = lambda p: [l.split(",", 1)[1] for l in p.read_text().splitlines()]
   for f in ("agents", "births", "deaths", "population"):
       print(f, "identical" if strip(a / f"{f}.csv") == strip(b / f"{f}.csv") else "DIFFER")
   ```
   All four must print `identical`. The snippet takes the two **newest** matching directories, so run it right
   after the pair; a `--policy` run or a different `--duration` with the same seed in between prints `DIFFER` for
   a reason that has nothing to do with your change. If the change intentionally alters the draw sequence, run the
   pair anyway (it proves the new build is self-consistent) and note it in `DESIGN.md` §5.
4. Analyze: `python Analysis/analyze_run.py <dir1> <dir2>` (or `--root Saved/SymbioticWorld`); paste the lines
   the change is about (lifetime learning, inheritance, per generation, Welch).
5. Screenshot for anything visible:
   `python Tools/run_sim.py --mode C --seed 1 --duration 45 --speed 1 --windowed --offscreen --shot 4,40 --auto-select --no-logs`
   -> `Saved/Screenshots/WindowsEditor/SW_<run_id>_t0004.png` and `_t0040.png`. `--offscreen` keeps stray
   keystrokes (M, P, 1-3) out of the run. Open the PNG and describe what is actually there.
6. `PROGRESS.md`, one line:
   `- YYYY-MM-DD <item> — PASS|PARTIAL (<how verified>) — <key numbers> — files: <list>`.

## 7. Submitting

- Branch from `main`: `policy/<topic>`, `sim/<topic>`, `analysis/<topic>`, `docs/<topic>`, `look/<topic>`.
- One recipe per PR. A PR that adds an action *and* a gene is two PRs.
- PR description: what and why (two sentences); files touched; **verification numbers**: the `Result:` line,
  the exact `run_sim.py` command(s), the `analyze_run.py` lines, the determinism result, the screenshot file
  name; contract changes (`SWTypes.h` / `DESIGN.md` / `POLICY_API.md`) or "none"; "not verified" for anything
  you could not run. A Mac-side PR states which steps of §6 the host still has to run.
- The host pulls the branch, runs §6, pastes the numbers into the PR and merges. Do not commit `Saved/`,
  screenshots, or anything under the gitignored `Content/` folders.
- Everything Python-side works on a Mac without the host: policy agents (`Tools/policy_server.py` +
  `Tools/policy_client_check.py`), replay of recorded decisions (`Tools/policy_replay.py --agent my --file
  docs/samples/decide_sample.jsonl`, or a file from `policy_server.py --record`; described in
  `docs/POLICY_API.md` under "Offline development"), and analysis of CSVs the host shares.

## 8. For AI coding agents (Claude Code, Cursor, ...)

- Read `CLAUDE.md` and this file before touching `Source/`. The hard rules there override anything else.
- Never build on a Mac; never build on the host while any UnrealEditor process exists; never call `Build.bat`
  directly (`Tools/build.bat` only). Never kill or launch an editor process that you did not start.
- Never edit `SWTypes.h` without the matching `DESIGN.md` edit in the same change; never rename a mode or
  redefine the reward. Terminology per §4.
- Run the Python checks you can: the syntax gate for the stdlib tools is
  `python3 -c "import ast,sys; [ast.parse(open(f).read(), feature_version=(3,9)) for f in sys.argv[1:]]" Tools/policy_server.py Tools/policy_replay.py Tools/policy_client_check.py`,
  then `python3 Tools/policy_replay.py --agent my --file docs/samples/decide_sample.jsonl` (exit 0),
  `Tools/policy_client_check.py` against a running `Tools/policy_server.py`, and `Analysis/analyze_run.py` on any
  run dir you have. Headless runs via `Tools/run_sim.py` are allowed on the host and do not need a build.
- Report exactly what you verified and what you could not (build, determinism pair, screenshot). A claim without
  numbers is treated as unverified.
