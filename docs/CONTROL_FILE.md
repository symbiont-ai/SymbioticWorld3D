# Live control file

Perturb a running world without a restart and without touching its keyboard: append one
command per line to a text file, the sim executes each new line once. Meant for a scientist
(human or agent) driving experiments against a world that is also being watched or streamed.
Code: `ASWWorldManager::InitControlFile / PollControlFile / ExecuteControlCommand / RunControlCommand`
in `Source/SymbioticWorld/SWWorldManager.cpp`; helper: `Tools/control.py`.

## Path and polling

* **Path:** `Settings.ControlFile`, default `Saved/control.txt`, relative to the project directory
  (`<repo>/`). Change it with `-SWControlFile=<path>`, `run_sim.py --control-file <path>` or
  `--set "Settings.ControlFile=<path>"`; an empty value turns the watch off. The path is resolved once
  at startup (a later `set Settings.ControlFile=...` has no effect until the process restarts).
* **Poll:** every `Settings.ControlFilePollSec` seconds (default 2, minimum 0.25) of **wall** time,
  from the manager's per-frame `Tick`, before the fixed-step loop, never inside a logical substep and
  never touching the seeded stream. Polling continues while paused, so `pause=off` works. The file is
  re-read only when its size or modification time changed; with no file, or no new lines, the poll is
  a pure stat and nothing in the sim changes.
* **Startup:** the sim records the file's current line count and ignores those lines. Log line:
  `control file: <path>, N existing lines ignored, watching (polled every 2.0 s)` (or
  `... not present, watching ...`). So a file left over from yesterday does not replay.

## Semantics: an append-only command log

* Every newline-terminated line beyond the last executed one is executed **once**, in file order,
  then the cursor moves past it. A line without a trailing newline (still being written) waits for the
  next poll. `Tools/control.py` always writes complete lines and repairs a missing final newline first.
* Blank lines and lines starting with `#` are skipped. An inline ` # comment` (space, then `#`) is
  dropped from every command except `note=`, whose text is taken verbatim.
* If the file **shrinks** (someone truncated or rewrote it) the cursor is reset to the new line count and
  a log line says so; the remaining lines are treated as already seen, not re-executed. If the file is
  removed the cursor resets to 0, so a recreated file executes all of its lines. A rewrite that keeps the
  line count but changes an already-executed line is treated as new content: the cursor resets to 0 and every
  line of the new file executes once (so truncate-and-replace inside one poll window is not lost).
* Every executed line is logged as `control: <line> -> <result>` (UE log, `LogSymbioticWorld`) and
  appended to `<run dir>/commands.csv` (below). Unknown or malformed lines are logged and recorded as
  `rejected: ...`; they never stop the sim or the remaining lines.
* The run survives everything except `reset` / `mode`; the watch survives resets too (cursor and path
  are per process, not per run).

## Grammar

Keywords are case-insensitive; the line is trimmed; spaces around `=` are tolerated.

| command | effect | same code path as |
|---|---|---|
| `drought=on` / `drought=off` / `drought=toggle` | drought perturbation (`Settings.DroughtRegenMultiplier` / `DroughtCapacityMultiplier` on every patch, HUD banner, drought visuals). `on`/`off` are idempotent: `ok: drought already on` | key **P**: `ASWWorldManager::ToggleDrought()` |
| `speed=<float>` | time scale, clamped to `[0, 1000]` like the keys; `0` stops the logical clock without setting the pause flag | keys **1/2/3**: `SetTimeScale()` |
| `pause=on` / `pause=off` | pause flag, idempotent | key **Space**: `TogglePause()` |
| `set <Scope.Field>=<value>` | the `-SWSet` reflection on the **live** objects: scopes `Settings`, `Lumen`, `Tecton`, `Genome` (alias `Founder`), `Look`; colours as `r:g:b`; several items may be joined with `;` (`set Settings.MaxPopulation=300;Lumen.MaxAge=200`). Result `ok: k/n field(s) set`; a field that does not exist is a `SWSet: no property ...` warning and counts as not set | launch flag `-SWSet`: `ApplyParameterOverrides()` |
| `reset` | restart the run with the same seed and mode (new run id, new log directory) | key **R**: `ResetRun()` |
| `reset seed=<int>` | set `Settings.Seed`, then restart | `ResetRun()` |
| `mode=A|B|C|N` | switch the learning mode and restart | key **M** ends in the same `SetMode() -> ResetRun()` |
| `note=<text>` | recorded verbatim (result `ok: note recorded (n chars)`), kept as `ASWWorldManager::GetLatestNote()` for a later HUD panel; use it to mark experiment steps in `commands.csv` | none (bookkeeping) |
| `caption=<text>` | scenario title for a recorded take: draws the text large and centred in the lower third for `Look.CaptionSeconds` (default 6), fading out over the last 0.5 s; `caption=` alone clears it at once. The text is taken verbatim like `note=`. Held on the **wall** clock, so a paused take keeps its title, and drawn even when `Look.bShowHUD` is false | none (HUD layer: `ASWHUD::DrawCaption`) |
| `at=<sim_time> <command>` | queue any other command for an exact logical time (`at=120 drought=on`, `at=45.5 follow=Leviathan`). Result `ok: queued for t=120.00 (3 pending): drought=on`; the queued line runs later and logs again then | `RunDueScheduledCommands()` -> `ExecuteControlCommand()` |
| `cam=x,y,z,pitch,yaw` | place the observer camera at that world position and rotation, releasing any follow (5 finite numbers, commas) | launch flag `-SWCam`: `ASWCameraPawn::SetPose()` |
| `follow=<target>` | chase camera; target is `Lumen` / `Tecton` (the selected organism of that species, selecting one if none is), `Leviathan` (the river predator), a scientist name or `any` (a field-team avatar, once it joins), or `none` to release | launch flags `-SWFollowSpecies` / `-SWFollowScientist`: `ASWCameraPawn::SetFollowSpec()` |

### `at=`: scripted takes

`at=` is how a demo take is preregistered instead of typed: append the whole shot list once, the sim fires
each line when its **logical** clock reaches the time (the wall clock and the frame rate do not matter, so
the same seed + shot list gives the same rows twice). Details:

* Time is logical seconds, finite and `>= 0`; a time already past fires on the next frame. Rejected:
  a non-numeric or negative time, no command after the time, and a nested `at=` (no recursion).
* Due commands run from `Tick`, before the fixed-step loop, never inside a substep — the same placement as
  the file poll. All commands due in one frame fire in that frame, in ascending scheduled time (equal times
  keep the order they were queued in). Each one goes through `ExecuteControlCommand`, so it logs
  `control: <line> -> <result>` and gets its own `commands.csv` row, exactly like a typed line; the log line
  before it reads `control: scheduled t=120.00 fires at t=120.00 (3 still pending)`.
* The queued text is stored verbatim (its own rules apply when it runs, so a scheduled `note=` keeps its
  `#` characters), and a malformed queued command is only rejected when it fires.
* `reset`, `reset seed=`, `mode=` **clear the pending list** (`control: N scheduled command(s) dropped`):
  a new run restarts the logical clock and an old shot must never re-fire.
* The shot list must be **appended while the sim runs** (`Tools/control.py`, or any append): lines that
  already exist when the sim starts are ignored, like every control-file line.

`cam=`, `follow=`, `caption=` and `set Look.bShowHUD=0` are camera/HUD only: they change nothing in the
world, draw nothing from the seeded stream and leave a run byte-identical to one without them. They need a
rendering run — in a headless `-nullrhi` run there is no observer camera and `cam=` / `follow=` are rejected,
and nothing draws a caption. A beat is usually two scheduled lines, the title and the event:

```
at=298 caption=Drought: the river drops
at=300 drought=on
```

Do not use `set Settings.Mode=...` or `set Settings.Seed=...` to change mode or seed: the first changes
the learning rules mid-run without a reset (and the run id keeps the old mode name), the second only
applies at the next reset. `mode=` and `reset seed=` do the right thing.

## Which settings are live and which wait for a reset

`set` writes the manager's live structs. Whether the world reacts at once depends on where the field
is **read**; this list was taken from the code (2026-09-06).

**`Settings.*` read at use, so effective at once (next substep / next event):**
`MaxPopulation`, `DecisionInterval`, `LogicalSubstep`, `MaxSubstepsPerFrame`, `MutationSigma` (next
birth), `RewardScale`, `WeightEnergy`, `WeightNovelty`, `WeightInteraction`, `DroughtRegenMultiplier`,
`DroughtCapacityMultiplier`, `NeutralBirthInterval`, `AgentLogInterval`, `bTraceFields`,
`TraceXDeposit`, `TraceYDeposit`, `TraceYRegenGain`, `TraceXFollowMin`, `ModifyBurn`, `QInitMax`
(organisms born after the command), `PolicyShare` (births after the command), `PolicyFilePollSec`,
`ControlFilePollSec`, and the predator's behaviour while it runs: `LeviathanSpeed`, `LeviathanChaseSpeed`,
`LeviathanSenseRadius`, `LeviathanStrikeRadius`, `LeviathanStrikeCooldown`, `LeviathanTarget`,
`LeviathanTurnInterval`, `LeviathanTurnChance`, `LeviathanLoiterChance`, `bLeviathanPauseInDrought`
(`bLeviathan` and `LeviathanCount` spawn the animals at `StartRun`: `set` then `reset`). `TraceMax`: the
reward scaling and the minimap use the new value at once, the fields' clamp keeps the old one until the next reset.
Added 2026-09-18: `MaxLumen`, `MaxTecton` (next birth), and the bank cycle's `bBankCycle`, `BankCyclePeriod`,
`BankCycleWarmup`, `BankCycleScope`, `BankCycleOffCapacity`, `BankCycleOnRegen`, `BankCycleRamp`,
`bBankCycleHardOff`, `bBankCyclePauseInDrought` (read every substep; the phase is advanced step by step, so a new
period or warm-up only lengthens or shortens the current phase, and a `LogicalSubstep` change rescales it).
`BankCycleStartBank` is latched when the cycle starts: set it and `bBankCycle=0`, let the sim run unpaused for at
least one substep, send `bBankCycle=1` in a later poll (more than `ControlFilePollSec` later) - this restarts the
warm-up - or `reset`.

**`Settings.*` read only at `StartRun`, so effective at the next `reset` / `mode=`:**
`InitialLumen`, `InitialTecton` (founders, and the neutral-control targets), `ResourcePatchesA`,
`ResourcePatchesB`, `PatchCapacity`, **`PatchRegenPerSec`** (each patch stores its regen at spawn, so a live
edit changes nothing until the patches are respawned), `TraceCells`, `TraceXHalfLife`, `TraceYHalfLife`
(the trace grids are built in `StartRun`), `Seed`, `Mode`, `FounderGenome` (the `Genome.` scope),
`FounderSpread`, `FounderAgeSpread`, `bWriteLogs`, `PatchMinSpacing` (used when patches respawn),
`PatchChannelClearance` and `PatchDryMargin` (patch placement, 2026-09-18; `Look.WetlandBand` now sets only wetness
shading and groundcover/shrub density and no longer moves patches).
`WorldHalfSize` / `WorldHalfSizeY` are mixed (clamping and spawn positions use the new value, the trace
grids and the rendered arena keep the old one): treat them as reset-only. `Look.RiverBranches`,
`RiverBranchWidth/Depth`, `RiverWidth/Depth/Amp/Wavelength` are terrain fields: never live (see below).

**Process lifetime (not even a reset picks them up):** `PolicyServers`, `PolicyTimeoutMs`,
`PolicyServerFile`, `ControlFile` (all resolved in `BeginPlay`).

**`Lumen.*` / `Tecton.*`:** an organism copies its species struct at birth (`ASWAgent::Init`), so every
field (`ReproThreshold`, `MaxAge`, `MoveSpeed`, `ForageRate`, `BasalBurn`, `StartEnergy`, ...) applies to
organisms born **after** the command; living ones keep their copy. A population turns over within
`MaxAge` logical seconds (150 for Lumen, 260 for Tecton), or `reset` applies it to everyone at once.

**`Genome.*`:** the founder genome, used at the next `reset` / `mode=` (in mode B every child copies
its parent, so this is the whole population's genome after the reset).

**`Look.*`:** read when an organism's body is built (birth, or `reset` for everyone):
`bAuthoredCreatures`, `AuthoredLumenScale`, `AuthoredTectonScale`, `bCreatureShadows`
(docs/CREATURE_RENDERING.md). Live: `bLumenTrails`, `TrailSampleInterval`, `TrailSamples`, `CreatureGlow`,
`SignalGlowBoost`, `LumenGlow`, `TectonGlow`, `DroughtBlendSeconds`, `bDroughtPreview`, `bShowCaptions`
(default true; 0 hides the caption layer) and `CaptionSeconds` (default 6; how long a `caption=` stays up,
read while it is on screen, so `set Look.CaptionSeconds=10` lengthens the caption that is already up),
`bShowHUD`
(default true; `set Look.bShowHUD=0` makes `ASWHUD::DrawHUD` return before it draws anything — title, stat
cards, panels, minimap and name tags all disappear on the next frame, `=1` brings them back; clean framing
for a recorded take, the sim is unaffected — **captions still draw**, because the title is the take's
narration),
`DroughtWaterDrop`, `bScientistAvatars` (0 removes the field-team avatars on the next frame; 1 shows them
only while a policy server is sending `scientists` reports, docs/POLICY_API.md §5). Applied at the next
drought transition (the environment re-applies its drought blend
only while the blend moves; `drought=toggle` twice forces it): `SunColor`, `SunTemperature`, `SunPitch`,
`SunYaw`, `MieScale`, `FogColor`, `FogDensity`, `VolumetricFogAlbedo`, `FogDirectionalColor`,
`WaterLevel`, `WaterfallGlow`, `WhiteTemp`, `HighlightTint`, `Saturation` and their `Drought*`
counterparts. Next reset (patches respawn): `ResourceGlow`, `ResourceAGlow`, `ResourceBGlow`. Never
(the valley is built once in `ASWEnvironment::Build`): terrain, river, arches, rocks, imported asset
counts, cliff walls, post-process build values, clouds, moon, waterfalls, trace overlay colours,
`ConsoleCommands`. Do **not** live-edit the terrain fields (`ValleyDepth`, `River*`, `TerrainHalfSize`,
...): organisms stand on `SWProc::GroundZ(Look, ...)` every substep and would float above or sink into
the mesh that was built with the old values.

## `commands.csv`

One file per run directory, header written when the run log opens, one row appended (and flushed) per
executed line, accepted or rejected:

```
run_id,sim_time,wall_utc,command,result
20260906-160102_seed1_C_learning_evolution,84.30,2026-09-06T14:01:26.512Z,"drought=on","ok: drought on at t=84.3"
20260906-160102_seed1_C_learning_evolution,90.10,2026-09-06T14:01:32.117Z,"set Lumen.ReproThreshold=85","ok: 1/1 field(s) set"
20260906-160102_seed1_C_learning_evolution,95.00,2026-09-06T14:01:37.004Z,"sped=5","rejected: unknown command (drought | speed | pause | set | reset | mode | note | caption | at | cam | follow)"
20260906-160144_seed3_C_learning_evolution,0.00,2026-09-06T14:02:08.330Z,"reset seed=3","ok: run 20260906-160102_seed1_C_learning_evolution ended at t=101.2; started 20260906-160144_seed3_C_learning_evolution (seed 3, mode C_learning_evolution)"
```

`sim_time` is the logical clock right after the command ran; `command` and `result` are CSV-quoted
(inner quotes doubled). A `reset` / `mode=` row lands in the run it **created** (the old run's logger is
closed by then); its result names the previous run and the time it ended. With `bWriteLogs=false` there
is no CSV, only the UE log lines. `python3 Tools/control.py --tail 10` prints the last rows of the newest
run.

## Determinism

A run with no control file, or with no new lines, is unchanged: the poll is a file stat. A run that
received commands is **not** reproducible from seed + mode alone, because the commands were applied at
wall-clock poll times, i.e. at whatever substep the world had reached. `commands.csv` is the record: to
repeat an experiment, replay the `set` lines as `--set` at launch and re-issue the timed commands from
their `sim_time` column (by hand or from a script watching `population.csv`). Keep `commands.csv` with
the run when you archive it.

## `Tools/control.py`

Standard library only.

```bash
python3 Tools/control.py "drought=on"                                   # validates, appends to Saved/control.txt
python3 Tools/control.py "set Lumen.ReproThreshold=85" "note=step 2"    # several lines, in order
python3 Tools/control.py --file C:/elsewhere/control.txt "speed=50"     # a sim launched with --control-file
python3 Tools/control.py "at=118 caption=Drought" "at=120 drought=on"   # a shot list for a recorded take
python3 Tools/control.py --check "mode=X"                               # exit 2: rejected before it reaches the file
python3 Tools/control.py --tail 10                                      # last 10 rows of the newest run's commands.csv
```

The validator mirrors the sim's grammar; a line it rejects is never written. The HUD title block shows
`ctrl` once at least one command was accepted in the current run.
