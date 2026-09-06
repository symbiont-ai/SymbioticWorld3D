# Symbiotic World — project instructions

Unreal Engine 5.7 C++ ecosystem sim for Sundai Hack 139 (2026-09-06).
Read `README.md` (build/run), `DESIGN.md` (exact mechanism definitions),
`CHECKLIST.md` (phases + exit conditions), `PROGRESS.md` (what passed).

## Commands (repo root)

- Build: `"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" SymbioticWorldEditor Win64 Development -Project="<repo>/SymbioticWorld.uproject" -WaitMutex -NoHotReload`
- Python: `python` (has pandas/matplotlib/scipy)
- Headless run: `python Tools/run_sim.py --mode C --seed 1 --duration 600 --analyze`
- Sweep without recompile: `--set "Settings.PatchRegenPerSec=5;Lumen.ReproThreshold=85"`
- Self-screenshot: `--windowed --shot 4,40 --no-logs --speed 1 --duration 45` → `Saved/Screenshots/WindowsEditor/`
- Analyze any runs: `python Analysis/analyze_run.py --root Saved/SymbioticWorld`
- Sweep: `python Tools/sweep.py --mode C --seed 1 2 --duration 600 --baseline --set "Settings.PatchRegenPerSec=6"`
- Screenshot with inspector: add `--auto-select` to the windowed run

## Hard rules

- **Do not build while any UnrealEditor / UnrealEditor-Cmd process is running.** The module DLL is locked and the link fails. `tasklist | findstr UnrealEditor` first.
- All sim logic is C++ (`Source/SymbioticWorld`). No Blueprint logic.
- Shared contract = `SWTypes.h` + `DESIGN.md`. Consume it; never redefine it.
- Terminology: tabular contextual bandit (γ = 0), not Q-learning. Evolution = Gaussian mutation of {α, ε, social}. Never write "intelligence", "emergent", "cooperation" into user-facing strings.
- All randomness via the manager's seeded `FRandomStream`.
- Do not commit or push unless the user asks.

## Manager Loop

`/phase` (coordinator skill) → `implementer` agent builds one CHECKLIST item →
`tester` agent verifies with numbers → box ticked + PROGRESS.md line.
`/phase all` loops. Agents are defined in `.claude/agents/`.
