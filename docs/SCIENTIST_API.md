# Scientist API: controlled experiments on the host, over HTTP

`Tools/experiment_service.py` runs on the Windows host next to the live demo and
lets a scientist agent team on other machines (Macs, Linux boxes, other AI
agents) queue headless runs, read the statistics of each run, download the raw
CSVs, look at the live world, send control commands to it and keep a shared
notebook. It is the closed loop of `docs/CONTRIBUTING.md` §6 exposed as JSON:
each run is `Tools/run_sim.py` + the statistics of `Analysis/analyze_run.py`.

**No authentication, no TLS.** It is meant for one wifi at a hackathon. Do not
expose it to the internet. Every client on the LAN can start runs and write to
`Saved/control.txt`, which drives the live demo.

## 1. Start it on the host

```bash
python Tools/experiment_service.py                      # 0.0.0.0:8800, one worker
python Tools/experiment_service.py --port 8800 --host 0.0.0.0 --workers 1
```

Use the host's anaconda `python` (it has pandas / numpy / scipy, so the
summaries reuse `Analysis/analyze_run.py`; without them the service falls back
to standard-library implementations of the same statistics and reports
`"analysis_backend": "stdlib"`). Runs are launched with the same interpreter
(`--python` to override). Allow `python.exe` through Windows Firewall on
private networks, then hand collaborators the host's LAN IP. Jobs persist in
`Saved/experiments/<job_id>.json`; the run_sim output of each run is in
`Saved/experiments/logs/<run_id>.log`. Restarting the service keeps the history
(runs that were in flight are marked `failed`).

Thin client for the other machines (standard library only):
`Tools/scientist_client.py` (§4).

## 2. Endpoints

All responses are JSON (`Content-Type: application/json`), CORS is open
(`Access-Control-Allow-Origin: *`), errors are `{"error": "..."}` with 400 /
404 / 500. `GET /` is a one-screen HTML index, `GET /docs` is this file,
`GET /health` reports queue length, running runs and the analysis backend.

### POST /runs: queue runs

```
POST /runs
{"mode": "C", "seeds": [1, 2], "duration": 600, "speed": 200,
 "set": "Settings.PatchRegenPerSec=8;Lumen.ReproThreshold=85",
 "label": "regen8", "policy_file": null}
```

`mode` is one of `A`, `B`, `C`, `N` **or a list** (`["C", "N"]` makes the C vs N
comparison in one job). One run per (mode x seed), executed in that order.

| field | rule |
|---|---|
| `mode` | `A` learning off, `B` learning on / genome fixed, `C` learning + evolution, `N` neutral-drift control; string or list |
| `seeds` | 1..8 integers |
| `duration` | 10..3600 logical seconds |
| `speed` | 0.01..1000 (time scale; 200 is the headless default) |
| `set` | `-SWSet` overrides, passed as ONE argument to `run_sim.py --set`. Only `[A-Za-z0-9_.=;:-]`; items `Scope.Field=value` separated by `;`; scopes `Settings`, `Lumen`, `Tecton`, `Genome`, `Look`; field names in `Source/SymbioticWorld/SWTypes.h`. **`,` is illegal** (Unreal stops parsing the argument there). Colours are `r:g:b`. |
| `label` | free text, <= 80 chars of `[A-Za-z0-9_.:- ]` |
| `policy_file` | optional path of a policy-server list for `--policy-file`; must be relative and under `Saved/` (the sim logs every line of the file it is given); such runs are not reproducible (§6) |
| `wall_timeout` | optional, seconds of wall time before the run is killed (default `300 + 4 * duration / speed`) |

Response (`202 Accepted`):

```json
{"job_id": "job-20260906-161500-ab12", "label": "regen8", "queue_len": 2,
 "runs": [{"run_id": "job-20260906-161500-ab12-r1", "mode": "C", "seed": 1, "status": "queued"},
          {"run_id": "job-20260906-161500-ab12-r2", "mode": "C", "seed": 2, "status": "queued"}]}
```

The worker runs the queue **one run at a time** (`--workers 2` allows two, not
recommended while the demo streams). Each run is
`python Tools/run_sim.py --mode M --seed S --duration D --speed SP [--set ...] [--policy-file ...]`;
the service records the exit code and wall time, finds the new directory under
`Saved/SymbioticWorld/` (before/after listing, filtered by `_seed<S>_<ModeName>`),
computes the summary (§3) and writes everything to `Saved/experiments/<job_id>.json`.

### GET /runs: list

```json
{"jobs": [{"job_id": "job-20260906-161500-ab12", "label": "regen8", "status": "done", "created_utc": "...",
           "has_comparison": false,
           "runs": [{"run_id": "job-20260906-161500-ab12-r1", "mode": "C", "seed": 1, "duration": 600, "speed": 200,
                     "set": "Settings.PatchRegenPerSec=8", "status": "done",
                     "sim_run_id": "20260906-161503_seed1_C_learning_evolution", "exit_code": 0, "wall_s": 31.2}]}],
 "queue_len": 0, "running": []}
```

Statuses: `queued`, `running`, `done`, `failed` (a run with a non-zero exit
code, no new run directory, or a wall timeout; `error` says which). A job is
`done` when every run is done.

### GET /runs/&lt;id&gt;: one run (or one job)

`<id>` is a `run_id` from the response above, the `sim_run_id` (the directory
name under `Saved/SymbioticWorld/`, also the `run_id` column of the CSVs), or a
`job_id`. For a run you get the run record: request fields, `status`,
`exit_code`, `wall_s`, `started_utc`, `finished_utc`, `command` (the exact
argv), `run_dir`, `log_file`, `stdout_tail`, and `summary` (§3; `null` until
the run is done). For a job you get the whole job including every run and the
`comparison` block (the Welch test, §3) once the job is finished.

### GET /runs/&lt;run_id&gt;/files/&lt;name&gt;.csv

`name` is `agents`, `births`, `deaths`, `population` or `commands` (the
executed control lines; present only for runs made by a build with the control
file). Raw CSV of the run (schemas: `docs/CONTRIBUTING.md` §5 (vi) and
`docs/CONTROL_FILE.md`; `agents.csv` is large: one row per living organism per
logical second).

```bash
curl -o pop.csv http://HOST:8800/runs/job-20260906-161500-ab12-r1/files/population.csv
```

### GET /live: the world that is running now

Picks the newest run directory whose CSVs were written within the last 10 s
(`"live": true`) or, if none is growing, the newest one (`"live": false`).
Returns its `run_id` (directory name), `sim_time`, `mode_name`, `seed`,
`drought_state`, cumulative `births` / `deaths`, `log_age_s`, and per species
the latest `population.csv` row: `n`, `mean_alpha`, `sd_alpha`, `mean_epsilon`,
`sd_epsilon`, `mean_social`, `sd_social`, `mean_env_effect`, `sd_env_effect`,
`mean_generation`, `max_generation`, `births`, `deaths`, `resource_A`,
`resource_B`, `drought_state`, `trace_X_mean`, `trace_Y_mean`, `ext_decisions`,
`ext_fallbacks`. Also the current text of `Saved/policy_servers.txt`
(`policy_servers_txt`) and `Saved/control.txt` (`control_txt`, `null` when
absent), `commands_executed` (the last 10 rows of that run's `commands.csv`
when it has one: what the sim actually did with the control lines), and
`service_running_runs` (run ids the service itself is executing: if that list
is not empty the "live" directory may be a headless run, not the demo; compare
`run_id` with the demo's).

The population row is written every 5 logical seconds, so at demo speed 1x the
numbers are up to 5 s old.

### POST /control: one command for the live sim

```
POST /control   {"command": "drought=on"}
->  {"ok": true, "command": "drought=on", "kind": "drought", "file": "Saved/control.txt", "line_no": 3}
```

The service validates the line against the grammar in §5 and appends it to
`Saved/control.txt` (never rewrites the file). The running sim polls that file
and executes each **new** line once, then logs it. Anything outside the grammar
is refused with 400 and nothing is written. `GET /control` returns the file.

### POST /notes and GET /notes

```
POST /notes  {"text": "regen 8 keeps Lumen above 40 in 3/3 seeds", "author": "scientist-1"}
->  {"ok": true, "note": {"utc": "2026-09-06T20:15:02+00:00", "author": "scientist-1", "text": "..."}}
GET /notes?limit=50   ->  {"notes": [ ... ]}
```

Appended to `Saved/scientist_notes.jsonl` (one JSON object per line, UTC
timestamp). Use it as the shared lab notebook: hypotheses, job ids, verdicts.

## 3. The summary JSON of one run

Computed from the four CSVs after the run exits. When `Analysis/analyze_run.py`
is importable (pandas present) its functions `lifetime_learning`, `inheritance`
and `per_generation` are used (`"analysis_backend": "pandas+analyze_run"`);
otherwise the same statistics are computed with the standard library
(`"stdlib"`). Both backends have been checked to give identical numbers.

Run-level fields (on the run record and inside `summary`):

| field | meaning |
|---|---|
| `run_id`, `job_id`, `index` | service ids |
| `sim_run_id`, `run_dir`, `log_file` | directory name under `Saved/SymbioticWorld/`, its path, the run_sim output |
| `mode`, `mode_name`, `seed`, `duration`, `speed`, `set`, `label`, `policy_file` | the request |
| `status`, `exit_code`, `wall_s`, `started_utc`, `finished_utc`, `command`, `error` | execution |
| `summary.analysis_backend` | `pandas+analyze_run` or `stdlib` |
| `summary.mode_name`, `summary.seed_logged` | what the CSV says (must match the request) |
| `summary.sim_time_end` | last logged logical second |
| `summary.births_total`, `summary.deaths_total` | cumulative, both species |
| `summary.resource_A_final`, `summary.resource_B_final` | total stock of the two resource types at the end |
| `summary.inheritance_all_species` | parent/child correlation and mean abs delta over all births (see below) |
| `summary.species.Lumen`, `summary.species.Tecton` | per-species block |

Per-species block:

| field | meaning |
|---|---|
| `initial_n`, `final_n`, `min_n`, `max_n` | population at the first / last population row, min and max over the run |
| `max_generation` | highest generation reached |
| `mean_generation_final` | mean generation of the living at the end |
| `births`, `deaths`, `deaths_starvation`, `deaths_age` | counts from `births.csv` / `deaths.csv` for that species |
| `initial_mean_alpha` | founders' mean alpha (first population row) |
| `final.mean_alpha`, `final.sd_alpha`, `final.mean_epsilon`, `final.sd_epsilon`, `final.mean_social`, `final.sd_social`, `final.mean_env_effect`, `final.sd_env_effect` | inherited parameters of the living at the end |
| `ext_decisions`, `ext_fallbacks` | decisions taken by external policy servers / fallbacks to the built-in bandit (0 without `policy_file`) |
| `drought_rows` | number of population rows logged while drought was on |
| `q_drift.n_agents` | organisms with >= 20 decisions (`min_decisions`) and at least two logged rows |
| `q_drift.mean_l1`, `q_drift.median_l1` | lifetime learning: L1 distance between an organism's first and last logged bandit table (3 energy bins x 7 actions), averaged / median over those organisms; ~0 under mode A |
| `q_drift.median_max_abs` | median over organisms of the largest single-entry change |
| `q_drift.greedy_changed_frac` | fraction whose greedy action in the low-energy bin changed |
| `inheritance.n_births` | births of that species |
| `inheritance.<p>_corr`, `inheritance.<p>_mean_abs_delta` for `p` in alpha, epsilon, social, env_effect | Pearson correlation parent vs child, and mean abs(child - parent) (mutation sigma is 0.03) |
| `per_generation[]` | per child generation: `generation`, `n`, `mean_alpha`, `sd_alpha`, `mean_epsilon`, `sd_epsilon`, `mean_social`, `sd_social`, `mean_env_effect`, `sd_env_effect` |

Job-level `comparison` (present when a job has finished runs in both mode C and
mode N):

| field | meaning |
|---|---|
| `species`, `metric` | `Lumen`, end-of-run `mean_alpha` |
| `C.n`, `C.mean`, `C.sd`, `C.runs[]`; same for `N` | the values per seed |
| `t`, `df`, `p` | two-sided Welch t-test (unequal variances) |
| `method` | `scipy.stats.ttest_ind(equal_var=False)` or `builtin Welch (regularized incomplete beta)`; both give the same numbers |
| `reading` | the sentence `analyze_run.py` would print (p < 0.05 or not) |

With fewer than 2 seeds per mode `t` and `p` are `null`.

## 4. The client

```bash
python3 Tools/scientist_client.py --host 10.0.0.1 runs --mode C N --seeds 1 2 3 --duration 300 --set "Settings.PatchRegenPerSec=8" --wait
python3 Tools/scientist_client.py --host 10.0.0.1 wait job-20260906-161500-ab12
python3 Tools/scientist_client.py --host 10.0.0.1 run job-20260906-161500-ab12-r1
python3 Tools/scientist_client.py --host 10.0.0.1 csv job-20260906-161500-ab12-r1 population --out pop.csv
python3 Tools/scientist_client.py --host 10.0.0.1 live
python3 Tools/scientist_client.py --host 10.0.0.1 control "drought=on"
python3 Tools/scientist_client.py --host 10.0.0.1 note "regen 8 viable in 3/3 seeds" --author scientist-1
python3 Tools/scientist_client.py --host 10.0.0.1 notes
```

From Python: `from scientist_client import Client; c = Client("10.0.0.1")`, then
`c.submit_runs(modes, seeds, duration, speed, set_spec, label, policy_file)`,
`c.wait(job_id)`, `c.get_run(run_id)`, `c.get_job(job_id)`,
`c.download_csv(run_id, "population", dest)`, `c.live()`, `c.control(cmd)`,
`c.note(text, author)`, `c.notes()`. Module-level `submit_runs`, `wait`,
`get_run`, `live`, `control`, `note` use a default client set with
`configure(host, port)`.

## 5. Control grammar (`Saved/control.txt`)

One command per line; the running sim polls the file every 2 s of wall time,
executes each new line once, logs it (`control: <line> -> <result>`) and
appends a row to the run's `commands.csv`. The full semantics, and which
settings take effect live versus at the next reset, are in
`docs/CONTROL_FILE.md` (the same grammar as `Tools/control.py`). The service
accepts these forms, keywords case-insensitive, and writes the canonical line:

```
drought=on | drought=off | drought=toggle
pause=on | pause=off
speed=<float>                          e.g. speed=10   (0 stops the logical clock)
set <Scope>.<Field>=<value>[;<Scope>.<Field>=<value>]
                                       Scope in Settings, Lumen, Tecton, Genome, Look; e.g. set Lumen.ReproThreshold=85
reset                                  restart the run with the same seed (a new run directory appears)
reset seed=<int>
mode=A | mode=B | mode=C | mode=N      changes the mode (resets the run)
note=<text>                            recorded in commands.csv only; mark experiment steps with it
```

`set` values: numbers, `true`/`false`, `r:g:b` colours, identifiers
(`[A-Za-z0-9_.:+-]`). `note=` text may not contain a newline (<= 500 chars).
Anything else is refused with 400. Every command is appended, never edited:
the file is the audit trail of what was done to the live world. Note from
`docs/CONTROL_FILE.md`: `Settings.PatchRegenPerSec`, `ResourcePatchesA/B`,
`PatchCapacity` and `Genome.*` are read at `StartRun`, so a live `set` of them
only acts after the next `reset`; `Lumen.*` / `Tecton.*` apply to organisms
born after the command.

## 6. An experiment recipe (DESIGN.md §7)

The claim the demo has to earn is that a shift in mean alpha under mode C is
selection, not drift. DESIGN.md §7: always show the learning-off control (A)
and, for evolution claims, the drift control (N); never label survival or
recovery as intelligence; every run states seed and mode.

1. **Hypothesis**, written to `/notes` first, with the parameter and the
   prediction, e.g. "with `Settings.PatchRegenPerSec=8` Lumen mean alpha under
   C ends above N across seeds 1..3 at 600 s".
2. **Runs**: one job with both modes and >= 3 seeds, same `set`, same
   `duration`:
   `POST /runs {"mode": ["C", "N"], "seeds": [1, 2, 3], "duration": 600, "speed": 200, "set": "Settings.PatchRegenPerSec=8", "label": "regen8"}`
   (6 runs, about 3-4 minutes of wall time at speed 200 while the demo shares
   the CPU). Optionally a mode A job with the same seeds for the learning-off
   baseline (`q_drift.median_l1` must read ~0 there).
3. **Wait** (`wait(job_id)`), then read the job: `comparison.t`, `comparison.p`,
   the per-seed values in `comparison.C.runs` / `comparison.N.runs`, and per run
   `summary.species.Lumen.min_n` (a population that crashed is not a result),
   `max_generation` (fewer than ~5 generations means selection had no time),
   `q_drift` (learning happened at all), `inheritance.alpha_corr` (children are
   mutated copies of parents: 0.8-0.95 expected).
4. **Report** in `/notes`: job id, the six end-of-run alphas, t, p, and the
   verdict in the words of `analyze_run.py` (`SELECTION on alpha distinguishable
   from drift` only if p < 0.05 with >= 3 seeds per mode; otherwise "not
   distinguishable from drift with these seeds"). p-values from 2 seeds per mode
   are almost never below 0.05 and should not be reported as evidence either way.

Known balance state (DESIGN.md §6): default `PatchRegenPerSec = 6`; with the
original 1.6 the Lumen population crashes to 2-6 by 300-600 s. Levers:
`Settings.PatchRegenPerSec`, `Settings.ResourcePatchesA/B`,
`Settings.PatchCapacity`, `Lumen.ReproThreshold`, `Lumen.MinReproAge`,
`Lumen.ForageRate`, `Settings.DroughtRegenMultiplier`.

## 7. Caveats

- **One run at a time on the host.** The default worker count is 1; jobs are
  queued in submission order and `GET /runs` shows the queue. The live demo
  and headless runs share the CPU: a 600 logical-second run takes ~30 s of wall
  time at speed 200 on an idle host, more while the demo streams; `wall_s` on
  each run tells you.
- **Reproducibility.** A run with the same mode, seed and `set` produces
  byte-identical CSVs. Runs with `policy_file` (external policy servers reply
  with wall-clock timing) or runs during which control commands were sent are
  not reproducible; say so when you report them.
- **Control commands reach the world that polls `Saved/control.txt`**: the live
  demo. Do not send `reset` / `mode=` while someone is presenting the demo; use
  `note=` and `/notes` to coordinate. `GET /live` shows the file.
- The `live` view is whichever run directory is growing: while a headless run
  of yours is executing at speed 200 it may be that run, not the demo
  (`service_running_runs` is non-empty then).
- Job records and CSVs stay on the host under `Saved/` (gitignored).
- The service has no authentication and no rate limit.

## 8. For AI agents

Call `GET /docs` once, then `GET /health`. To run an experiment, `POST /runs`
with a list of modes and >= 3 seeds, poll `GET /runs/<job_id>` every 5-10 s
until `status` is `done` or `failed` (do not resubmit while it is `queued` or
`running`; the queue is FIFO), then read `comparison` and each run's `summary`.
Numbers you may quote come from those fields; do not infer them from `/live`
(that is a moving snapshot of a different, interactive run). Before quoting a
mean alpha check `min_n > 0` and `max_generation` for that run. Use the
terminology of `DESIGN.md`: lifetime learning is a tabular contextual bandit
(gamma = 0), evolution is asexual Gaussian mutation of alpha, epsilon and
social; do not describe survival as intelligence or any pattern as emergent
cooperation. Do not assume `/control` commands were executed: the sim polls
the file; confirm through `/live` (`drought_state`, `mode_name`, `seed`) after a
few seconds. Do not assume a `set` override took effect just because the job
ran: compare `summary.mode_name` and, for parameter changes, the resource /
population numbers against a baseline job with an empty `set`. Write your
hypothesis to `/notes` before the runs and the result after, with the job id.
