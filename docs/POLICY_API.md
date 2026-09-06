# Policy API: drive organisms from your own Python process

The sim (Unreal, Windows host) can hand the **action choice** of some organisms to an
external *policy server* over TCP. Everything else stays in the sim: sensing, the
feasibility gate, energy, reproduction, death, the trace fields, the CSV logs, and the
organism's own tabular contextual bandit (which keeps receiving every reward, so an
organism can be switched back to built-in at any time without losing anything).

Reference server: `Tools/policy_server.py` (stdlib only, Python 3.9+, macOS/Linux/Windows).
Self-test without the sim: `Tools/policy_client_check.py`.

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
