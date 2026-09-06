#!/usr/bin/env python3
"""Replay a recorded policy-server session against an agent class, without the sim.

    python3 Tools/policy_replay.py --agent my --file docs/samples/decide_sample.jsonl
    python3 Tools/policy_replay.py --agent bandit --file my_run.jsonl --limit 100

The recording comes from `python3 Tools/policy_server.py --record PATH` on a machine that
was connected to the sim (one JSON line per exchange: {"t_wall", "decide", "actions"}).
This script feeds every recorded "decide" message to YOUR agent exactly the way the server
does (one agent instance per organism id, act(obs, mask) per organism, learn() one decision
later with the recorded last_action / last_reward) and reports:

  * decisions replayed and decisions per second (your act()+learn() throughput)
  * action distribution: your agent vs the agent that was recorded
  * mean recorded reward by recorded action (the reward the sim credited to last_action)
  * mask violations (an action your agent returned that was not feasible; must be 0,
    each one would be a fallback to the organism's built-in bandit in the real sim)
  * the slowest single act() call (keep the whole request under a few ms; see POLICY_API.md)

What it cannot do: the recording is a fixed trace, so your choices do not change what the
next observation looks like (off-policy replay). learn() therefore receives the reward of the
RECORDED action, not of yours: a learning agent sees realistic (obs, action, reward) triples
but is not evaluated closed-loop. That needs the sim (ask the host to point it at you).

Standard library only, Python 3.9+. Imports the agent classes from Tools/policy_server.py.
"""
import argparse
import json
import os
import sys
import time
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from policy_server import ACTIONS, AGENTS   # noqa: E402  (side-effect free: the server starts only under __main__)


def to_name(a):
    """Normalise an action value (name, index, numeric string) to a name, or None."""
    if isinstance(a, bool):
        return None
    if isinstance(a, int) and 0 <= a < len(ACTIONS):
        return ACTIONS[a]
    if isinstance(a, str):
        if a in ACTIONS:
            return a
        if a.isdigit() and 0 <= int(a) < len(ACTIONS):
            return ACTIONS[int(a)]
    return None


def load(path, limit):
    exchanges, bad = [], 0
    with open(path, "r", encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = raw.strip()
            if not line:
                continue
            try:
                ex = json.loads(line)
                if ex.get("decide", {}).get("type") != "decide":
                    raise ValueError("no decide message")
            except (ValueError, AttributeError) as e:
                bad += 1
                print(f"line {n}: skipped ({e})", file=sys.stderr)
                continue
            exchanges.append(ex)
            if limit and len(exchanges) >= limit:
                break
    return exchanges, bad


def hist_line(title, counts, total):
    cells = []
    for a in ACTIONS:
        n = counts.get(a, 0)
        cells.append(f"{a} {n:5d} ({(100.0 * n / total if total else 0.0):5.1f}%)")
    return f"{title:<10}" + "  ".join(cells)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--agent", choices=sorted(AGENTS), default="my", help="agent class from policy_server.py to replay")
    ap.add_argument("--file", required=True, metavar="PATH", help="JSONL recording written by policy_server.py --record")
    ap.add_argument("--limit", type=int, default=0, metavar="N", help="replay only the first N exchanges (0 = all)")
    args = ap.parse_args()

    agent_class = AGENTS[args.agent]
    exchanges, bad = load(args.file, args.limit)
    if not exchanges:
        sys.exit(f"no exchanges in {args.file}")

    policies, last_obs = {}, {}          # organism id -> agent instance / obs we acted on (same as the server)
    mine, recorded = Counter(), Counter()
    reward_sum, reward_n = defaultdict(float), Counter()
    violations, invalid, agree, learn_calls = 0, 0, 0, 0
    slowest = (0.0, None, None)          # seconds, organism id, step
    decisions = 0
    organisms = set()
    species = Counter()
    t_first = exchanges[0]["decide"].get("t", 0.0)
    t_last = exchanges[-1]["decide"].get("t", 0.0)
    learn_errors, act_errors = 0, 0

    t_start = time.perf_counter()
    for ex in exchanges:
        msg = ex["decide"]
        rec_actions = ex.get("actions", {}).get("actions", {})
        step = msg.get("step")
        for obs in msg.get("agents", []):
            aid = obs["id"]
            organisms.add(aid)
            species[obs.get("species", "?")] += 1
            pol = policies.get(aid)
            if pol is None:
                pol = policies[aid] = agent_class(obs)
            # Credit the previous decision exactly like the server: the sim's last_action / last_reward.
            prev = last_obs.get(aid)
            la, lr = obs.get("last_action"), obs.get("last_reward")
            if la is not None and lr is not None:
                reward_sum[la] += float(lr)
                reward_n[la] += 1
                if prev is not None:
                    try:
                        pol.learn(prev, la, float(lr))
                        learn_calls += 1
                    except Exception as e:
                        learn_errors += 1
                        if learn_errors <= 3:
                            print(f"learn() raised for organism {aid} at step {step}: {e!r}", file=sys.stderr)
            mask = obs.get("mask", [1] * len(ACTIONS))
            t0 = time.perf_counter()
            try:
                a = pol.act(obs, mask)
            except Exception as e:
                a = None
                act_errors += 1
                if act_errors <= 3:
                    print(f"act() raised for organism {aid} at step {step}: {e!r}", file=sys.stderr)
            dt = time.perf_counter() - t0
            if dt > slowest[0]:
                slowest = (dt, aid, step)
            decisions += 1
            name = to_name(a)
            if name is None:
                invalid += 1
            else:
                mine[name] += 1
                if not mask[ACTIONS.index(name)]:
                    violations += 1
                last_obs[aid] = obs
            rec = to_name(rec_actions.get(str(aid)))
            if rec is not None:
                recorded[rec] += 1
                if rec == name:
                    agree += 1
    elapsed = time.perf_counter() - t_start

    print(f"file        {args.file}  ({len(exchanges)} exchanges" + (f", {bad} bad lines skipped" if bad else "") + ")")
    print(f"agent       {agent_class.__name__} (--agent {args.agent})  vs recorded replies in the file")
    print(f"sim time    t={t_first:.1f} .. {t_last:.1f} s, {len(organisms)} organisms "
          + ", ".join(f"{k} {v}" for k, v in sorted(species.items())) + " (decisions)")
    print(f"decisions   {decisions}   in {elapsed * 1000:.1f} ms wall   = {decisions / elapsed if elapsed else 0.0:,.0f} decisions/s   "
          f"(learn() calls {learn_calls})")
    print(hist_line("yours", mine, sum(mine.values())))
    print(hist_line("recorded", recorded, sum(recorded.values())))
    rec_total = sum(recorded.values())
    print(f"agreement   {agree} / {rec_total} decisions same as recorded ({(100.0 * agree / rec_total if rec_total else 0.0):.1f}%)")
    print("mean recorded reward by recorded action (last_action -> last_reward, sim credited):")
    for a in ACTIONS:
        if reward_n[a]:
            print(f"    {a:<8} {reward_sum[a] / reward_n[a]:+.4f}  (n={reward_n[a]})")
    print(f"mask violations {violations}" + ("   <-- FIX: these would fall back to the built-in bandit" if violations else "   (OK)"))
    if invalid:
        print(f"invalid returns {invalid}   <-- FIX: act() returned something that is not an action name or index 0..6")
    if act_errors or learn_errors:
        print(f"exceptions      act() {act_errors}, learn() {learn_errors}   <-- FIX")
    print(f"slowest act()   {slowest[0] * 1e6:.0f} us  (organism {slowest[1]}, step {slowest[2]})")
    return 0 if (violations == 0 and invalid == 0 and act_errors == 0 and learn_errors == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
