#!/usr/bin/env python3
"""Reference policy server for Symbiotic World: your Python agents drive organisms in the sim.

Standard library only (Python 3.9+), runs on macOS / Linux / Windows. The sim (on the
Windows host) connects to THIS process over TCP and asks it, once per logical substep,
which action each of your organisms takes. Protocol: docs/POLICY_API.md.

    python3 Tools/policy_server.py --agent bandit --port 9000
    python3 Tools/policy_server.py --agent my                    # your own class, see MyAgent below
    python3 Tools/policy_server.py --agent heuristic --record my_run.jsonl   # also log every exchange for policy_replay.py

Agents: random, bandit (tabular contextual bandit, gamma 0), heuristic (fixed rules on the
percept), tracefollower (uses the Trace X / Trace Y fields), my (yours). No Unreal on your
machine? Develop offline: replay a recording with Tools/policy_replay.py (see docs/POLICY_API.md,
"Offline development on a Mac").

Then tell the host your IP (macOS: `ipconfig getifaddr en0`); the host runs
    python Tools/run_sim.py --mode C --seed 7 --duration 600 --speed 20 --policy "<your ip>:9000=Lumen"

Every 5 s this prints decisions/s and the mean time act() took per request, so you can
see whether you are keeping the sim waiting (keep a whole request under a few ms: the
sim blocks for at most its timeout, default 200 ms, and falls back to its own bandit).

Wire format: one JSON object per line, UTF-8, '\\n' terminated, both directions.
  sim -> server  {"type":"hello", ...}                       once per connection / run
  sim -> server  {"type":"decide","t":..,"step":n,"agents":[...]}
  server -> sim  {"type":"actions","step":n,"actions":{"<id>":"forage", ...}}
  server -> sim  {"type":"log","text":"..."}                 optional, printed in the UE log

What an agent entry in "decide" contains (every field, see docs/POLICY_API.md for units):
  id            organism id (int, stable for its lifetime; reused never)
  species       "Lumen" | "Tecton"
  generation    0 for founders
  age, energy, max_energy
  bin           "LOW" | "MID" | "HIGH"   energy thirds, the built-in bandit's context
  bin_index     0 | 1 | 2
  mask          [7 ints] 1 = action feasible now (in ACTION order); choose only among these
  last_action   the action that just ended (your previous choice, or the built-in's on a fallback), or null
  last_reward   its reward = energy change / 10 (+ small interaction term), exact, or null on the first decision
  last_bin      the bin last_action was chosen in
  last_external true if last_action was your choice (false = built-in bandit chose, e.g. after a timeout)
  decisions     how many decisions this organism has made
  q             [3][7] floats: the organism's OWN tabular bandit values (a hint; it keeps learning)
  position      [x, y] in world units (uu; arena is +-world_half_size)
  heading       yaw in degrees
  percept       see PERCEPT_FIELDS below
  genome        {"alpha","epsilon","social","e"}: inherited learning parameters (fixed for life)
"""
import argparse
import json
import random
import socket
import socketserver
import sys
import threading
import time

ACTIONS = ["forage", "explore", "follow", "avoid", "signal", "rest", "modify"]   # enum order in the sim
BINS = ["LOW", "MID", "HIGH"]

PERCEPT_FIELDS = {
    "energy": "current energy",
    "max_energy": "species maximum",
    "resource_known": "a stocked patch of this species' food is within sense range",
    "resource_loc": "[x, y] of that patch, or null",
    "resource_dist": "distance to it (uu), or null",
    "resource_dir": "[dx, dy] unit vector toward it, or null",
    "resource_stock": "its stock (energy units), 0 if none",
    "same_species_in_range": "neighbours of the same species within social range",
    "other_species_in_range": "neighbours of the other species",
    "neighbour_known": "same_species_in_range > 0",
    "neighbour_centroid": "[x, y] mean position of same-species neighbours, or null",
    "nearest_any_agent_dist": "distance to the closest organism of either species, or null",
    "signal_known": "a Lumen signal received in the last 12 s (follow goes there)",
    "signal_loc": "[x, y] the signalled resource, or null",
    "trace_x": "local Trace X (Lumen information marker, 0..1)",
    "trace_y": "local Trace Y (Tecton soil work, 0..1)",
    "trace_x_gradient": "there is an uphill direction in Trace X",
    "trace_x_gradient_dir": "[dx, dy] unit vector uphill, or null",
    "on_land": "above the water level (Tecton modify needs it)",
    "patch_in_cell_needs_soil": "Tecton: a patch in this trace cell is below half stock",
}


# ---------------------------------------------------------------------------
# Agent policies. One instance per organism (created on its first decision).
#   act(obs, mask)  -> action name (str) or index (int); obs is the agent entry above
#   learn(obs, action, reward) -> called with the obs you acted on, the action you returned
#                                 and the reward the sim credited for it (one decision later)
# Keep both fast: every organism in a request is handled before the reply is sent.
# ---------------------------------------------------------------------------
class RandomAgent:
    """Uniform choice among feasible actions. The floor any learner should beat."""

    def __init__(self, obs):
        self.rng = random.Random(obs["id"])

    def act(self, obs, mask):
        feasible = [a for a, m in zip(ACTIONS, mask) if m]
        return self.rng.choice(feasible) if feasible else "rest"

    def learn(self, obs, action, reward):
        pass


class BanditAgent:
    """Tabular contextual bandit, mirrors DESIGN.md section 1 (gamma = 0, NOT Q-learning).

    context c = energy bin (LOW/MID/HIGH), actions a = the 7 names.
    Q[c][a] += alpha * (r - Q[c][a]); epsilon-greedy over the FEASIBLE actions only.
    alpha and epsilon come from the organism's inherited genome, so evolution in the sim
    still shapes this policy exactly as it shapes the built-in one. Q starts Uniform(0, 0.05).
    """

    def __init__(self, obs):
        self.rng = random.Random(obs["id"] * 7919)
        self.q = [[self.rng.uniform(0.0, 0.05) for _ in ACTIONS] for _ in BINS]

    def act(self, obs, mask):
        c = obs["bin_index"]
        feasible = [i for i, m in enumerate(mask) if m]
        if not feasible:
            return "rest"
        if self.rng.random() < obs["genome"]["epsilon"]:
            return ACTIONS[self.rng.choice(feasible)]
        best = max(feasible, key=lambda i: (self.q[c][i], -i))   # ties -> lowest index, like the sim
        return ACTIONS[best]

    def learn(self, obs, action, reward):
        c = obs["bin_index"]
        i = ACTIONS.index(action)
        self.q[c][i] += obs["genome"]["alpha"] * (reward - self.q[c][i])


class HeuristicAgent:
    """Fixed if/else rules on the percept, no learning. A readable non-random baseline.

    Order of the rules (first match wins; every rule is gated by the mask):
      1. forage   a resource is known and energy is not HIGH
      2. avoid    energy is LOW and other-species neighbours are in range (mask allows avoid
                  only when the closest organism is within CrowdRadius)
      3. rest     energy is LOW and no resource is known
      4. explore  otherwise (always feasible)
    Percept field names are exactly those the sim writes (docs/POLICY_API.md, SWWorldManager.cpp).
    """

    def __init__(self, obs):
        pass

    def act(self, obs, mask):
        p = obs["percept"]
        b = obs["bin"]
        if p["resource_known"] and b != "HIGH" and mask[ACTIONS.index("forage")]:
            return "forage"
        if b == "LOW" and p["other_species_in_range"] > 0 and mask[ACTIONS.index("avoid")]:
            return "avoid"
        if b == "LOW" and not p["resource_known"] and mask[ACTIONS.index("rest")]:
            return "rest"
        return "explore"

    def learn(self, obs, action, reward):
        pass


class TraceFollowerAgent:
    """Uses the Trace X / Trace Y fields (DESIGN.md section 4), per species, no learning.

    Lumen:  follow when the local Trace X gradient is strong (trace_x_gradient and
            trace_x >= TRACE_X_STRONG; the sim itself only climbs above TraceXFollowMin = 0.08).
            Note the mask allows follow only with a same-species neighbour or a fresh signal,
            and the sim's follow goes to the signal, then the neighbour centroid, and climbs the
            gradient only when both are gone (SWAgent.cpp, ESWAction::Follow).
    Tecton: modify (deposit Trace Y) when on land and energy is HIGH (mask: on land, > 25 % energy).
    Both:   else forage when a resource is known and energy is not HIGH, else explore.
    """

    TRACE_X_STRONG = 0.15

    def __init__(self, obs):
        pass

    def act(self, obs, mask):
        p = obs["percept"]
        b = obs["bin"]
        if obs["species"] == "Lumen":
            if p["trace_x_gradient"] and p["trace_x"] >= self.TRACE_X_STRONG and mask[ACTIONS.index("follow")]:
                return "follow"
        else:   # Tecton
            if p["on_land"] and b == "HIGH" and mask[ACTIONS.index("modify")]:
                return "modify"
        if p["resource_known"] and b != "HIGH" and mask[ACTIONS.index("forage")]:
            return "forage"
        return "explore"

    def learn(self, obs, action, reward):
        pass


class MyAgent:
    """>>> YOUR AGENT GOES HERE (select it with --agent my). <<<

    Rules of the game:
      * return one of the names in ACTIONS (or its index 0..6) whose mask entry is 1;
        anything else makes the sim fall back to the organism's built-in bandit for
        that decision (counted as a fallback in population.csv).
      * obs["percept"] tells you what the organism senses; obs["q"] is what its own
        built-in learner currently believes (a free hint), obs["genome"] its inherited
        alpha / epsilon / social / e.
      * learn() arrives one decision later with the exact reward of your last action.
      * Be quick: act() runs for every organism in a request before the reply goes out.
    """

    def __init__(self, obs):
        self.rng = random.Random(obs["id"])

    def act(self, obs, mask):
        p = obs["percept"]
        # Example heuristic: eat when food is near and energy is not high, otherwise wander.
        if mask[ACTIONS.index("forage")] and obs["bin"] != "HIGH":
            return "forage"
        if mask[ACTIONS.index("rest")] and obs["bin"] == "HIGH" and p["resource_known"]:
            return "rest"
        return "explore"

    def learn(self, obs, action, reward):
        pass


AGENTS = {"random": RandomAgent, "bandit": BanditAgent, "heuristic": HeuristicAgent,
          "tracefollower": TraceFollowerAgent, "my": MyAgent}


# ---------------------------------------------------------------------------
# Server plumbing (you should not need to touch this)
# ---------------------------------------------------------------------------
class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.decisions = 0
        self.requests = 0
        self.act_seconds = 0.0
        self.fallback_hint = 0     # actions we returned that were not in the mask (our own bug)

    def add(self, n_decisions, seconds):
        with self.lock:
            self.decisions += n_decisions
            self.requests += 1
            self.act_seconds += seconds

    def snapshot_and_reset(self):
        with self.lock:
            s = (self.decisions, self.requests, self.act_seconds)
            self.decisions = self.requests = 0
            self.act_seconds = 0.0
            return s


STATS = Stats()
AGENT_CLASS = BanditAgent
VERBOSE = False
RECORD = None                 # open file from --record, or None; one JSON line per decide/actions exchange
RECORD_LOCK = threading.Lock()


def record_exchange(decide_msg, reply_msg):
    """Append {"t_wall", "decide", "actions"} as one line (Tools/policy_replay.py reads these)."""
    if RECORD is None:
        return
    line = json.dumps({"t_wall": time.time(), "decide": decide_msg, "actions": reply_msg}, separators=(",", ":"))
    with RECORD_LOCK:
        RECORD.write(line + "\n")
        RECORD.flush()


class Handler(socketserver.StreamRequestHandler):
    """One thread per connected sim. State (policies, last obs) is per connection."""

    def setup(self):
        super().setup()
        self.request.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.policies = {}     # organism id -> agent instance
        self.last_obs = {}     # organism id -> obs we acted on last time (for learn())
        self.last_seen = {}    # organism id -> sim time of its last request (to forget the dead)
        self.hello = None

    def send(self, obj):
        self.wfile.write((json.dumps(obj, separators=(",", ":")) + "\n").encode("utf-8"))
        self.wfile.flush()

    def handle(self):
        peer = "%s:%d" % self.client_address
        print(f"[{peer}] sim connected", flush=True)
        try:
            for raw in self.rfile:
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                try:
                    msg = json.loads(line)
                except json.JSONDecodeError as e:
                    print(f"[{peer}] bad JSON: {e}", flush=True)
                    continue
                t = msg.get("type")
                if t == "hello":
                    self.on_hello(peer, msg)
                elif t == "decide":
                    self.on_decide(msg)
                else:
                    print(f"[{peer}] unknown message type {t!r}", flush=True)
        except (ConnectionResetError, BrokenPipeError):
            pass
        finally:
            print(f"[{peer}] sim disconnected ({len(self.policies)} organisms seen)", flush=True)

    def on_hello(self, peer, msg):
        self.hello = msg
        assert msg.get("protocol") == 1, "unexpected protocol version"
        assert msg.get("actions") == ACTIONS, "action list changed; update ACTIONS"
        print(f"[{peer}] hello: run {msg.get('run_id')} seed {msg.get('seed')} mode {msg.get('mode')} "
              f"controls {msg.get('controls')} timeout {msg.get('timeout_ms')} ms", flush=True)
        # New run (same connection after a reset): forget the previous population.
        self.policies.clear(); self.last_obs.clear(); self.last_seen.clear()
        self.send({"type": "log", "text": f"policy_server: agent={AGENT_CLASS.__name__} ready for {msg.get('controls')}"})

    def on_decide(self, msg):
        t0 = time.perf_counter()
        actions = {}
        now = msg.get("t", 0.0)
        for obs in msg.get("agents", []):
            aid = obs["id"]
            pol = self.policies.get(aid)
            if pol is None:
                pol = self.policies[aid] = AGENT_CLASS(obs)
            # Credit the previous decision: exact reward for the action that just ended.
            prev = self.last_obs.get(aid)
            if prev is not None and obs.get("last_action") is not None and obs.get("last_reward") is not None:
                try:
                    pol.learn(prev, obs["last_action"], float(obs["last_reward"]))
                except Exception as e:   # your learn() must never take the sim down
                    print(f"learn() raised for organism {aid}: {e!r}", flush=True)
            mask = obs.get("mask", [1] * len(ACTIONS))
            try:
                a = pol.act(obs, mask)
            except Exception as e:
                print(f"act() raised for organism {aid}: {e!r}; falling back", flush=True)
                a = None
            if isinstance(a, int) and 0 <= a < len(ACTIONS):
                a = ACTIONS[a]
            if a in ACTIONS:
                if not mask[ACTIONS.index(a)]:
                    STATS.fallback_hint += 1
                actions[str(aid)] = a
                self.last_obs[aid] = obs
            self.last_seen[aid] = now
        reply = {"type": "actions", "step": msg.get("step"), "actions": actions}
        self.send(reply)
        STATS.add(len(actions), time.perf_counter() - t0)
        record_exchange(msg, reply)
        if VERBOSE:
            print(f"t={now:.1f} step={msg.get('step')} {len(actions)} actions in {(time.perf_counter() - t0) * 1000:.2f} ms", flush=True)
        # Forget organisms not seen for 30 logical seconds (they died).
        if len(self.last_seen) > 64 and msg.get("step", 0) % 500 == 0:
            dead = [i for i, ts in self.last_seen.items() if now - ts > 30.0]
            for i in dead:
                self.policies.pop(i, None); self.last_obs.pop(i, None); self.last_seen.pop(i, None)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def stats_loop(period):
    while True:
        time.sleep(period)
        d, r, secs = STATS.snapshot_and_reset()
        if r == 0:
            continue
        print(f"{d / period:8.1f} decisions/s   {r / period:6.1f} requests/s   mean response {secs / r * 1000:.2f} ms   "
              f"({d / r:.1f} organisms/request)" + (f"   WARNING {STATS.fallback_hint} infeasible actions returned" if STATS.fallback_hint else ""),
              flush=True)


def main():
    global AGENT_CLASS, VERBOSE, RECORD
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--agent", choices=sorted(AGENTS), default="bandit", help="which policy class drives the organisms")
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--host", default="0.0.0.0", help="bind address (0.0.0.0 = reachable from the LAN)")
    ap.add_argument("--stats-every", type=float, default=5.0, help="seconds between throughput lines")
    ap.add_argument("--verbose", action="store_true", help="print one line per request")
    ap.add_argument("--record", default=None, metavar="PATH",
                    help="append one JSON line per exchange {t_wall, decide, actions}; replay offline with Tools/policy_replay.py")
    args = ap.parse_args()
    AGENT_CLASS = AGENTS[args.agent]
    VERBOSE = args.verbose
    if args.record:
        RECORD = open(args.record, "a", encoding="utf-8")
        print(f"policy_server: recording every exchange to {args.record}", flush=True)
    threading.Thread(target=stats_loop, args=(args.stats_every,), daemon=True).start()
    try:
        with Server((args.host, args.port), Handler) as srv:
            print(f"policy_server: agent={AGENT_CLASS.__name__} listening on {args.host}:{args.port}  (Ctrl-C to stop)", flush=True)
            try:
                srv.serve_forever()
            except KeyboardInterrupt:
                print("bye", flush=True)
    finally:
        if RECORD is not None:
            with RECORD_LOCK:
                RECORD.close()
                RECORD = None


if __name__ == "__main__":
    main()
