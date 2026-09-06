# Symbiotic Lab — the scientist team

Implements `Scientist_Team_PRD.md`: a team of seven scientist agents that
observe runs, argue over what the data means, turn disagreements into
preregistered experiments run through the repo's own tools, and accumulate a
registry of validated knowledge that persists across sessions.

The harness does the science, the model does the interpretation. Everything
consequential is enforced in code, never by the model's judgment:

| rule | where |
|---|---|
| agents may only cite evidence IDs that exist | `meeting.py` (invalid citations dropped) |
| agree with no citation → recorded abstain | `meeting.py::_validate_stance` |
| disagree needs a citation or a named prior | `meeting.py::_validate_stance` |
| unanimity appoints a rotating dissenter | `meeting.py::_appoint_dissenter` |
| stance split → double crux → docketed dispute | `meeting.py::_run_crux` |
| protocol validation (control arm, ≥2 seeds, registered metric) | `meeting.py::_validate_protocol` |
| experiment verdicts (metric vs preregistered threshold) | `runner.py::score_experiment` |
| Brier scoring + credibility weights | `scorer.py` |
| promotion conjecture→supported→law, demotion on failure | `memory.py::apply_experiment_outcome` |
| stance sides scored when a dispute resolves | `runner.py::score_experiment` |
| endangerment detection + two-stage conservation programs | `conservation.py` |
| lab evolution between generations | `evolution.py` |
| Vega's data annex (non-voting, report-only) | `datasci.py` |
| victory conditions (spec §1) scored from evidence | `victory.py::scorecard` |

## Running

```
python -m Lab.lab session --meetings 3 --llm ollama      # needs Ollama + llama3.1
python -m Lab.lab session --meetings 3 --llm mock        # no model needed
python -m Lab.lab ingest Saved/SymbioticWorld/<run_id>   # feed it existing runs
python -m Lab.lab run-queued                             # sim machine: execute experiments
python -m Lab.lab report                                 # regenerate outputs
python -m Lab.lab textbook                               # print the inherited object
python -m Lab.lab victory                                # score the spec's six victory conditions
```

Outputs land in `Lab/reports/` (lab report + transcript with the stance ledger
inline). State lives in `Lab/lab.sqlite`; delete it to start a fresh
generation-zero lab, keep it to inherit the textbook.

A session: seed textbook → N× (meeting → execute queued experiments) →
consolidation → report. On a machine without UE 5.7, designed experiments are
parked as `awaiting-sim` and the report prints the exact `Tools/run_sim.py`
commands; run them (or `python -m Lab.lab run-queued`) on the sim machine.

## Backends

- `--llm ollama` — one local `llama3.1` via Ollama structured outputs
  (`format` = JSON schema, one profile = one system prompt + temperature).
  Model/URL via `LAB_MODEL` / `OLLAMA_URL` env vars.
- `--llm mock` — deterministic scripted scientists driven by each profile's
  declared priors (the `mock:` block in `profiles/*.yaml`). The disagreements
  are real but scripted; use it for pipeline tests and dry demos.

## The lab evolves

Being right in public is a competitive advantage, on two timescales:

- **Within a generation** — credibility moves on every Brier-scored
  preregistered prediction *and* on every resolved dispute (the side that
  called it gains, the side that fought it loses). Credibility weights the
  design docket, the conservation debate, and floor time in the evidence
  round (observers below weight 1.0 lose a finding slot).
- **Between generations** — at each session close, agents are ranked by mean
  Brier; below-median agents mutate temperament (temperature, base confidence)
  toward the fittest agent, with noise, clamped. Identity and priors persist —
  the lab evolves temperament, not personality. Parameters live per generation
  in `agent_params` and overlay the YAML profiles at load.

## Victory conditions (spec §1 — minimum proof of challenge fit)

The spec's six-step "minimum proof" is held in `victory.py` as the lab's
standing victory conditions **about the organisms** — the scientists observe
and verify them, code scores them. Each condition maps to registered metrics
(median lifetime Q drift with a mode A control for "learned because of
experience"; parent/child genome correlation in [0.5, 0.995] for "related but
mutated"; a passed C-vs-N experiment for population-level selection; a
regen-cut arm with survival plus continued Q drift for drought adaptation).
Every session scores the scorecard into the report, and each condition not
yet met stands as an open question (`OQ-VC-*`) so the meetings keep aiming
experiments at the spec's own definition of done. `python -m Lab.lab victory`
prints it on demand.

## Conservation dockets

When recent runs show a species' end-of-run population under the threshold
(`conservation.py::ENDANGERED_N`), the detection code opens a docket card
citing that evidence and the lab **debates the intervention** under the usual
stance rules. If the credibility-weighted debate carries, a two-stage program
runs: **stage 1, assessment** — single-arm baseline runs must show resource
headroom (criteria checked in code); **stage 2, introduction** — founders of
the endangered species boosted vs an unmodified control, with all agents
preregistering the outcome. Programs advance only on recorded verdicts:
`debating → assessing → introducing → done` (or `assessment-failed` /
`rejected`).

## Vega, the data scientist (non-voting)

`datasci.py` produces population/alpha charts and least-squares trend
forecasts with uncertainty bands (verified later against longer runs when
they arrive). Vega influences nobody by construction: the module runs after
the meetings, writes only `Lab/reports/figs/` + the forecasts table + the
report annex, and never touches evidence, stances, notebooks, or credibility.
The smoke test asserts the quarantine.

## How the scientists touch the simulator

The models never drive the sim directly. They emit protocol JSON; code
validates it and the runner executes it through `Tools/run_sim.py`. Today's
intervention surface is exactly what the sim exposes at launch: mode A/B/C/N,
seed, duration, and any `-SWSet` parameter override at t=0 (regen rates,
founder counts, repro thresholds, drought multipliers, the wI knockout
`Settings.WeightInteraction=0`). Mid-run verbs — trigger_drought(t),
spawn/introduce individuals mid-run, freeze_param, fork_run — need small
sim-side additions before the lab can use them.

## Honest notes

- Drought is a runtime toggle in the UE sim (no CLI trigger), so drought
  experiments are designed as regen-rate interventions
  (`Settings.PatchRegenPerSec=1.8` ≈ 0.3× of the regen-6 baseline), which is
  the regen half of what drought does. The capacity half needs a `-SWDrought`
  arg in the sim first.
- `Lab/tests/make_fixture.py` writes synthetic CSVs with a built-in ground
  truth for testing only — never ingest fixtures into a real lab database.
- `python3 Lab/tests/test_smoke.py` runs the whole loop (session on fixtures,
  then simulated sim-machine scoring) and asserts the discourse rules fired.
- Credibility weights currently enter at the design round (ranking which
  dispute gets the experiment slot). Open decision 2 in the PRD (inherit
  weights across generations?) is unresolved; today they persist with the DB.

## Hosting the lab beside the sim (policy bridge)

The repo's `Tools/policy_server.py` + `docs/POLICY_API.md` describe hosting
external Python agents on any machine (macOS/Linux/Windows) that the Windows
sim host connects to over TCP (`run_sim.py --policy "<ip>:9000=Lumen"`). The
lab uses the same split: run the lab (and Ollama) on your machine, keep the
DB/textbook local, and either execute experiments there when the engine is
present or hand the report's printed `run_sim.py` commands to the sim host and
`python -m Lab.lab run-queued` after the CSVs land. The scientists deliberately
do NOT drive organisms through the policy bridge (PRD non-goal: no controlling
individual creatures) — but a scripted policy served from `policy_server.py`
is a legitimate future *intervention arm* for experiments ("does a
forage-greedy Lumen policy change the selection gradient on alpha?").

## The two scientist modes (god view is the default)

- **God view** — `python -m Lab.lab observe`. The DEFAULT and the priority
  mode: the bridge receives every organism's percept, genome, reward and
  chosen action every decision, and distills each 60 s window into global
  evidence: populations, genome means, rewards, traces, and the full
  behavior distribution per species (`live_*_action_*_frac`). Cheapest for
  the sim (no avatar rendering), complete information for the scientists.
- **Embodied field team** — add `--embody`. The eight researchers walk the
  arena as virtual bodies with a sense radius; each observer can present
  only what they personally witnessed, and the sim can render them as
  mannequins (V key / `Look.bScientistAvatars`, off by default). Costs
  render load and partial observability — use it when the point is the
  fieldwork, not the fastest science.

Same discourse rules in both modes; the toggle changes what the scientists
can see, never how claims are validated.

## Joining the live simulator (`observe`)

`python -m Lab.lab observe` starts a policy-bridge server (docs/POLICY_API.md)
that the running sim connects to when launched with
`--policy "<lab ip>:9000=Both"`. The bridge replies to every `decide` with an
empty actions dict — by the protocol's fallback rule each organism keeps
choosing with its own built-in bandit, so behavior is untouched — while the
full live stream (percepts, Q tables, genomes, rewards) is distilled into
citable `live_*` evidence windows (60 sim-seconds each) in `lab.sqlite`.
Run `python -m Lab.lab session` and `python -m Lab.lab ui` against the same DB
and the scientists hold meetings about the world as it runs.

## A button on the world itself

`python Tools/add_lab_button.py` (run on the stream host, idempotent,
`--remove` restores) patches the served Pixel Streaming player pages with a
floating **🧪 Symbiotic Lab** button that opens the dashboard on the same
host at :8765 — so anyone watching the world is one click from the
scientists' conversations and Vega's reports, and the dashboard's World tab
brings them straight back.
