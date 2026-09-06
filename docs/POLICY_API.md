# Policy API: drive organisms from your own Python process

The sim (Unreal, Windows host) can hand the **action choice** of some organisms to an
external *policy server* over TCP. Everything else stays in the sim: sensing, the
feasibility gate, energy, reproduction, death, the trace fields, the CSV logs, and the
organism's own tabular contextual bandit (which keeps receiving every reward, so an
organism can be switched back to built-in at any time without losing anything).

Reference server: `Tools/policy_server.py` (stdlib only, Python 3.9+, macOS/Linux/Windows).
Self-test without the sim: `Tools/policy_client_check.py`. Offline replay of a recorded session:
`Tools/policy_replay.py` (section "Offline development on a Mac" below).

## Roles and transport

* The **sim is the TCP client**; your process is the **server** (bind `0.0.0.0:<port>`).
* One connection per server. Newline-delimited JSON, UTF-8, one object per line, `\n` terminated.
* The sim reconnects every 5 s of wall time while a server is down.
* Multiple servers can be configured (one per species, or `Both`). Each gets its own request.

Host side:

```
python Tools/run_sim.py --mode C --seed 7 --duration 600 --speed 20 --policy "10.228.152.5:9000=Lumen|10.228.152.7:9000=Tecton"
                       [--policy-timeout 200] [--policy-share 1.0]
```

Raw engine flags: `-SWPolicy="host:port=Species|host:port=Species"` (Species = `Lumen`, `Tecton`,
`Both`; `|` between servers, `=` before the species; `,` and `;` are not allowed because
`FParse::Value` stops at `,` and `-SWSet` owns `;`), `-SWPolicyTimeoutMs=200`,
`-SWPolicyShare=1.0`. The same three live in `FSWRunSettings` as `PolicyServers`,
`PolicyTimeoutMs`, `PolicyShare`, so `--set "Settings.PolicyServers=host:port=Lumen"` also works.

`PolicyShare` is the fraction of a served species assigned to its server, decided **per
organism at birth** with the seeded stream (a draw only happens when share < 1, or when two
servers serve the same species and one has to be picked). The rest stay built-in.

Servers can also be added, changed or removed **while the sim runs** by editing a text file;
see "Adding servers while the sim runs" below. `-SWPolicy` is the launch-time alternative.

## Messages

### 1. `hello` (sim -> server), once per connection and again on every run reset

```json
{"type":"hello","protocol":1,
 "actions":["forage","explore","follow","avoid","signal","rest","modify"],
 "bins":["LOW","MID","HIGH"],
 "species":["Lumen","Tecton"],
 "controls":["Lumen"],
 "seed":7,"mode":"C","mode_name":"C_learning_evolution","run_id":"20260906-140102_seed7_C_learning_evolution",
 "decision_interval":1.000,"substep":0.100,"timeout_ms":200,"share":1.000,"world_half_size":4500.0,
 "max_energy":{"Lumen":100.0,"Tecton":160.0},"max_age":{"Lumen":150.0,"Tecton":300.0},
 "learning":"tabular contextual bandit, gamma 0; the sim keeps updating each organism's own table with every reward"}
```

`actions` is the action order used by every `mask` and `q` array. `controls` lists the
species any configured server drives (the hello is shared between servers).

### 2. `decide` (sim -> server), at most one per logical substep per server

Sent in every substep in which at least one of that server's organisms is due to decide
(every `decision_interval` = 1 logical s per organism; founders decide in lock-step, children
whenever they were born). The sim then **blocks** for up to `timeout_ms`.

```json
{"type":"decide","t":12.30,"step":123,"server":"10.228.152.5:9000","agents":[
 {"id":17,"species":"Lumen","generation":0,"age":41.30,"energy":55.213,"max_energy":100.0,
  "bin":"MID","bin_index":1,
  "mask":[1,1,1,0,1,1,1],
  "last_action":"forage","last_reward":0.6120,"last_bin":"MID","last_external":true,"decisions":41,
  "q":[[0.0123,0.0400,0.0011,0.0480,0.0022,0.0312,0.0007],
       [0.4210,-0.1150,0.0030,0.0100,-0.0900,-0.0800,0.0200],
       [0.0100,0.0200,0.0300,0.0400,0.0000,0.0100,0.0200]],
  "position":[-1204.5,350.2],"heading":42.0,
  "percept":{"energy":55.213,"max_energy":100.0,
             "resource_known":true,"resource_loc":[-900.0,500.0],"resource_dist":335.4,"resource_dir":[0.8944,0.4472],"resource_stock":88.50,
             "same_species_in_range":2,"other_species_in_range":0,"neighbour_known":true,"neighbour_centroid":[-1400.0,300.0],
             "nearest_any_agent_dist":410.2,
             "signal_known":false,"signal_loc":null,
             "trace_x":0.1200,"trace_y":0.0000,"trace_x_gradient":true,"trace_x_gradient_dir":[0.7071,0.7071],
             "on_land":true,"patch_in_cell_needs_soil":false},
  "genome":{"alpha":0.1200,"epsilon":0.2000,"social":0.5000,"e":0.5000}}
]}
```

Field list (per organism):

| field | meaning |
|---|---|
| `id` | organism id, stable for its lifetime, never reused within a run |
| `species` | `Lumen` or `Tecton` |
| `generation` | 0 for founders |
| `age`, `energy`, `max_energy` | logical seconds; energy units |
| `bin`, `bin_index` | energy thirds `LOW`/`MID`/`HIGH` = 0/1/2, the built-in bandit's context |
| `mask` | 7 ints in `actions` order, 1 = feasible **now**. Reply only with feasible actions |
| `last_action` | the action that just ended (your previous choice, or the built-in's on a fallback); `null` on the first decision |
| `last_reward` | the **exact** reward the sim credited for `last_action`: `dEnergy / RewardScale (10)` + the documented interaction term (DESIGN.md §4); `null` on the first decision |
| `last_bin` | the bin `last_action` was chosen in |
| `last_external` | `true` if `last_action` came from you; `false` if the built-in bandit chose (timeout, disconnect, infeasible reply) |
| `decisions` | decisions made so far |
| `q` | `[3][7]` the organism's own bandit table (DESIGN.md §1). A hint: it keeps learning from every reward whoever chose the action |
| `position`, `heading` | world units (arena is `[-world_half_size, +world_half_size]`), yaw in degrees |
| `percept.*` | every `FSWPercept` field: `energy`, `max_energy`, `resource_known`, `resource_loc`, `resource_dist`, `resource_dir` (unit vector toward it), `resource_stock`, `same_species_in_range`, `other_species_in_range`, `neighbour_known`, `neighbour_centroid`, `nearest_any_agent_dist`, `signal_known` (a Lumen signal received in the last 12 s), `signal_loc`, `trace_x`, `trace_y`, `trace_x_gradient`, `trace_x_gradient_dir`, `on_land`, `patch_in_cell_needs_soil`. Locations/distances are `null` when there is nothing |
| `genome` | inherited `alpha`, `epsilon`, `social`, `e` (fixed for life; mutated at birth) |

What the actions do is in `DESIGN.md` §1 and §4 (forage moves to / eats the nearest stocked patch;
explore is a random walk; follow goes to a fresh signal, else the same-species centroid, else up the
Trace X gradient; avoid moves away from the centroid; signal broadcasts the known resource to
same-species neighbours (Lumen only); rest burns least; modify deposits Trace X (Lumen) / Trace Y
(Tecton)).

### 3. `actions` (server -> sim), one per `decide`

```json
{"type":"actions","step":123,"actions":{"17":"forage","23":5,"40":"explore"}}
```

Values are action names or indices `0..6` (numbers or numeric strings). Keys are organism ids
as strings. `step` is optional but **recommended**: a reply whose `step` differs from the current
request is discarded as stale (this is how a reply that arrives after the timeout is kept from
being applied to the next substep).

### 4. `log` (server -> sim), optional, any time

```json
{"type":"log","text":"epoch 3, mean reward 0.21"}
```

Printed in the UE log as `[host:port] text`.

### server -> sim: `scientists` (optional, visual only)

```json
{"type":"scientists","team":[{"name":"Vesper","x":-1200.5,"y":350.0}]}
```

Reports embodied field observers (the Symbiotic Lab's `observe --embody`) for
the sim's avatar layer: the sim renders one labelled mannequin per entry and
smooths movement between reports. `Look.bScientistAvatars` gates the layer and
is OFF by default (the plain god-view is the stable demo configuration); turn
it on at launch with `-SWSet "Look.bScientistAvatars=true"` or live through
the control file (`set Look.bScientistAvatars=1`), and off again the same way.
The lab's witnessing and evidence are bridge-side and work identically with
the layer off.
Coordinates are arena uu, the same space as organism `position`. Send at most
a few per sim-second; entries beyond 16 are ignored. STRICTLY visual: no
organism can perceive an avatar, nothing enters the seeded stream, the CSVs,
or the percepts, so a run reproduces byte-identically with or without them.
Builds older than this message ignore it.

## Fallback rules (the organism uses its own built-in bandit for that one decision)

* the server is not connected (also at the first decision, which happens at birth, before any exchange);
* no `actions` reply within `timeout_ms` (a late reply is discarded by its `step`);
* the reply has no entry for that organism, or the entry is not a valid action;
* the action is **infeasible** under the organism's `mask`.

Every fallback is counted: `population.csv` columns `ext_decisions` / `ext_fallbacks` (cumulative,
per species), and the UE log reports connect / disconnect / timeout / recovery **once per state
change**, plus a throughput line every 10 s. `agents.csv` has a final `policy` column
(`builtin` or `ext:host:port`). The inspector shows `policy: external host:port` or
`policy: builtin`; the title block shows `ext N/M` (external organisms / total).

## Adding servers while the sim runs

The sim watches a **server list file** and applies every change without a restart. This is the
normal way to bring a collaborator's server into a running world; `-SWPolicy` is the launch-time
alternative (both can be used together).

* **Path:** `Settings.PolicyServerFile`, default `Saved/policy_servers.txt`, relative to the
  project directory (`<repo>/`). Change it with `-SWPolicyFile=<path>`, `run_sim.py --policy-file
  <path>`, or `--set "Settings.PolicyServerFile=<path>"`; an empty value turns the watch off.
  Template: `Tools/policy_servers.example.txt`.
* **Format:** one server per line, `host:port=Species` with `Species` = `Lumen`, `Tecton` or `Both`
  (case-insensitive). `#` starts a comment, blank lines are ignored, whitespace is trimmed. A malformed
  line is logged once (with its line number) and skipped; the rest of the file still applies. The same
  `host:port` twice in the file: the later line wins.
* **Poll:** every `Settings.PolicyFilePollSec` seconds (default 3) of **wall** time, from the
  manager's per-frame tick, never inside a logical substep and never touching the seeded stream. The
  file is only re-read when its size or modification time changed. A missing file means "no file
  servers" (logged once at start as `Policy file not present, watching <path> (polled every 3.0 s; ...)`); a file that
  disappears later drops its servers.
* **Effective set** = the `-SWPolicy` entries plus the file entries. The same `host:port` in both:
  the file's species wins. Every change is logged as one line,
  `Policy servers: +10.0.0.5:9000=Lumen -10.0.0.7:9000=Tecton ~10.0.0.9:9000=Both (file <path>); ...`
  (`+` added, `-` removed, `~` species changed), with the number of organisms bound and unbound.
* **What happens on a change**, applied between two substeps so no organism switches policy
  mid-decision: a new server gets a connection (same connect / reconnect-every-5-s behaviour as a
  launch-time server; it receives the `hello` on connect); a removed server is closed and every
  organism bound to it goes back to its built-in bandit at once (no fallback is counted: fallbacks
  count only decisions a bound server failed to answer); an organism bound to a server whose species mapping no longer covers it is
  unbound the same way; unbound organisms of a species whose server set changed are assigned with the
  **same rule as at birth** (`PolicyShare`, choice among several servers). That rule draws from the
  seeded stream only when there is a real choice (share < 1, or more than one server for the species),
  exactly as at birth, so a run without any server stays byte-identical. Servers that were already
  connected keep their connection and their organisms; they are not sent a new `hello` (a `hello`
  means "new run" to a server; every server still gets one on a run reset). Consequence: adding a
  second server for a species that already has one gives the newcomer only organisms born from then
  on. To move a species, edit the old line's species (for example `Both` -> `Tecton`): its Lumen
  are unbound at the next poll and assigned to the Lumen server(s). Verified on a live stream: a
  line appended at 15:23:17 was connected and answering within one second; removing it unbound its
  organisms within three seconds while viewers stayed connected.
* The HUD title's `ext N/M` is the number of organisms currently bound to a server; the UE log prints
  `Policy servers: K configured, C connected; bound organisms N/M (...)` every 10 s while the set is
  non-empty.

Finding the servers on the wifi, `Tools/policy_probe.py` (stdlib only):

```bash
python3 Tools/policy_probe.py                                       # scan this machine's own /24 for port 9000
python3 Tools/policy_probe.py --subnet 10.228.152 --port 9000        # a given /24, another port
python3 Tools/policy_probe.py --write Saved/policy_servers.txt       # append what it found to the server file (no duplicates)
```

It connects to every address of the /24 (1.5 s timeout), and for each open port does a real `hello` +
one-organism `decide` exchange. Hosts that answer with a valid, feasible action are printed as
ready-to-paste lines, `10.228.152.5:9000=Both   # replied in 0.6 ms`; open ports that do not speak
the protocol are listed separately with the reason. `--species Lumen|Tecton|Both` sets the species
on the printed lines; `--write` appends only `host:port`s that the file does not list yet.

## Timing advice

* The sim **blocks per substep** while it waits for your reply. At `--speed 20` there are ~200
  substeps per wall second and a decision round every ~10 substeps with ~40 organisms in it.
  Keep a whole request (all organisms) under a few ms: pure-Python table lookups are fine,
  a neural network call per organism is not, unless you batch it.
* `Tools/policy_server.py` prints decisions/s and the mean time it spent in `act()` per request
  every 5 s; the UE log prints the mean round trip as seen from the sim. If you see timeouts,
  raise `--policy-timeout`, lower `--speed`, or make `act()` cheaper.
* Handle `learn()` from the *next* request: `last_action`/`last_reward` are delivered in the
  same message as the new observation, so a learning agent never needs a second round trip.

## Determinism caveat

Without `--policy` the sim is byte-identical to the previous build for a given seed and mode
(no code path touches the seeded stream). With external policies the run is still fully
**logged**, and it is reproducible only if the external agent is: same replies to the same
requests (seeded RNG in your agent, no dependence on wall time), no timeouts. A timeout or a
disconnect changes which organisms draw from the sim's seeded stream, so two runs against a
slow server can differ.

## Offline development on a Mac (no Unreal)

You do not need the sim to develop an agent. A machine that *was* connected to the sim can
record every exchange, and `Tools/policy_replay.py` feeds that recording to your agent class
exactly the way the server would, without Unreal, without pip installs (stdlib, Python 3.9+).

### Record (on any machine the sim connects to, usually the Windows host)

```bash
python Tools/policy_server.py --agent heuristic --port 9100 --record my_run.jsonl
python Tools/run_sim.py --mode C --seed 4 --duration 300 --speed 20 --policy "127.0.0.1:9100=Both" --policy-share 0.05 --no-logs
```

`--record PATH` appends one JSON line per `decide`/`actions` exchange:

```json
{"t_wall": 1788986267.12, "decide": {"type":"decide","t":12.30,"step":123,"agents":[...]}, "actions": {"type":"actions","step":123,"actions":{"17":"forage"}}}
```

`decide` is the sim's message verbatim (every field of section 2), `actions` is the reply the
server sent. The `hello` is not recorded. The file is opened in append mode, so several runs
or several server processes can share one file. Size: about 1 KB per organism per decision;
the default population (52 founders, one decision each per logical second) writes ~50 KB per
logical second, so for a shareable sample use `--policy-share` (a small served subset of a
normal population) or a short run.

### The sample: `docs/samples/decide_sample.jsonl`

Recorded with the two commands above (mode C, seed 4, 300 logical s, `HeuristicAgent`
replying, share 0.05) and cut to the first 300 whole lines: 350 KB, `t = 1.0 .. 216.0`,
6 organisms (4 Lumen, 2 Tecton; 187 Lumen and 113 Tecton decisions, one or two organisms per
exchange), 0 timeouts, 0 stale replies. Because the served subset is drawn per organism at
birth, the world around them is the full 52-founder population.

### Replay

```bash
python3 Tools/policy_replay.py --agent my --file docs/samples/decide_sample.jsonl            # --agent random|bandit|heuristic|tracefollower|my
python3 Tools/policy_replay.py --agent bandit --file my_run.jsonl --limit 100                # first 100 exchanges only
```

For every recorded `decide` it creates one agent instance per organism id (as the server does),
calls `learn(prev_obs, last_action, last_reward)` with the sim's recorded credit for the
organism's previous action, then `act(obs, mask)`, and prints:

```
file        docs/samples/decide_sample.jsonl  (300 exchanges)
agent       BanditAgent (--agent bandit)  vs recorded replies in the file
sim time    t=1.0 .. 216.0 s, 6 organisms Lumen 187, Tecton 113 (decisions)
decisions   300   in 1.7 ms wall   = 172,157 decisions/s   (learn() calls 294)
yours     forage   127 ( 42.3%)  explore    19 (  6.3%)  follow     7 (  2.3%)  avoid    22 (  7.3%)  signal    36 ( 12.0%)  rest    28 (  9.3%)  modify    61 ( 20.3%)
recorded  forage   184 ( 61.3%)  explore   116 ( 38.7%)  follow     0 (  0.0%)  avoid     0 (  0.0%)  signal     0 (  0.0%)  rest     0 (  0.0%)  modify     0 (  0.0%)
agreement   116 / 300 decisions same as recorded (38.7%)
mean recorded reward by recorded action (last_action -> last_reward, sim credited):
    forage   +0.2124  (n=182)
    explore  -0.1684  (n=113)
    follow   -0.1767  (n=3)
    signal   -0.1900  (n=1)
    modify   -0.1939  (n=1)
mask violations 0   (OK)
slowest act()   9 us  (organism 39, step 10)
```

* `mask violations` **must be 0**: each one is an action the sim would have refused and replaced
  by the organism's built-in bandit (a counted fallback). Exit code is 1 if any violation, invalid
  return value or exception occurred, 0 otherwise, so the script works as a test.
* `mean recorded reward by recorded action` groups the sim's `last_reward` by `last_action`.
  Actions that never appear in the recorded replies (here `follow`, `signal`, `modify`) are the
  built-in bandit's own choices at birth, before the first exchange (`last_external: false`).
* `slowest act()` is the single worst call; the sim waits for a whole request (all organisms),
  so keep act() in the tens of microseconds. `decisions/s` is your act()+learn() throughput
  on this machine, for comparison against the ~200 decisions/s the sim needs at `--speed 20`
  with 40 organisms (section "Timing advice").
* Replay is **off-policy**: the trace is fixed, so your choice does not change the next
  observation, and `learn()` receives the reward of the *recorded* action. A learning agent
  gets realistic `(obs, action, reward)` triples and the mask/timing checks, but not a
  closed-loop score. That needs the sim.

### The loop

1. `python3 Tools/policy_replay.py --agent my --file docs/samples/decide_sample.jsonl` (0 violations, fast).
2. Edit `MyAgent` in `Tools/policy_server.py` (read `HeuristicAgent` and `TraceFollowerAgent` for percept usage).
3. Replay again; `python3 Tools/policy_client_check.py --port 9000` with your server running for the wire format.
4. Ask the host to point the sim at you: `python Tools/run_sim.py --mode C --seed 7 --duration 600 --speed 20 --policy "<your ip>:9000=Lumen"`.
   Run your server with `--record my_agent.jsonl` during that session to get a recording of
   your own organisms for the next offline round, and read `ext_decisions` / `ext_fallbacks`
   in the host's `population.csv` for the real fallback count.

For AI coding agents working in this repo: the contract for an agent class is `__init__(obs)`,
`act(obs, mask) -> name or index`, `learn(obs, action, reward)`; `ACTIONS` order is fixed by the
sim's `hello`; `policy_server.py` must stay importable without side effects (the server starts only
under `if __name__ == "__main__"`), stdlib only, Python 3.9 syntax; `python3 Tools/policy_replay.py
--agent <name> --file docs/samples/decide_sample.jsonl` exiting 0 is the acceptance check.
