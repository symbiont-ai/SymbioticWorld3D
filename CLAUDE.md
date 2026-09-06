# Symbiotic World — project instructions

Unreal Engine 5.7 C++ ecosystem sim for Sundai Hack 139 (2026-09-06).
Read `README.md` (build/run), `DESIGN.md` (exact mechanism definitions),
`CHECKLIST.md` (phases + exit conditions), `PROGRESS.md` (what passed).
Collaborators: read `docs/CONTRIBUTING.md` before changing the sim.

## Commands (repo root)

- Build: `Tools/build.bat` (wraps Build.bat for SymbioticWorldEditor Win64 Development and REFUSES to run while any UnrealEditor / UnrealEditor-Cmd process exists; never call Build.bat directly)
- Python: `python` (has pandas/matplotlib/scipy)
- Headless run: `python Tools/run_sim.py --mode C --seed 1 --duration 600 --analyze`
- Sweep without recompile: `--set "Settings.PatchRegenPerSec=5;Lumen.ReproThreshold=85"`
- Self-screenshot: `--windowed --shot 4,40 --no-logs --speed 1 --duration 45` → `Saved/Screenshots/WindowsEditor/`
- Analyze any runs: `python Analysis/analyze_run.py --root Saved/SymbioticWorld`
- Sweep: `python Tools/sweep.py --mode C --seed 1 2 --duration 600 --baseline --set "Settings.PatchRegenPerSec=6"`
- Screenshot with inspector: add `--auto-select` to the windowed run

## Hard rules

- **Do not build while any UnrealEditor / UnrealEditor-Cmd process is running.** The module DLL is locked, the link fails, and replacing the DLL under a live session crashes it (it killed the Pixel Streaming demo once). `Tools/build.bat` enforces this; agents must use it.
- All sim logic is C++ (`Source/SymbioticWorld`). No Blueprint logic.
- Shared contract = `SWTypes.h` + `DESIGN.md`. Consume it; never redefine it.
- Terminology: tabular contextual bandit (γ = 0), not Q-learning. Evolution = Gaussian mutation of {α, ε, social}. Never write "intelligence", "emergent", "cooperation" into user-facing strings.
- All randomness via the manager's seeded `FRandomStream`.
- Do not commit or push unless the user asks.

## Manager Loop

`/phase` (coordinator skill) → `implementer` agent builds one CHECKLIST item →
`tester` agent verifies with numbers → box ticked + PROGRESS.md line.
`/phase all` loops. Agents are defined in `.claude/agents/`.
