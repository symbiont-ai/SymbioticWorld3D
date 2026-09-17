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
import sqlite3
import threading
import time

from . import config, db, duty
from .population_manager import PopulationManager

WINDOW_S = 60.0     # sim-seconds per evidence window
def _skippable(call, *args, **kwargs):
    """Run a duty read/write that must never hold up the sim.

    The observer answers the live simulator from this thread, and a meeting on another connection
    can hold the write lock for as long as it takes to hold the meeting. A heartbeat or a phase
    advance that cannot get in right now is simply skipped: both are retried on the next cycle, and
    the sim keeps its frame rate. Returns None when it was skipped.
    """
    try:
        return call(*args, **kwargs)
    except sqlite3.OperationalError as ex:
        if "locked" not in str(ex) and "busy" not in str(ex):
            raise
        return None


HEARTBEAT_S = 10.0  # wall seconds between "an observer is live" notes for the duty cycle
PHASE_POLL_S = 1.0  # ... and between reads of the shared duty phase: the sim waits on this thread,
                    # so the phase is cached rather than read once per decide message
LAB_LINE_MAX = 90   # the sim's SYMBIOTIC LAB panel is one short line per report
LAB_POLL_S = 1.0    # how often the lab's own DB is checked for new rows
LAB_SEND_S = 1.0    # at most one report per second, so the panel stays readable


def _short(text, limit=LAB_LINE_MAX):
    t = " ".join(str(text or "").split())
    return t if len(t) <= limit else t[:limit - 3].rstrip() + "..."


class LabActivity:
    """Turn the lab's own new DB rows into one-line reports for the sim's SYMBIOTIC LAB panel.

    Read-only and lock-tolerant on purpose: `python -m Lab.lab session` writes this same file while
    a take is running, so every query is wrapped and a locked (or not yet created) database simply
    means no report this second. Nothing here touches the simulation: the lines travel as the
    documented {"type": "log"} side message (docs/POLICY_API.md) and are drawn by the HUD.
    """

    def __init__(self, db_path):
        self.path = str(db_path)
        self.con = None
        self.queue = []
        self.next_poll = 0.0
        self.next_send = 0.0
        self.primed = False        # the first poll records what is already there and reports none of it
        self.last_meeting = 0
        self.last_turn = 0
        self.hypotheses = {}       # id -> status
        self.experiments = {}      # id -> (status, verdict)
        self.predictions = set()   # (experiment_id, agent)

    # -- plumbing ---------------------------------------------------------
    def _connect(self):
        if self.con is not None:
            return self.con
        try:
            uri = "file:" + self.path.replace("\\", "/").replace(" ", "%20") + "?mode=ro"
            con = sqlite3.connect(uri, uri=True, timeout=2.0)
            con.row_factory = sqlite3.Row
            con.execute("PRAGMA busy_timeout=2000")
            self.con = con
        except sqlite3.Error:
            self.con = None        # not created yet: try again on the next poll
        return self.con

    def _rows(self, sql, args=()):
        con = self._connect()
        if con is None:
            return []
        try:
            return con.execute(sql, args).fetchall()
        except sqlite3.Error:
            # locked by a meeting mid-write, or a table this build does not have: skip this poll
            try:
                self.con.close()
            except sqlite3.Error:
                pass
            self.con = None
            return []

    def _emit(self, text):
        line = _short(text)
        if line:
            self.queue.append(line)
            del self.queue[:-20]   # a long meeting must not build a backlog the panel never drains

    # -- polling ----------------------------------------------------------
    def poll(self, now=None):
        """Read new rows into the queue. Cheap, and never raises."""
        now = now if now is not None else time.monotonic()
        if now < self.next_poll:
            return
        self.next_poll = now + LAB_POLL_S
        first = not self.primed
        self.primed = True

        for r in self._rows("SELECT id, kind FROM meetings WHERE id > ? ORDER BY id", (self.last_meeting,)):
            self.last_meeting = r["id"]
            if not first:
                self._emit("meeting %s opened (%s)" % (r["id"], r["kind"] or "session"))
        for r in self._rows("SELECT id, meeting_id, round, agent, payload FROM transcript "
                            "WHERE id > ? ORDER BY id", (self.last_turn,)):
            self.last_turn = r["id"]
            if first:
                continue
            line = self._turn_line(r)
            if line:
                self._emit(line)
        for r in self._rows("SELECT id, claim, proposer, status FROM hypotheses"):
            hid, status = r["id"], r["status"]
            known = self.hypotheses.get(hid, False)
            self.hypotheses[hid] = status
            if first or known == status:
                continue
            if known is False:
                self._emit("%s proposed by %s: %s" % (hid, r["proposer"] or "the lab", r["claim"]))
            else:
                self._emit("%s now %s: %s" % (hid, status, r["claim"]))
        for r in self._rows("SELECT id, status, verdict, protocol, metric_result FROM experiments"):
            xid = r["id"]
            state = (r["status"], r["verdict"])
            known = self.experiments.get(xid)
            self.experiments[xid] = state
            if first or known == state:
                continue
            self._emit(self._experiment_line(xid, r))
        for r in self._rows("SELECT experiment_id, agent, predicted, confidence FROM predictions"):
            key = (r["experiment_id"], r["agent"])
            if key in self.predictions:
                continue
            self.predictions.add(key)
            if first:
                continue
            self._emit("%s predicts %s for %s (conf %.2f)"
                       % (r["agent"], r["predicted"], r["experiment_id"], float(r["confidence"] or 0.0)))

    @staticmethod
    def _turn_line(row):
        rnd = (row["round"] or "").upper()
        try:
            payload = json.loads(row["payload"] or "null")
        except (TypeError, ValueError):
            payload = None
        head = "meeting %s %s: %s" % (row["meeting_id"], rnd, row["agent"])
        if rnd == "EVIDENCE" and isinstance(payload, list) and payload:
            f = payload[0]
            ids = ", ".join(f.get("evidence_ids", [])[:2])
            return "%s cites %s (%s)" % (head, ids or "no id", f.get("text", ""))
        if rnd in ("DISSENT", "CRUX") and isinstance(payload, dict):
            return "%s %s" % (head, payload.get("text") or payload.get("crux") or payload.get("reason") or "")
        if rnd == "MINUTES" and isinstance(payload, dict):
            return "meeting %s minutes: %s" % (row["meeting_id"], payload.get("summary", ""))
        if rnd in ("REVIEW", "CONSERVATION-REVIEW") and isinstance(payload, dict):
            return "%s review: %s" % (head, payload.get("summary") or payload.get("objections") or "no objection")
        return None

    @staticmethod
    def _experiment_line(xid, row):
        status, verdict = row["status"], row["verdict"]
        if verdict:
            result = ""
            try:
                res = json.loads(row["metric_result"] or "null")
                if isinstance(res, dict):
                    parts = ["%s %.3g" % (k, v) for k, v in list(res.items())[:2]
                             if isinstance(v, (int, float))]
                    result = ", ".join(parts)
            except (TypeError, ValueError):
                pass
            return "%s verdict %s%s" % (xid, str(verdict).upper(), (" (%s)" % result) if result else "")
        try:
            proto = json.loads(row["protocol"] or "{}")
        except (TypeError, ValueError):
            proto = {}
        if isinstance(proto, dict) and proto.get("metric"):
            return "%s %s: %s, %d seeds, threshold %g" % (
                xid, status, proto.get("metric"), len(proto.get("seeds") or []), proto.get("threshold", 0))
        return "%s %s" % (xid, status)

    # -- sending ----------------------------------------------------------
    def due(self, now=None):
        """The next report to send, or None (at most one per LAB_SEND_S)."""
        now = now if now is not None else time.monotonic()
        self.poll(now)
        if not self.queue or now < self.next_send:
            return None
        self.next_send = now + LAB_SEND_S
        return self.queue.pop(0)


class _Window:
    def __init__(self, index):
        self.index = index
        self.ids = {"Lumen": set(), "Tecton": set()}
        self.alpha = {"Lumen": [], "Tecton": []}
        self.epsilon = {"Lumen": [], "Tecton": []}
        self.env_e = {"Lumen": [], "Tecton": []}
        self.rewards = {"Lumen": [], "Tecton": []}
        self.actions = {"Lumen": {}, "Tecton": {}}   # god view: behavior distribution
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
        la = a.get("last_action")
        if la:
            self.actions[sp][la] = self.actions[sp].get(la, 0) + 1
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
            # Behavior distribution: what the whole population actually did this
            # window (the god view's core observable; no interpretation attached).
            total = sum(self.actions[sp].values())
            if total:
                for act, cnt in sorted(self.actions[sp].items()):
                    out.append((f"live_{s}_action_{act}_frac", cnt / total,
                                f"share of {sp} decisions choosing {act} "
                                f"({total} decisions) — {prov}"))
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
        next_beat = 0.0
        next_phase_read = 0.0
        in_field = True
        manager = PopulationManager() if srv.manage else None
        # This thread's own connection, with a 250 ms busy wait: it answers the live sim, so a write
        # blocked by a meeting is skipped (see _skippable) rather than stalling the world.
        con = db.connect(srv.lab_db_path, timeout=0.25)
        # What the lab itself is doing, forwarded to the sim's SYMBIOTIC LAB panel: a separate
        # read-only connection, polled once a second, one line at a time (LabActivity).
        activity = LabActivity(srv.lab_db_path)
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
                    last_team_sent = -1.0   # a reset run restarts t at 0: report the team again at once
                    if srv.embody:
                        from .embodiment import EmbodiedField
                        field = EmbodiedField(run_id, msg.get("world_half_size"), msg.get("world_half_size_y"),
                                              msg.get("water_mask"), msg.get("decision_interval"))
                        # A new run restarts the sim clock; the duty phase itself survives it.
                        st = _skippable(duty.ensure, con, 0)
                        phase = st["phase"] if st is not None else duty.FIELD   # locked: start in the field
                        in_field = duty.in_field(phase)
                        next_phase_read = time.monotonic() + PHASE_POLL_S
                        _skippable(duty.heartbeat, con)
                        print(f"[observe] duty phase: {phase}")
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
                    # Then at most one line about the lab's own work (meetings, hypotheses,
                    # experiments, predictions): the sim draws the last few on its HUD.
                    report = activity.due()
                    if report:
                        self._send({"type": "log", "text": report})
                    if window is None:
                        continue
                    if field:
                        wall = time.monotonic()
                        if wall >= next_phase_read:
                            next_phase_read = wall + PHASE_POLL_S
                            st = _skippable(duty.read, con)
                            if st is not None:          # locked by a meeting: keep the last phase
                                in_field = duty.in_field(st["phase"])
                        field.step(t, agents, in_field)
                        if wall >= next_beat:
                            next_beat = wall + HEARTBEAT_S
                            _skippable(duty.heartbeat, con)   # `loop` waits while this is fresh
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
                            note = _skippable(field.flush, con, window.index,
                                              window.index * WINDOW_S,
                                              (window.index + 1) * WINDOW_S)
                            if note:   # locked by a meeting: this window's witnessed rows are skipped
                                print(f"[observe] {note}")
                                self._send({"type": "log", "text": note})
                        if field:
                            # The duty cycle runs on the same window clock as the evidence: five
                            # windows of fieldwork, then the team is called back to camp until a
                            # meeting has run (Lab/duty.py).
                            note = _skippable(duty.advance, con, k)
                            if note:
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
        # Dashboard collection switch (lab_meta.live_collect): while "off" the
        # bridge stays connected and replies instantly, but windows are
        # discarded instead of minted as evidence.
        if db.get_meta(con, "live_collect", "on") == "off":
            print(f"[observe] window {window.index}: collection paused — discarded")
            return
        t0, t1 = window.index * WINDOW_S, (window.index + 1) * WINDOW_S
        stats = window.stats(run_id, t0, t1)
        if not stats:
            return
        wid = f"live:{run_id}:w{window.index}"
        # A meeting on another connection can hold the write lock. This connection's busy wait is
        # deliberately short (it answers the live sim), so retry briefly across the gaps a meeting
        # leaves between turns, and give the window up rather than kill the bridge: the sim keeps
        # running and the next window is minted normally.
        for attempt in range(10):
            try:
                for stat, value, prov in stats:
                    db.add_evidence(con, wid, stat, float(value), prov)
                con.execute("UPDATE runs SET sim_end=? WHERE run_id=?", (t1, f"live:{run_id}"))
                con.commit()
                break
            except sqlite3.OperationalError as ex:
                if "locked" not in str(ex) and "busy" not in str(ex):
                    raise
                con.rollback()
                if attempt == 9:
                    print(f"[observe] window {window.index}: database busy, window not minted")
                    return
                time.sleep(0.2)
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
        role += (" + EMBODIED FIELD TEAM (nine bodies: eight in the field — the voting field scientists"
                 " plus Vega — and Humboldt the PI at camp; witnessed-only evidence, duty cycle in Lab/duty.py)")
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
