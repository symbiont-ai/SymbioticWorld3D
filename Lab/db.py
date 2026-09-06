"""SQLite layer. Every consequential object in the lab lives here; prose lives
only in transcripts/minutes. All promotion/validation logic sits in memory.py
and meeting.py — this module is storage plus a few typed helpers."""
import json
import sqlite3
from datetime import datetime, timezone
from pathlib import Path

from . import config

SCHEMA = """
CREATE TABLE IF NOT EXISTS meetings(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    started_at TEXT, kind TEXT, minutes TEXT);
CREATE TABLE IF NOT EXISTS runs(
    run_id TEXT PRIMARY KEY, mode TEXT, seed INTEGER, sim_end REAL,
    path TEXT, ingested_at TEXT);
CREATE TABLE IF NOT EXISTS evidence(
    id TEXT PRIMARY KEY, run_id TEXT, stat TEXT, value REAL,
    provenance TEXT, created_at TEXT);
CREATE TABLE IF NOT EXISTS hypotheses(
    id TEXT PRIMARY KEY, claim TEXT, mechanism TEXT, scope TEXT,
    status TEXT,                -- conjecture | supported | law | refuted | caveat
    proposer TEXT, created_meeting INTEGER);
CREATE TABLE IF NOT EXISTS hypothesis_evidence(
    hypothesis_id TEXT, evidence_id TEXT, UNIQUE(hypothesis_id, evidence_id));
CREATE TABLE IF NOT EXISTS stances(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meeting_id INTEGER, hypothesis_id TEXT, agent TEXT,
    stance TEXT,                -- agree | disagree | abstain
    confidence REAL, reason TEXT, evidence_ids TEXT,
    downgraded INTEGER DEFAULT 0);   -- 1 = recorded abstain because citation rule failed
CREATE TABLE IF NOT EXISTS challenges(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meeting_id INTEGER, hypothesis_id TEXT, agent TEXT, text TEXT);
CREATE TABLE IF NOT EXISTS disputes(
    id TEXT PRIMARY KEY, hypothesis_id TEXT, sides TEXT, crux TEXT,
    experiment_id TEXT, resolution TEXT);  -- open | resolved:<verdict> | unresolvable-in-sim
CREATE TABLE IF NOT EXISTS experiments(
    id TEXT PRIMARY KEY, hypothesis_id TEXT, protocol TEXT,
    status TEXT,                -- proposed | vetoed | queued | awaiting-sim | done | failed
    run_ids TEXT, metric_result TEXT, verdict TEXT, created_meeting INTEGER);
CREATE TABLE IF NOT EXISTS predictions(
    experiment_id TEXT, agent TEXT, predicted TEXT, confidence REAL,
    actual TEXT, brier REAL, UNIQUE(experiment_id, agent));
CREATE TABLE IF NOT EXISTS notebooks(
    agent TEXT PRIMARY KEY, doc TEXT, version INTEGER);
CREATE TABLE IF NOT EXISTS credibility(
    agent TEXT, domain TEXT, weight REAL, n_scored INTEGER,
    PRIMARY KEY(agent, domain));
CREATE TABLE IF NOT EXISTS open_questions(
    id TEXT PRIMARY KEY, text TEXT, origin TEXT,
    status TEXT);               -- open | addressed
CREATE TABLE IF NOT EXISTS transcript(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    meeting_id INTEGER, round TEXT, agent TEXT, payload TEXT);
CREATE TABLE IF NOT EXISTS lab_meta(
    key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS agent_params(
    agent TEXT, generation INTEGER,
    temperature REAL, base_confidence REAL, fitness REAL,
    PRIMARY KEY(agent, generation));
CREATE TABLE IF NOT EXISTS programs(          -- two-stage conservation programs
    id TEXT PRIMARY KEY, species TEXT, card_id TEXT,
    stage TEXT,          -- assessment | introduction
    status TEXT,         -- debating | assessing | assessment-failed | introducing | done | rejected
    assessment_exp TEXT, introduction_exp TEXT, created_meeting INTEGER);
CREATE TABLE IF NOT EXISTS forecasts(         -- Vega's, non-voting, report-only
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id TEXT, metric TEXT, horizon REAL,
    predicted REAL, lower REAL, upper REAL, actual REAL, created_at TEXT);
"""


def get_meta(con, key, default=None):
    row = con.execute("SELECT value FROM lab_meta WHERE key=?", (key,)).fetchone()
    return row["value"] if row else default


def set_meta(con, key, value):
    con.execute("INSERT INTO lab_meta VALUES(?,?) "
                "ON CONFLICT(key) DO UPDATE SET value=excluded.value", (key, str(value)))


def now():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def connect(path=None):
    p = Path(path) if path else config.DB_PATH
    p.parent.mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(p)
    con.row_factory = sqlite3.Row
    con.executescript(SCHEMA)
    return con


# --- small typed helpers ----------------------------------------------------

def next_id(con, table, prefix):
    n = con.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
    return f"{prefix}-{n + 1:03d}"


def add_evidence(con, run_id, stat, value, provenance):
    """Insert (or return existing) evidence object; IDs are stable and citable."""
    row = con.execute("SELECT id FROM evidence WHERE run_id=? AND stat=?",
                      (run_id, stat)).fetchone()
    if row:
        return row["id"]
    eid = next_id(con, "evidence", "E")
    con.execute("INSERT INTO evidence VALUES(?,?,?,?,?,?)",
                (eid, run_id, stat, value, provenance, now()))
    return eid


def evidence_exists(con, eid):
    return con.execute("SELECT 1 FROM evidence WHERE id=?", (eid,)).fetchone() is not None


def log_turn(con, meeting_id, round_name, agent, payload):
    con.execute("INSERT INTO transcript(meeting_id, round, agent, payload) VALUES(?,?,?,?)",
                (meeting_id, round_name, agent, json.dumps(payload)))


def get_notebook(con, agent):
    row = con.execute("SELECT doc FROM notebooks WHERE agent=?", (agent,)).fetchone()
    return row["doc"] if row else ""


def set_notebook(con, agent, doc):
    con.execute(
        "INSERT INTO notebooks(agent, doc, version) VALUES(?,?,1) "
        "ON CONFLICT(agent) DO UPDATE SET doc=excluded.doc, version=version+1",
        (agent, doc))


def get_credibility(con, agent, domain):
    row = con.execute("SELECT weight FROM credibility WHERE agent=? AND domain=?",
                      (agent, domain)).fetchone()
    return row["weight"] if row else 1.0


def set_credibility(con, agent, domain, weight, n_scored):
    con.execute(
        "INSERT INTO credibility VALUES(?,?,?,?) "
        "ON CONFLICT(agent, domain) DO UPDATE SET weight=excluded.weight, n_scored=excluded.n_scored",
        (agent, domain, weight, n_scored))
