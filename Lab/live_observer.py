#!/usr/bin/env python3
"""The scientists join the LIVE simulator — as observers, through the policy bridge.

Speaks the protocol in docs/POLICY_API.md, but never drives an organism: every
`decide` gets an instant empty `actions` reply, and by the documented fallback
rule ("the reply has no entry for that organism") each organism keeps choosing
with its own built-in bandit. The sim's behavior is untouched; the lab gets the
full live stream — per-organism percepts, Q tables, genomes, rewards — and
distills it into citable evidence windows in lab.sqlite while meetings run
against the same database.

    python -m Lab.lab observe --port 9000            # on this machine
then on the sim host:
    python Tools/run_sim.py --mode C --seed 7 --duration 900 --speed 20 --policy "<this ip>:9000=Both"

Non-goal (PRD): controlling individual creatures. This bridge cannot — it
replies with no actions by construction.
"""
import json
import socket
import socketserver
import threading

from . import config, db
from .population_manager import PopulationManager

WINDOW_S = 60.0     # sim-seconds per evidence window


class _Window:
    def __init__(self, index):
        self.index = index
        self.ids = {"Lumen": set(), "Tecton": set()}
        self.alpha = {"Lumen": [], "Tecton": []}
        self.epsilon = {"Lumen": [], "Tecton": []}
        self.env_e = {"Lumen": [], "Tecton": []}
        self.rewards = {"Lumen": [], "Tecton": []}
        self.trace_x, self.trace_y = [], []

    def add(self, a):
        sp = a.get("species")
        if sp not in self.ids:
            return
        aid = a.get("id")
        g = a.get("genome") or {}
        p = a.get("percept") or {}
        if aid not in self.ids[sp]:          # one genome sample per organism per window
            self.ids[sp].add(aid)
            if "alpha" in g:
                self.alpha[sp].append(g["alpha"])
                self.epsilon[sp].append(g["epsilon"])
                self.env_e[sp].append(g.get("e", 0.5))
        if a.get("last_reward") is not None:
            self.rewards[sp].append(a["last_reward"])
        if "trace_x" in p:
            self.trace_x.append(p["trace_x"])
            self.trace_y.append(p["trace_y"])

    def stats(self, run_id, t0, t1):
        prov = f"live policy bridge, run {run_id}, sim window {t0:.0f}-{t1:.0f}s"
        out = []
        for sp in ("Lumen", "Tecton"):
            s = sp.lower()
            n = len(self.ids[sp])
            out.append((f"live_{s}_n", float(n), f"distinct {sp} deciding — {prov}"))
            if self.alpha[sp]:
                out.append((f"live_{s}_mean_alpha",
                            sum(self.alpha[sp]) / len(self.alpha[sp]),
                            f"mean inherited alpha of live {sp} — {prov}"))
                out.append((f"live_{s}_mean_epsilon",
                            sum(self.epsilon[sp]) / len(self.epsilon[sp]),
                            f"mean inherited epsilon of live {sp} — {prov}"))
                out.append((f"live_{s}_mean_e",
                            sum(self.env_e[sp]) / len(self.env_e[sp]),
                            f"mean inherited e of live {sp} — {prov}"))
            if self.rewards[sp]:
                out.append((f"live_{s}_mean_reward",
                            sum(self.rewards[sp]) / len(self.rewards[sp]),
                            f"mean per-decision reward of live {sp} — {prov}"))
        if self.trace_x:
            out.append(("live_trace_x_mean", sum(self.trace_x) / len(self.trace_x),
                        f"mean local Trace X at decision points — {prov}"))
            out.append(("live_trace_y_mean", sum(self.trace_y) / len(self.trace_y),
                        f"mean local Trace Y at decision points — {prov}"))
        return out


class ObserverHandler(socketserver.StreamRequestHandler):
    def handle(self):
        srv = self.server
        peer = f"{self.client_address[0]}:{self.client_address[1]}"
        print(f"[observe] sim connected from {peer}")
        run_id, window, field = None, None, None
        last_team_sent = -1.0
        manager = PopulationManager() if srv.manage else None
        con = db.connect(srv.lab_db_path)     # this thread's own connection
        try:
            for raw in self.rfile:
                try:
                    msg = json.loads(raw)
                except json.JSONDecodeError:
                    continue
                mtype = msg.get("type")

                if mtype == "hello":
                    run_id = msg.get("run_id", "live")
                    window = _Window(0)
                    if srv.embody:
                        from .embodiment import EmbodiedField
                        field = EmbodiedField(run_id, msg.get("world_half_size"))
                    if manager:
                        manager.load_doctrine(con)
                    print(f"[observe] hello: run {run_id}, mode {msg.get('mode_name')}, "
                          f"seed {msg.get('seed')}, controls {msg.get('controls')}")
                    if not con.execute("SELECT 1 FROM runs WHERE run_id=?",
                                       (f"live:{run_id}",)).fetchone():
                        con.execute("INSERT INTO runs VALUES(?,?,?,?,?,?)",
                                    (f"live:{run_id}", msg.get("mode_name", "?"),
                                     int(msg.get("seed", -1)), 0.0, "policy-bridge", db.now()))
                        con.commit()
                    self._send({"type": "log",
                                "text": ("Symbiotic Lab connected: population management ON"
                                         if manager else
                                         "Symbiotic Lab connected: observing only, "
                                         "no actions will be driven")})

                elif mtype == "decide":
                    agents = msg.get("agents", [])
                    t = float(msg.get("t", 0.0))
                    actions = {}
                    if manager:
                        manager.observe_cohort(t, agents)
                        for a in agents:
                            act = manager.act(a)
                            if act is not None:
                                actions[str(a.get("id"))] = act
                    # Reply first — the sim must never wait on the lab.
                    self._send({"type": "actions", "step": msg.get("step"),
                                "actions": actions})
                    if window is None:
                        continue
                    if field:
                        field.step(t, agents)
                        # Avatar layer: report the team's positions at most twice
                        # per sim-second. Old sim builds ignore the message type;
                        # organisms never see it (visual only, docs/POLICY_API.md).
                        if t - last_team_sent >= 0.5:
                            last_team_sent = t
                            self._send({"type": "scientists",
                                        "team": field.team_positions()})
                    k = int(t // WINDOW_S)
                    if k > window.index:
                        self._flush(con, run_id, window)
                        if field:
                            note = field.flush(con, window.index,
                                               window.index * WINDOW_S,
                                               (window.index + 1) * WINDOW_S)
                            print(f"[observe] {note}")
                            self._send({"type": "log", "text": note})
                        if manager:
                            changed = manager.load_doctrine(con)   # scientists' latest
                            if changed:
                                self._send({"type": "log",
                                            "text": "doctrine update: " + ", ".join(changed)})
                            self._send({"type": "log", "text": manager.summary()})
                        window = _Window(k)
                    for a in agents:
                        window.add(a)
        finally:
            if window is not None and run_id:
                self._flush(con, run_id, window)
                if field:
                    field.flush(con, window.index, window.index * WINDOW_S,
                                (window.index + 1) * WINDOW_S)
            con.close()
            print(f"[observe] sim disconnected ({peer})")

    def _send(self, obj):
        try:
            self.wfile.write((json.dumps(obj) + "\n").encode())
            self.wfile.flush()
        except OSError:
            pass   # peer gone (run over); the final window flush still lands in the DB

    def _flush(self, con, run_id, window):
        t0, t1 = window.index * WINDOW_S, (window.index + 1) * WINDOW_S
        stats = window.stats(run_id, t0, t1)
        if not stats:
            return
        wid = f"live:{run_id}:w{window.index}"
        for stat, value, prov in stats:
            db.add_evidence(con, wid, stat, float(value), prov)
        con.execute("UPDATE runs SET sim_end=? WHERE run_id=?", (t1, f"live:{run_id}"))
        con.commit()
        lum = next((v for s, v, _ in stats if s == "live_lumen_n"), 0)
        tec = next((v for s, v, _ in stats if s == "live_tecton_n"), 0)
        print(f"[observe] window {window.index} ({t0:.0f}-{t1:.0f}s): "
              f"{lum:.0f} Lumen, {tec:.0f} Tecton -> {len(stats)} evidence objects")
        self._send({"type": "log",
                    "text": f"lab logged window {window.index}: "
                            f"{lum:.0f} Lumen / {tec:.0f} Tecton"})


class _Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def my_lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "<this machine's ip>"
    finally:
        s.close()


def serve(db_path=None, port=9000, host="0.0.0.0", manage=False, embody=False):
    srv = _Server((host, port), ObserverHandler)
    srv.lab_db_path = str(db_path or config.DB_PATH)
    srv.manage = manage
    srv.embody = embody
    ip = my_lan_ip()
    role = "POPULATION MANAGER (driving organisms)" if manage else "observer (read-only)"
    if embody:
        role += " + EMBODIED FIELD TEAM (seven walking observers, witnessed-only evidence)"
    print(f"Symbiotic Lab live bridge on {host}:{port} — {role} (db: {srv.lab_db_path})")
    print("On the sim host, attach the lab to a run with:")
    print(f'  python Tools/run_sim.py --mode C --seed 7 --duration 900 --speed 20 '
          f'--policy "{ip}:{port}=Both"')
    if manage:
        print("Managed organisms follow the lab's population doctrine "
              "(Lab/population_manager.py); infeasible or missing replies fall "
              "back to each organism's own bandit.")
    else:
        print("The lab replies with no actions: organisms keep their built-in bandits; "
              "the lab only reads.")
    print("Run meetings on the same DB meanwhile: python -m Lab.lab session --meetings 1")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
