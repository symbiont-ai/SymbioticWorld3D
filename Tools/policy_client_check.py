#!/usr/bin/env python3
"""Self-test for a policy server without the sim: pretend to be Symbiotic World for one exchange.

    python3 Tools/policy_client_check.py --host 127.0.0.1 --port 9000

Connects, sends a "hello" and one "decide" request with two fake organisms (exactly the
field set the sim sends; see docs/POLICY_API.md), prints whatever comes back and the
round-trip time. Use it on your Mac while `python3 Tools/policy_server.py --agent my`
runs in another terminal, before the host points the real sim at you.
Standard library only.
"""
import argparse
import json
import socket
import time

HELLO = {
    "type": "hello", "protocol": 1,
    "actions": ["forage", "explore", "follow", "avoid", "signal", "rest", "modify"],
    "bins": ["LOW", "MID", "HIGH"], "species": ["Lumen", "Tecton"], "controls": ["Lumen"],
    "seed": 7, "mode": "C", "mode_name": "C_learning_evolution", "run_id": "policy_client_check",
    "decision_interval": 1.0, "substep": 0.1, "timeout_ms": 200, "share": 1.0, "world_half_size": 4500.0,
    "max_energy": {"Lumen": 100.0, "Tecton": 160.0}, "max_age": {"Lumen": 150.0, "Tecton": 300.0},
    "learning": "tabular contextual bandit, gamma 0; the sim keeps updating each organism's own table with every reward",
}


def fake_agent(aid, species, energy, max_energy, with_food, last=None):
    q = [[round(0.01 * ((aid + b * 7 + a) % 5), 4) for a in range(7)] for b in range(3)]
    frac = energy / max_energy
    bin_index = 0 if frac < 0.3333 else (1 if frac < 0.6667 else 2)
    obs = {
        "id": aid, "species": species, "generation": 1, "age": 12.3, "energy": energy, "max_energy": max_energy,
        "bin": ["LOW", "MID", "HIGH"][bin_index], "bin_index": bin_index,
        "mask": [1 if with_food else 0, 1, 1, 0, 1 if (with_food and species == "Lumen") else 0, 1, 1 if with_food else 0],
        "last_action": None, "last_reward": None, "last_bin": None, "last_external": False, "decisions": 12,
        "q": q, "position": [-1200.0, 350.0], "heading": 42.0,
        "percept": {
            "energy": energy, "max_energy": max_energy,
            "resource_known": with_food,
            "resource_loc": [-900.0, 500.0] if with_food else None,
            "resource_dist": 335.4 if with_food else None,
            "resource_dir": [0.8944, 0.4472] if with_food else None,
            "resource_stock": 88.5 if with_food else 0,
            "same_species_in_range": 2, "other_species_in_range": 0,
            "neighbour_known": True, "neighbour_centroid": [-1400.0, 300.0],
            "nearest_any_agent_dist": 410.2,
            "signal_known": False, "signal_loc": None,
            "trace_x": 0.12, "trace_y": 0.0, "trace_x_gradient": True, "trace_x_gradient_dir": [0.7071, 0.7071],
            "on_land": True, "patch_in_cell_needs_soil": False,
        },
        "genome": {"alpha": 0.12, "epsilon": 0.2, "social": 0.5, "e": 0.5},
    }
    if last:
        obs.update({"last_action": last[0], "last_reward": last[1], "last_bin": obs["bin"], "last_external": True})
    return obs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--rounds", type=int, default=3, help="decide requests to send (rewards are faked in between)")
    args = ap.parse_args()

    with socket.create_connection((args.host, args.port), timeout=5.0) as s:
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        f = s.makefile("rwb")

        def send(obj):
            f.write((json.dumps(obj, separators=(",", ":")) + "\n").encode("utf-8"))
            f.flush()

        def recv_until(kind, deadline=2.0):
            s.settimeout(deadline)
            while True:
                line = f.readline()
                if not line:
                    raise SystemExit("server closed the connection")
                msg = json.loads(line.decode("utf-8"))
                if msg.get("type") == "log":
                    print("  server log:", msg.get("text"))
                    continue
                if msg.get("type") == kind:
                    return msg
                print("  unexpected message:", msg)

        print(f"connected to {args.host}:{args.port}; sending hello")
        send(HELLO)
        time.sleep(0.2)   # let an optional log line arrive before the first decide
        last = {}
        ok = True
        for step in range(1, args.rounds + 1):
            agents = [fake_agent(101, "Lumen", 45.0, 100.0, True, last.get(101)),
                      fake_agent(102, "Tecton", 120.0, 160.0, False, last.get(102))]
            req = {"type": "decide", "t": step * 1.0, "step": step * 10, "server": f"{args.host}:{args.port}", "agents": agents}
            t0 = time.perf_counter()
            send(req)
            reply = recv_until("actions")
            dt = (time.perf_counter() - t0) * 1000
            print(f"round {step}: reply in {dt:.2f} ms -> {json.dumps(reply)}")
            if reply.get("step") not in (None, req["step"]):
                print("  WARNING: reply echoes a different step; the sim would discard it as stale")
                ok = False
            for a in agents:
                act = reply.get("actions", {}).get(str(a["id"]))
                if act is None:
                    print(f"  WARNING: no action for organism {a['id']} (sim would fall back to the built-in bandit)")
                    ok = False
                    continue
                names = HELLO["actions"]
                if isinstance(act, int) or (isinstance(act, str) and act.isdigit()):
                    act = names[int(act)] if 0 <= int(act) < 7 else None
                if act not in names or not a["mask"][names.index(act)]:
                    print(f"  WARNING: '{act}' is not feasible for organism {a['id']} (mask {a['mask']}); sim would fall back")
                    ok = False
                last[a["id"]] = (act, round(0.3 if act == "forage" else -0.15, 4))   # fake reward for the next round
        print("OK: the server speaks the protocol" if ok else "problems found, see warnings above")


if __name__ == "__main__":
    main()
