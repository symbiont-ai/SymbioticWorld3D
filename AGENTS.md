# Instructions for AI coding agents working in this repository

This file is read by Cursor, Codex and similar tools. Claude Code reads `CLAUDE.md`, which
carries the same rules for the host machine. Read both, then `docs/CONTRIBUTING.md`.

## The two ways to add an agent

1. **A new controller for organisms, in Python, no Unreal needed** (this is what most
   collaborators want). Copy `Tools/policy_server.py`, implement `MyAgent.act(obs, mask)` and
   `MyAgent.learn(obs, action, reward)`, run `python3 Tools/policy_server.py --agent my --port 9000`,
   and the host adds `your-ip:9000=Lumen` (or `Tecton`, `Both`) to `Saved/policy_servers.txt`; the
   running world connects within seconds. The exact JSON contract, every percept field and the
   fallback rules are in `docs/POLICY_API.md`. Develop offline with
   `python3 Tools/policy_replay.py --agent my --file docs/samples/decide_sample.jsonl` (exit 0 means
   no infeasible actions) and `python3 Tools/policy_client_check.py` against your server.
2. **A new species or agent type inside the simulation, in C++**: follow recipe (iii) in
   `docs/CONTRIBUTING.md` (file and function anchors). Only the Windows host can build and run it;
   open a pull request with the verification numbers the guide asks for and the host builds it.

## Rules that override everything else

- Never build on a Mac. On the host, build only with `Tools/build.bat`, which refuses while any
  Unreal process is running; never call `Build.bat` directly and never kill an editor process you
  did not start (a live demo may be streaming from this tree).
- All simulation logic is C++ under `Source/SymbioticWorld`; no Blueprint logic. Every random draw
  goes through the manager's seeded stream. A run with no external policy must stay byte-identical
  for the same seed (`docs/CONTRIBUTING.md` §6 shows the check).
- `Source/SymbioticWorld/SWTypes.h` and `DESIGN.md` are one contract: change both or neither.
- Terminology: tabular contextual bandit (not Q-learning); evolution is Gaussian mutation of the
  learning parameters; do not write "intelligence", "emergent" or "cooperation" into user-facing text.
- Do not commit generated logs, screenshots or the licensed Electric Dreams / Megascans content
  (`.gitignore` already excludes them). Do not commit or push unless asked.
- Report exactly what you verified with numbers and what you could not.
