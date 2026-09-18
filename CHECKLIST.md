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
- [x] **Editor opens** and Play-in-Editor works; LMB selects; Tab cycles; 1/2/3 change
      speed; P drought; M mode — confirmed at the keyboard by the project owner on 2026-09-18,
      the one item in this list that no headless run or recording could verify. The editor log
      of those sessions shows four Play-in-Editor starts and zero warnings or errors from the
      simulation code; editor startup logs 0 project warnings since commit 89483e2.
- [x] Headless run works: `python Tools/run_sim.py --mode B --seed 42 --duration 300`
      writes `Saved/SymbioticWorld/<run_id>/*.csv` and exits on its own
- [x] `Analysis/analyze_run.py` reports lifetime Q drift ~0 in mode A, > 0 in mode B
- [x] 3 seeds × {C, N} run; comparison printed (selection vs drift) — even if
      not yet significant, the pipeline works
- [x] Balance pass: population does not crash to 0 or pin at cap within 600 s in C
- [x] README build steps verified from a clean `git clone`
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
| 3 | Actors (procedural Lumen dotted/legs + Tecton cracks DONE 2026-09-05; authored Blender skeletal meshes with idle/walk clips wired in 2026-09-11, procedural bodies kept as the fallback: docs/CREATURE_RENDERING.md) | Lumen sleeker (dotted spine, longer legs), Tecton plates + cracks kept, gait; Blender FBX swap (`Content/Characters/Symbiotic`, `Tools/import_symbiotic_creatures.py`) | close-up screenshot; determinism unchanged (same seed => identical CSVs) |
| 4 | Perf ladder | Lumen GI off (done in ini), clouds decision, VSM, screen %, counts | 50x with 220 agents <= 22 ms |
| 5 | Trace X / Trace Y (DONE 2026-09-05 evening; tune half-lives, deposits, wI on the research day) | Lumen information field + Tecton terrain/moisture field, `modify` action feasible, fields logged | `trace_X_mean`, `trace_Y_mean` in population.csv; each species measurably changes a field |
| 6 | Learning / evolution research | balance across seeds 1-5 and 1800 s; selection on alpha vs drift (C vs N) with p < 0.05 or an honest negative | `Analysis/analyze_run.py` cross-run table + plots in `docs/` |
| 7 | Drought | visible event (warm grade, water drop, straw vegetation) + measurable behaviour shift | before/after action distributions in the log; screenshot pair |
| 8 | Demo | camera rail (`-SWDemo=1`), 90 s story, backup video via `-benchmark -dumpmovie` | rehearsed twice from the demo seed |
| 9 | AI scientist (P3, last) | intervention/narration agent over the CSV logs | only after 1-8 are green |

## Backlog (added 2026-09-15)

Open work found in the 2026-09-15 status review, fork check and Lab test prep.

- [x] **Merge WKrohg's fork branch `discourse-trajectory`** (8 commits after PR #2, no PR): OpenRouter
      backend with per-scientist `model:` in `profiles/*.yaml`; Humboldt, a 9th scientist (PI) with a ROUND 0
      agenda and veto arbitration; Fisher revise-and-resubmit before a Karla veto is final; `python -m Lab.lab
      loop` (always-on lab, `--interval`, `--consolidate-every`, failed cycles roll back); dashboard
      collect / pause / reset / save / load (`POST /api/live/*`, `Lab/saves/`); `LAB_TURN_PACE` (default 30 s),
      `LAB_LLM`, `LAB_STREAM_URL`; `Lab/tests/test_openrouter.py`; session-5 report + transcript. Only conflict:
      `.gitignore` (keep both `Content/Characters/Mannequins/` and `Lab/saves/`). Exit: `pytest Lab/tests`,
      `python Lab/tests/test_smoke.py` and `python Lab/tests/test_openrouter.py` pass on the merged tree
      (pytest alone does not collect the two script-style tests).
- [x] **Humboldt vs the field team**: after the merge the roster is nine; check `Lab/embodiment.py` routing, the
      sim's avatar cap and the "eight" headcount strings. Exit: `observe --embody` shows Humboldt as a body, or
      his exclusion is deliberate and documented.
- [x] **Harden the Claude CLI backend** (`Lab/llm.py` `ClaudeCLILLM`): profile as `--system-prompt`, `--tools ""`,
      `--strict-mcp-config`, `--no-session-persistence`, `--json-schema` + `--output-format json`, prompt via
      stdin (Windows argv limit), strip the host `CLAUDE_CODE_*` env, current model ids (`claude-opus-5`,
      `claude-sonnet-5`, `claude-haiku-4-5`, no date suffix), per-scientist `claude_model:` (Fisher, Karla,
      Humboldt on the stronger model). Exit: one probe per response kind returns schema-valid JSON; mock tests pass.
- [x] **Lab test on Claude models**: `session --llm claude --meetings 1` with `LAB_TURN_PACE=0` (model choice
      open). Exit: the designed experiment runs headless, verdict + Brier scores + report are written.
- [x] **Demo recording (block 8, revived 2026-09-17)**: scripted takes instead of the never-built `-SWDemo` rail.
      Control-file additions — `at=<sim_time> <command>` (any command at an exact sim time, pending list cleared on
      reset/mode so a new run never re-fires an old shot), `cam=x,y,z,pitch,yaw` (place the camera, release follow),
      `follow=Lumen|Tecton|Leviathan|<scientist>|none` (the existing -SWFollow paths), `Look.bShowHUD` for clean
      framing — plus `run_sim.py --res WxH` (the windowed run is hard-coded 1600x900; Game Bar records the window).
      Capture with Game Bar (Win+Alt+R, NVENC on the RTX 4060), cut with ffmpeg 8.1. Story beats: learning +
      evolution, Leviathan predation, the field team, drought. Exit: a shot-list control file drives one take with
      every beat, `commands.csv` shows each scheduled command at its preregistered sim time, a second take from the
      same seed + shot list lands the same rows, and the no-policy determinism pair stays byte-identical.
- [x] **Experiment designer** (added 2026-09-17, from the 10-experiment audit): ten experiments held three
      distinct designs and six were one drought contrast re-run, because `Lab/llm.py` `_protocol` was a
      three-branch keyword router (every challenge claim contains "drought"), `victory.sync_questions` rewrites a
      question's text every meeting so the same unmet condition minted a fresh card each time (H-015..H-020), the
      design slot took the NEWEST card so the seeded conjectures H-005..H-007 starved, and a C-vs-C draft with
      empty arms self-vetoed - which is why mode A, the predator, MutationSigma and WeightNovelty were
      unreachable and OQ-VC-3's learning-off control could not be proposed. Fix: a design bench (one row per
      runnable contrast: arms, preregistered metric, direction, threshold), card identity by condition rather
      than wording, least-tested-card-first design slot with up to two experiments per card, a protocol
      signature check that files a repeat as `duplicate` instead of running it, four seeded conjectures for the
      mechanisms that landed after the seed list (predation floor, predation vs epsilon, novelty bonus,
      inheritance fidelity), and mode A + the knob surface in the DESIGN prompt a real model sees.
      Exit: a run of the docket queues >=8 distinct protocols, no two queued/done experiments share
      arms+metric+seeds+duration, H-005..H-007 hold experiments, and the mode-A control answers OQ-VC-3.

- [x] HUD (done 2026-09-18): the "ext N/M" suffix has its own line and the title panel grows to hold it, and
      the field team's name tags are decluttered upward instead of piling into one pixel at camp. Verified at
      1600x900 with nine avatars and a policy bridge attached.
- [x] Repo hygiene (done 2026-09-18): every worktree is clean and `main` is at `origin/main`. Nothing was
      discarded - main's 21 files of creature/avatar drafts are preserved on `archive/creature-drafts-2026-09-18`
      (SWCreatureMeshComponent is 75 lines there against 79 shipped), goofy-aryabhata's SpawnPatches fix was
      restored rather than committed twice because it is already on main verbatim as 27941a8 and its notes are
      archived on its own branch, and the two river experiments are now commits instead of loose files
      (`river-crossings` 00e0c39, 174 insertions; `river-levers` 245c2e7, 635 insertions). The rebase onto main is
      DECLINED, not forgotten: both conflict (PROGRESS.md and docs/POLICY_API.md, plus SWWorldManager.cpp for
      river-levers) on a base 28 commits old, and both experiments failed their own exit conditions - if the
      question is worth reopening, the lab's design bench should re-derive it against current main rather than
      replaying a stale patch.

## Cut order (spec §11)

P0 same individual learns + visible; reproduction; inherited/mutated
meta-parameters; population change across generations.
P1 two species; environmental modification; drought; speeds; control mode.
P2 inspector; evolution chart; polished HUD; VFX.
P3 goal drift; social learning; policy inheritance; AI scientist; Python analysis.

If behind: cut P3, then P2 VFX, then Trace Y deformation. Never cut lifetime
learning or inherited learning parameters.
