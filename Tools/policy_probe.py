#!/usr/bin/env python3
"""Find policy servers on the wifi and print ready-to-paste server-file lines.

    python3 Tools/policy_probe.py                                   # scan this machine's own /24 for port 9000
    python3 Tools/policy_probe.py --subnet 10.228.152 --port 9000   # a given /24
    python3 Tools/policy_probe.py --write Saved/policy_servers.txt  # append the conforming hosts to the server file

Scans every address of a /24 for one TCP port. For each open port it does what the sim does:
sends the "hello", then one "decide" with a single fake organism, and waits for an "actions"
reply (1.5 s timeout for connect, and again for the reply). Hosts whose reply is a valid,
feasible action are printed as

    host:port=Both   # replied in X ms

which is exactly a line of the server list file the running sim watches (docs/POLICY_API.md,
"Adding servers while the sim runs"). Open ports that do not speak the protocol are listed
separately. --write PATH appends the conforming lines to that file, never duplicating a
host:port that is already there (comments and species are left alone).

Standard library only, Python 3.9+, macOS/Linux/Windows.
"""
import argparse
import json
import os
import socket
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ACTIONS = ["forage", "explore", "follow", "avoid", "signal", "rest", "modify"]

HELLO = {
    "type": "hello", "protocol": 1,
    "actions": ACTIONS,
    "bins": ["LOW", "MID", "HIGH"], "species": ["Lumen", "Tecton"], "controls": ["Lumen", "Tecton"],
    "seed": 0, "mode": "C", "mode_name": "C_learning_evolution", "run_id": "policy_probe",
    "decision_interval": 1.0, "substep": 0.1, "timeout_ms": 200, "share": 1.0, "world_half_size": 4500.0,
    "max_energy": {"Lumen": 100.0, "Tecton": 160.0}, "max_age": {"Lumen": 150.0, "Tecton": 300.0},
    "learning": "tabular contextual bandit, gamma 0; the sim keeps updating each organism's own table with every reward",
}

# One fake Lumen at MID energy with a known resource: every action but "avoid" is feasible.
MASK = [1, 1, 1, 0, 1, 1, 1]
ORGANISM = {
    "id": 1, "species": "Lumen", "generation": 0, "age": 5.0, "energy": 55.0, "max_energy": 100.0,
    "bin": "MID", "bin_index": 1, "mask": MASK,
    "last_action": None, "last_reward": None, "last_bin": None, "last_external": False, "decisions": 0,
    "q": [[0.0] * 7 for _ in range(3)], "position": [0.0, 0.0], "heading": 0.0,
    "percept": {
        "energy": 55.0, "max_energy": 100.0,
        "resource_known": True, "resource_loc": [300.0, 400.0], "resource_dist": 500.0, "resource_dir": [0.6, 0.8], "resource_stock": 80.0,
        "same_species_in_range": 1, "other_species_in_range": 0, "neighbour_known": True, "neighbour_centroid": [-200.0, 100.0],
        "nearest_any_agent_dist": 223.6,
        "signal_known": False, "signal_loc": None,
        "trace_x": 0.0, "trace_y": 0.0, "trace_x_gradient": False, "trace_x_gradient_dir": None,
        "on_land": True, "patch_in_cell_needs_soil": False,
    },
    "genome": {"alpha": 0.1, "epsilon": 0.2, "social": 0.5, "e": 0.5},
}


def own_subnet():
    """First three octets of the address this machine uses for its default route (no packet is sent)."""
    ip = None
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.connect(("8.8.8.8", 80))     # UDP connect only picks the outgoing interface
            ip = s.getsockname()[0]
        finally:
            s.close()
    except OSError:
        pass
    if not ip or ip.startswith("127."):
        try:
            ip = socket.gethostbyname(socket.gethostname())
        except OSError:
            ip = None
    if not ip or ip.startswith("127."):
        return None
    return ".".join(ip.split(".")[:3])


def probe(host, port, timeout):
    """Returns (status, detail_ms_or_reason). status: 'ok' | 'bad' | 'closed'."""
    try:
        s = socket.create_connection((host, port), timeout=timeout)
    except OSError:
        return "closed", None
    try:
        s.settimeout(timeout)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        f = s.makefile("rwb")

        def send(obj):
            f.write((json.dumps(obj, separators=(",", ":")) + "\n").encode("utf-8"))
            f.flush()

        send(HELLO)
        step = 7
        req = {"type": "decide", "t": 1.0, "step": step, "server": f"{host}:{port}", "agents": [ORGANISM]}
        t0 = time.perf_counter()
        send(req)
        deadline = t0 + timeout
        while True:
            if time.perf_counter() > deadline:
                return "bad", "open, but no 'actions' reply within %.1f s" % timeout
            try:
                line = f.readline()
            except (socket.timeout, OSError):
                return "bad", "open, but no 'actions' reply within %.1f s" % timeout
            if not line:
                return "bad", "open, but closed the connection without an 'actions' reply"
            try:
                msg = json.loads(line.decode("utf-8", errors="replace"))
            except ValueError:
                return "bad", "open, but sent something that is not JSON"
            if not isinstance(msg, dict):
                return "bad", "open, but sent a JSON value that is not an object"
            if msg.get("type") == "log":
                continue
            if msg.get("type") != "actions":
                return "bad", "open, but replied with type %r instead of 'actions'" % msg.get("type")
            if msg.get("step") not in (None, step):
                return "bad", "replied to a different step (%r); the sim would discard it as stale" % msg.get("step")
            ms = (time.perf_counter() - t0) * 1000.0
            act = (msg.get("actions") or {}).get("1")
            if act is None:
                return "bad", "replied, but with no action for the organism (the sim would fall back)"
            if isinstance(act, int) or (isinstance(act, str) and act.strip().isdigit()):
                idx = int(act)
                if not 0 <= idx < len(ACTIONS):
                    return "bad", "replied with action index %r (valid: 0..6)" % act
                act = ACTIONS[idx]
            if act not in ACTIONS:
                return "bad", "replied with unknown action %r" % act
            if not MASK[ACTIONS.index(act)]:
                return "bad", "replied with %r, which the mask marked infeasible (the sim would fall back)" % act
            return "ok", ms
    except OSError as e:
        return "bad", "open, but the exchange failed (%s)" % e
    finally:
        try:
            s.close()
        except OSError:
            pass


def existing_servers(path):
    """host:port keys (lower-case) already listed in a server file; empty if the file is missing."""
    keys = set()
    if not os.path.exists(path):
        return keys
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for raw in fh:
            line = raw.split("#", 1)[0].strip()
            if not line or "=" not in line:
                continue
            keys.add(line.split("=", 1)[0].strip().lower())
    return keys


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--subnet", default=None, metavar="A.B.C", help="the /24 to scan (default: this machine's own, from its default route)")
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--species", default="Both", choices=["Lumen", "Tecton", "Both"], help="species to put on the printed lines (default Both)")
    ap.add_argument("--timeout", type=float, default=1.5, metavar="S", help="connect timeout and reply timeout per host (default 1.5)")
    ap.add_argument("--range", default=None, metavar="LO-HI", help="last-octet range to scan (default 1-254; 127.x.x: 1-1, every address there is this machine)")
    ap.add_argument("--workers", type=int, default=64, help="parallel probes")
    ap.add_argument("--write", default=None, metavar="PATH", help="append the conforming lines to this server file (no duplicates)")
    args = ap.parse_args()

    subnet = args.subnet or own_subnet()
    if not subnet:
        sys.exit("could not detect this machine's subnet; give --subnet A.B.C")
    parts = subnet.strip().rstrip(".").split(".")
    if len(parts) != 3 or not all(p.isdigit() and 0 <= int(p) <= 255 for p in parts):
        sys.exit("--subnet must be the first three octets, e.g. 10.228.152")
    subnet = ".".join(parts)
    lo, hi = 1, 254
    if args.range:
        try:
            lo, hi = (int(x) for x in args.range.split("-", 1))
        except ValueError:
            sys.exit("--range must be LO-HI, e.g. 1-254")
    elif subnet.startswith("127."):
        lo, hi = 1, 1
    lo, hi = max(1, lo), min(254, hi)
    hosts = ["%s.%d" % (subnet, i) for i in range(lo, hi + 1)]
    print("probing %s.%d-%d port %d (%d hosts, %.1f s timeout, %d workers)" % (subnet, lo, hi, args.port, len(hosts), args.timeout, args.workers), flush=True)

    t0 = time.perf_counter()
    with ThreadPoolExecutor(max_workers=max(1, args.workers)) as ex:
        results = list(ex.map(lambda h: (h,) + probe(h, args.port, args.timeout), hosts))
    ok = [(h, ms) for h, st, ms in results if st == "ok"]
    bad = [(h, why) for h, st, why in results if st == "bad"]
    print("scanned in %.1f s: %d open, %d conforming" % (time.perf_counter() - t0, len(ok) + len(bad), len(ok)), flush=True)

    lines = ["%s:%d=%s   # replied in %.1f ms" % (h, args.port, args.species, ms) for h, ms in ok]
    print()
    print("policy servers (paste into the server file, default Saved/policy_servers.txt):")
    for line in lines:
        print("  " + line)
    if not lines:
        print("  (none)")
    if bad:
        print()
        print("open ports that do not speak the policy protocol (not written):")
        for h, why in bad:
            print("  %s:%d  %s" % (h, args.port, why))

    if args.write:
        have = existing_servers(args.write)
        new = [line for line in lines if line.split("=", 1)[0].strip().lower() not in have]
        if new:
            os.makedirs(os.path.dirname(os.path.abspath(args.write)) or ".", exist_ok=True)
            lead = ""
            if os.path.exists(args.write) and os.path.getsize(args.write) > 0:
                with open(args.write, "rb") as fh:
                    fh.seek(-1, os.SEEK_END)
                    lead = "" if fh.read(1) in (b"\n", b"\r") else "\n"   # do not glue onto an unterminated last line
            with open(args.write, "a", encoding="utf-8") as fh:
                fh.write(lead + "".join(line + "\n" for line in new))
        print()
        print("%s: appended %d line(s), %d already listed" % (args.write, len(new), len(lines) - len(new)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
