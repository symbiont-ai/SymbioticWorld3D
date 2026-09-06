#!/usr/bin/env python3
"""Lab dashboard: watch the scientists argue, the experiments run, and Vega's
charts — read-only, standard library only (like Tools/policy_server.py).

    python -m Lab.lab ui                 # http://localhost:8765
    python -m Lab.lab ui --port 9001 --db path/to/lab.sqlite

Serves Lab/ui/index.html, /api/state (one JSON snapshot of the whole lab DB),
and /figs/* (Vega's PNGs). The page polls /api/state, so it live-updates while
a session or the sim machine is writing to the same database.
"""
import json
import sqlite3
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

from . import config

UI_DIR = config.LAB_DIR / "ui"
FIG_DIR = config.REPORT_DIR / "figs"

TABLES = ["meetings", "runs", "hypotheses", "stances", "challenges", "disputes",
          "experiments", "predictions", "credibility", "open_questions",
          "transcript", "programs", "forecasts", "agent_params"]


def snapshot(db_path):
    con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    con.row_factory = sqlite3.Row
    out = {}
    try:
        for t in TABLES:
            try:
                out[t] = [dict(r) for r in con.execute(f"SELECT * FROM {t}")]
            except sqlite3.OperationalError:
                out[t] = []
        try:   # newest evidence only (the World tab's live ticker)
            out["evidence"] = [dict(r) for r in con.execute(
                "SELECT * FROM evidence ORDER BY rowid DESC LIMIT 200")]
        except sqlite3.OperationalError:
            out["evidence"] = []
        try:
            row = con.execute("SELECT value FROM lab_meta WHERE key='generation'").fetchone()
            out["generation"] = int(row["value"]) if row else 0
        except sqlite3.OperationalError:
            out["generation"] = 0
    finally:
        con.close()
    out["figs"] = sorted(p.name for p in FIG_DIR.glob("*.png")) if FIG_DIR.exists() else []
    return out


def make_handler(db_path):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def _send(self, code, body, ctype):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = urlparse(self.path).path
            if path in ("/", "/index.html"):
                page = (UI_DIR / "index.html").read_bytes()
                self._send(200, page, "text/html; charset=utf-8")
            elif path == "/api/state":
                if not Path(db_path).exists():
                    self._send(200, b'{"empty": true}', "application/json")
                    return
                body = json.dumps(snapshot(db_path)).encode()
                self._send(200, body, "application/json")
            elif path.startswith("/figs/"):
                name = Path(path).name          # no traversal: bare basename only
                f = FIG_DIR / name
                if f.exists() and f.suffix == ".png":
                    self._send(200, f.read_bytes(), "image/png")
                else:
                    self._send(404, b"not found", "text/plain")
            else:
                self._send(404, b"not found", "text/plain")

    return Handler


def serve(db_path=None, port=8765, host="0.0.0.0"):
    """Read-only; defaults to all interfaces so LAN viewers of the streamed
    world can open the dashboard next to it (pass --host 127.0.0.1 to keep it
    local)."""
    db_path = str(db_path or config.DB_PATH)
    httpd = ThreadingHTTPServer((host, port), make_handler(db_path))
    print(f"Lab dashboard: http://{host}:{port}  (db: {db_path})")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
