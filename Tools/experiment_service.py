#!/usr/bin/env python
"""Experiment service: lets scientist agents on other machines run controlled
headless experiments on this host and read the live world.

  python Tools/experiment_service.py                 # 0.0.0.0:8800, one worker
  python Tools/experiment_service.py --port 8800 --workers 1

Endpoints (JSON, CORS open, NO authentication: LAN hackathon use only; see docs/SCIENTIST_API.md):
  POST /runs        queue headless runs (mode x seeds) -> job_id + run_ids
  GET  /runs        list jobs / runs with status
  GET  /runs/<id>   summary JSON of one run (or the job when <id> is a job_id)
  GET  /runs/<id>/files/<agents|births|deaths|population>.csv
  GET  /live        latest still-growing run dir: last population rows, control / policy files
  POST /control     append one validated command line to Saved/control.txt
  POST /notes, GET /notes   scientist notebook (Saved/scientist_notes.jsonl)
  GET  /docs        docs/SCIENTIST_API.md;  GET /  index;  GET /health

The HTTP layer and the job runner are standard library only. pandas / numpy /
scipy are imported lazily inside the analysis helpers and fall back to
standard-library implementations of the same statistics when absent.
Runs are executed one at a time (default) through Tools/run_sim.py with the
same interpreter that runs this service (--python to override).
"""
import argparse
import csv
import json
import math
import os
import queue
import re
import secrets
import statistics
import subprocess
import sys
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

ROOT = Path(__file__).resolve().parents[1]
RUN_SIM = ROOT / "Tools/run_sim.py"
SAVED = ROOT / "Saved"
RUNS_ROOT = SAVED / "SymbioticWorld"
EXP_DIR = SAVED / "experiments"
LOG_DIR = EXP_DIR / "logs"
CONTROL_FILE = SAVED / "control.txt"
POLICY_FILE = SAVED / "policy_servers.txt"
NOTES_FILE = SAVED / "scientist_notes.jsonl"
DOCS_FILE = ROOT / "docs/SCIENTIST_API.md"

MODE_NAMES = {"A": "A_learning_off", "B": "B_learning_on", "C": "C_learning_evolution", "N": "N_neutral_control"}
SPECIES = ("Lumen", "Tecton")
PARAMS = ("alpha", "epsilon", "social", "env_effect")
CSV_FILES = ("agents", "births", "deaths", "population")
DOWNLOADABLE = CSV_FILES + ("commands",)     # commands.csv exists only for runs made by a build with the control file
LIVE_WINDOW_S = 10.0
MIN_DECISIONS = 20          # organisms with fewer logged decisions are excluded from the drift statistic
MAX_BODY = 1 << 20

SET_CHARS = re.compile(r"^[A-Za-z0-9_.=;:\-]*$")
SET_ITEM = re.compile(r"^(Settings|Lumen|Tecton|Genome|Founder|Look)\.[A-Za-z_][A-Za-z0-9_]*=[A-Za-z0-9_.:\-]+$", re.I)   # scope is mandatory
POLICY_FILE_CHARS = re.compile(r"^[A-Za-z0-9_./\\:\- ]+$")
CONTROL_SCOPES = {"settings": "Settings", "lumen": "Lumen", "tecton": "Tecton", "genome": "Genome", "founder": "Genome", "look": "Look"}
# Mirrors Tools/control.py / ASWWorldManager::RunControlCommand (docs/CONTROL_FILE.md): keywords are
# case-insensitive and spaces around '=' are tolerated; the service writes the canonical form.
CONTROL_PATTERNS = [
    ("drought", re.compile(r"^drought\s*=\s*(on|off|toggle|1|0|true|false)$", re.I)),
    ("pause", re.compile(r"^pause\s*=\s*(on|off|1|0|true|false)$", re.I)),
    ("speed", re.compile(r"^speed\s*=\s*(\d+(?:\.\d*)?|\.\d+)$", re.I)),
    ("reset", re.compile(r"^reset(?:\s+seed\s*=\s*(-?\d+))?$", re.I)),
    ("mode", re.compile(r"^mode\s*=\s*([ABCN])$", re.I)),
    ("note", re.compile(r"^note\s*=(.{0,500})$", re.I)),
]
CONTROL_SET_ITEM = re.compile(r"^(Settings|Lumen|Tecton|Genome|Founder|Look)\.([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z0-9_.:+\-]+)$", re.I)
CONTROL_GRAMMAR = ("drought=on|off|toggle, speed=<float>, pause=on|off, set <Scope.Field>=<value>[;<Scope.Field>=<value>], "
                   "reset, reset seed=<int>, mode=A|B|C|N, note=<text>")


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def jsonable(o):
    """Make numpy scalars / NaN / Path JSON-safe (NaN and inf become null)."""
    if isinstance(o, dict):
        return {str(k): jsonable(v) for k, v in o.items()}
    if isinstance(o, (list, tuple)):
        return [jsonable(v) for v in o]
    if isinstance(o, Path):
        return str(o)
    if isinstance(o, bool) or o is None or isinstance(o, str):
        return o
    if isinstance(o, int):
        return o
    if isinstance(o, float):
        return None if (math.isnan(o) or math.isinf(o)) else o
    if hasattr(o, "item"):           # numpy scalar
        return jsonable(o.item())
    if hasattr(o, "tolist"):
        return jsonable(o.tolist())
    return str(o)


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------
class BadRequest(Exception):
    pass


def validate_job(body):
    if not isinstance(body, dict):
        raise BadRequest("body must be a JSON object")
    mode = body.get("mode", "C")
    modes = mode if isinstance(mode, list) else [mode]
    modes = [str(m).strip().upper() for m in modes]
    if not modes or any(m not in MODE_NAMES for m in modes):
        raise BadRequest("mode must be one of A, B, C, N (or a list of them)")
    seeds = body.get("seeds", body.get("seed", [1]))
    if not isinstance(seeds, list):
        seeds = [seeds]
    try:
        seeds = [int(s) for s in seeds]
    except (TypeError, ValueError):
        raise BadRequest("seeds must be integers")
    if not 1 <= len(seeds) <= 8:
        raise BadRequest("seeds: give 1..8 seeds")
    if any(s < 0 or s > 2 ** 31 - 1 for s in seeds):
        raise BadRequest("seeds must be in 0..2147483647")
    try:
        duration = float(body.get("duration", 600))
        speed = float(body.get("speed", 200))
    except (TypeError, ValueError):
        raise BadRequest("duration and speed must be numbers")
    if not 10 <= duration <= 3600:
        raise BadRequest("duration must be in 10..3600 logical seconds")
    if not 0.01 <= speed <= 1000:
        raise BadRequest("speed must be in 0.01..1000")
    set_spec = body.get("set") or ""
    if not isinstance(set_spec, str):
        raise BadRequest("set must be a string")
    set_spec = set_spec.strip().strip(";")
    if set_spec:
        if "," in set_spec:
            raise BadRequest("set: ',' is illegal (the string is one -SWSet argument; separate overrides with ';')")
        if not SET_CHARS.match(set_spec):
            raise BadRequest("set may contain only [A-Za-z0-9_.=;:-]")
        for item in set_spec.split(";"):
            if not SET_ITEM.match(item):
                raise BadRequest(f"set item '{item}' is not Scope.Field=value (Scope: Settings/Lumen/Tecton/Genome/Look)")
    label = body.get("label") or ""
    if not isinstance(label, str) or len(label) > 80 or not re.match(r"^[A-Za-z0-9_.:\- ]*$", label):
        raise BadRequest("label: <= 80 chars of [A-Za-z0-9_.:- ]")
    policy_file = body.get("policy_file")
    if policy_file is not None:
        policy_file = str(policy_file).strip()
        if not policy_file:
            policy_file = None
        elif ("," in policy_file or ".." in policy_file or ":" in policy_file or not POLICY_FILE_CHARS.match(policy_file)
              or Path(policy_file).is_absolute() or not policy_file.replace("\\", "/").startswith("Saved/")):
            # The sim reads and logs every line of this file, so an unrestricted path would be an arbitrary file read.
            raise BadRequest("policy_file: a relative path under Saved/ (no ',', '..' or drive letters)")
    wall_timeout = body.get("wall_timeout")
    if wall_timeout is not None:
        try:
            wall_timeout = float(wall_timeout)
        except (TypeError, ValueError):
            raise BadRequest("wall_timeout must be a number of seconds")
        if not 30 <= wall_timeout <= 6 * 3600:
            raise BadRequest("wall_timeout must be in 30..21600 s")
    return {"modes": modes, "seeds": seeds, "duration": duration, "speed": speed, "set": set_spec,
            "label": label, "policy_file": policy_file, "wall_timeout": wall_timeout}


def validate_control(cmd):
    """Return the canonical command line or raise BadRequest. Grammar: docs/SCIENTIST_API.md."""
    if not isinstance(cmd, str):
        raise BadRequest("command must be a string")
    cmd = cmd.strip()
    if not cmd or "\n" in cmd or "\r" in cmd or len(cmd) > 600:
        raise BadRequest("command: one non-empty line, <= 600 chars")
    if cmd.startswith("#"):
        raise BadRequest("comments are not commands")
    low = cmd.lower()
    if low == "set" or low.startswith("set "):
        items = [it.strip() for it in cmd[3:].split(";") if it.strip()]
        if not items:
            raise BadRequest("set needs <Scope.Field>=<value> (Scope: Settings, Lumen, Tecton, Genome, Look)")
        canon = []
        for it in items:
            m = CONTROL_SET_ITEM.match(it)
            if not m:
                raise BadRequest(f"bad set item '{it}': expected Scope.Field=value with Scope in Settings/Lumen/Tecton/Genome/Look "
                                 "and a value of [A-Za-z0-9_.:+-]")
            scope, field, value = m.groups()
            canon.append(f"{CONTROL_SCOPES[scope.lower()]}.{field}={value}")
        return "set " + ";".join(canon), "set"
    for kind, pat in CONTROL_PATTERNS:
        m = pat.match(cmd)
        if not m:
            continue
        arg = m.group(1)
        if kind == "note":
            return "note=" + arg, kind
        if kind == "reset":
            return ("reset seed=" + arg) if arg is not None else "reset", kind
        if kind == "mode":
            return "mode=" + arg.upper(), kind
        if kind == "speed":
            if float(arg) < 0:
                raise BadRequest("speed must be >= 0")
            return "speed=" + arg, kind
        return f"{kind}={arg.lower()}", kind
    raise BadRequest("command not in grammar: " + CONTROL_GRAMMAR)


# ---------------------------------------------------------------------------
# CSV helpers (standard library)
# ---------------------------------------------------------------------------
def read_rows(path):
    p = Path(path)
    if not p.exists() or p.stat().st_size == 0:
        return []
    with open(p, newline="", encoding="utf-8", errors="replace") as f:
        return list(csv.DictReader(f))


def fnum(x, default=None):
    try:
        return float(x)
    except (TypeError, ValueError):
        return default


def pearson(xs, ys):
    n = len(xs)
    if n < 2:
        return None
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    syy = sum((y - my) ** 2 for y in ys)
    if sxx <= 0 or syy <= 0:
        return None
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / math.sqrt(sxx * syy)


def tail_rows(path, nbytes=16384):
    """Header + the complete rows in the last nbytes of a CSV (for the live view)."""
    p = Path(path)
    size = p.stat().st_size
    with open(p, "rb") as f:
        header = f.readline().decode("utf-8", "replace").strip().split(",")
        start = max(len(header) + 1, size - nbytes)
        f.seek(start)
        chunk = f.read().decode("utf-8", "replace")
    lines = chunk.split("\n")
    if start > len(header) + 1:
        lines = lines[1:]            # first line may be partial
    rows = []
    for ln in lines:
        ln = ln.strip()
        if not ln:
            continue
        parts = ln.split(",")
        if len(parts) != len(header):
            continue
        rows.append(dict(zip(header, parts)))
    return rows


def typed_row(row):
    out = {}
    for k, v in row.items():
        f = fnum(v)
        if f is None:
            out[k] = v
        elif f.is_integer() and re.match(r"^-?\d+$", v or ""):
            out[k] = int(f)
        else:
            out[k] = f
    return out


# ---------------------------------------------------------------------------
# Analysis: reuse Analysis/analyze_run.py when pandas is available, else stdlib
# ---------------------------------------------------------------------------
_analyze_mod = None
_analyze_tried = False


def analyze_module():
    """Import Analysis/analyze_run.py (needs pandas / numpy); None when unavailable."""
    global _analyze_mod, _analyze_tried
    if _analyze_tried:
        return _analyze_mod
    _analyze_tried = True
    if os.environ.get("SW_EXPERIMENT_NO_PANDAS"):
        return None
    try:
        sys.path.insert(0, str(ROOT / "Analysis"))
        import analyze_run  # noqa: F401  (imports pandas, numpy)
        _analyze_mod = analyze_run
    except Exception as ex:  # ImportError or a broken install
        print(f"[service] Analysis/analyze_run.py not importable ({ex}); using stdlib statistics", flush=True)
        _analyze_mod = None
    return _analyze_mod


def species_block_stdlib(run_dir, species, births, deaths, agents_drift):
    pop = [typed_row(r) for r in read_rows(run_dir / "population.csv") if r.get("species") == species]
    pop.sort(key=lambda r: r["sim_time"])
    b = [typed_row(r) for r in births if r.get("species") == species]
    d = [r for r in deaths if r.get("species") == species]
    out = {"n_population_rows": len(pop)}
    if pop:
        first, last = pop[0], pop[-1]
        ns = [r["n"] for r in pop]
        out.update({
            "initial_n": first["n"], "final_n": last["n"], "min_n": min(ns), "max_n": max(ns),
            "max_generation": max(r["max_generation"] for r in pop),
            "mean_generation_final": last["mean_generation"],
            "initial_mean_alpha": first["mean_alpha"],
            "final": {k: last[k] for k in ("mean_alpha", "sd_alpha", "mean_epsilon", "sd_epsilon", "mean_social",
                                            "sd_social", "mean_env_effect", "sd_env_effect")},
            "ext_decisions": last.get("ext_decisions"), "ext_fallbacks": last.get("ext_fallbacks"),
            "drought_rows": sum(1 for r in pop if r.get("drought_state") == 1),
        })
    out["births"] = len(b)
    out["deaths"] = len(d)
    out["deaths_starvation"] = sum(1 for r in d if r.get("cause") == "starvation")
    out["deaths_age"] = sum(1 for r in d if r.get("cause") == "age")
    # inheritance: parent/child correlation and mean |delta| per parameter (births.csv)
    inh = {"n_births": len(b)}
    for p in PARAMS:
        xs = [r[f"parent_{p}"] for r in b if f"parent_{p}" in r]
        ys = [r[f"child_{p}"] for r in b if f"child_{p}" in r]
        if xs and ys:
            inh[f"{p}_corr"] = pearson(xs, ys)
            inh[f"{p}_mean_abs_delta"] = sum(abs(y - x) for x, y in zip(xs, ys)) / len(xs)
    out["inheritance"] = inh
    # per generation (children born into each generation)
    gens = {}
    for r in b:
        gens.setdefault(r["child_generation"], []).append(r)
    pg = []
    for g in sorted(gens):
        rows = gens[g]
        item = {"generation": g, "n": len(rows)}
        for p in PARAMS:
            vals = [r[f"child_{p}"] for r in rows if f"child_{p}" in r]
            if vals:
                item[f"mean_{p}"] = sum(vals) / len(vals)
                item[f"sd_{p}"] = statistics.stdev(vals) if len(vals) > 1 else None
        pg.append(item)
    out["per_generation"] = pg
    out["q_drift"] = agents_drift.get(species, {"n_agents": 0, "min_decisions": MIN_DECISIONS})
    return out


def drift_stdlib(run_dir):
    """Per-organism L1 drift of the bandit table between its first and last logged row."""
    p = run_dir / "agents.csv"
    if not p.exists() or p.stat().st_size == 0:
        return {}
    with open(p, newline="", encoding="utf-8", errors="replace") as f:
        rd = csv.DictReader(f)
        qcols = [c for c in (rd.fieldnames or []) if c.startswith("Q_")]
        if not qcols:
            return {}
        first, last = {}, {}
        for r in rd:
            key = (r["agent_id"], r["species"])
            t = fnum(r["sim_time"], 0.0)
            q = [fnum(r[c], 0.0) for c in qcols]
            dec = int(fnum(r.get("decisions"), 0))
            if key not in first or t < first[key][0]:
                first[key] = (t, q)
            if key not in last or t >= last[key][0]:
                last[key] = (t, q, dec)
    per_species = {}
    for key, (t0, q0) in first.items():
        t1, q1, dec = last[key]
        if t1 <= t0 or dec < MIN_DECISIONS:
            continue
        l1 = sum(abs(a - b) for a, b in zip(q0, q1))
        mx = max(abs(a - b) for a, b in zip(q0, q1))
        greedy = int(max(range(7), key=lambda i: q0[i]) != max(range(7), key=lambda i: q1[i]))
        per_species.setdefault(key[1], []).append((l1, mx, greedy))
    out = {}
    for sp, rows in per_species.items():
        l1s = [r[0] for r in rows]
        out[sp] = {"n_agents": len(rows), "min_decisions": MIN_DECISIONS,
                   "mean_l1": sum(l1s) / len(l1s), "median_l1": statistics.median(l1s),
                   "median_max_abs": statistics.median([r[1] for r in rows]),
                   "greedy_changed_frac": sum(r[2] for r in rows) / len(rows)}
    return out


def summarize_stdlib(run_dir):
    births = read_rows(run_dir / "births.csv")
    deaths = read_rows(run_dir / "deaths.csv")
    pop = [typed_row(r) for r in read_rows(run_dir / "population.csv")]
    drift = drift_stdlib(run_dir)
    out = {"analysis_backend": "stdlib"}
    if pop:
        last = max(pop, key=lambda r: r["sim_time"])
        out.update({"mode_name": last["mode"], "seed_logged": last["seed"], "sim_time_end": last["sim_time"],
                    "births_total": last["births"], "deaths_total": last["deaths"],
                    "resource_A_final": last["resource_A"], "resource_B_final": last["resource_B"]})
    out["species"] = {sp: species_block_stdlib(run_dir, sp, births, deaths, drift) for sp in SPECIES}
    inh = {"n_births": len(births)}
    b = [typed_row(r) for r in births]
    for p in PARAMS:
        xs = [r[f"parent_{p}"] for r in b if f"parent_{p}" in r]
        ys = [r[f"child_{p}"] for r in b if f"child_{p}" in r]
        if xs:
            inh[f"{p}_corr"] = pearson(xs, ys)
            inh[f"{p}_mean_abs_delta"] = sum(abs(y - x) for x, y in zip(xs, ys)) / len(xs)
    out["inheritance_all_species"] = inh
    return out


def summarize_pandas(run_dir, ar):
    import numpy as np  # noqa: F401
    run = ar.load_run(run_dir)
    pop, births, deaths, agents = run["population"], run["births"], run["deaths"], run["agents"]
    out = {"analysis_backend": "pandas+analyze_run"}
    if not pop.empty:
        t_end = pop["sim_time"].max()
        last = pop[pop["sim_time"] == t_end].iloc[0]
        out.update({"mode_name": run["mode"], "seed_logged": run["seed"], "sim_time_end": float(t_end),
                    "births_total": int(last["births"]), "deaths_total": int(last["deaths"]),
                    "resource_A_final": float(last["resource_A"]), "resource_B_final": float(last["resource_B"])})
    ll = ar.lifetime_learning(agents)
    species = {}
    for sp in SPECIES:
        blk = {}
        p = ar.population_trajectory(pop, sp)
        blk["n_population_rows"] = int(len(p))
        if not p.empty:
            first, last = p.iloc[0], p.iloc[-1]
            blk.update({
                "initial_n": int(first["n"]), "final_n": int(last["n"]), "min_n": int(p["n"].min()), "max_n": int(p["n"].max()),
                "max_generation": int(p["max_generation"].max()), "mean_generation_final": float(last["mean_generation"]),
                "initial_mean_alpha": float(first["mean_alpha"]),
                "final": {k: float(last[k]) for k in ("mean_alpha", "sd_alpha", "mean_epsilon", "sd_epsilon", "mean_social",
                                                       "sd_social", "mean_env_effect", "sd_env_effect")},
                "ext_decisions": int(last["ext_decisions"]) if "ext_decisions" in p.columns else None,
                "ext_fallbacks": int(last["ext_fallbacks"]) if "ext_fallbacks" in p.columns else None,
                "drought_rows": int((p["drought_state"] == 1).sum()) if "drought_state" in p.columns else 0,
            })
        b = births[births["species"] == sp] if not births.empty else births
        d = deaths[deaths["species"] == sp] if not deaths.empty else deaths
        blk["births"] = int(len(b))
        blk["deaths"] = int(len(d))
        blk["deaths_starvation"] = int((d["cause"] == "starvation").sum()) if len(d) else 0
        blk["deaths_age"] = int((d["cause"] == "age").sum()) if len(d) else 0
        blk["inheritance"] = ar.inheritance(b) if len(b) else {"n_births": 0}
        pg = ar.per_generation(births, sp) if not births.empty else None
        rows = []
        if pg is not None and not pg.empty:
            for gen in pg.index:
                item = {"generation": int(gen), "n": int(pg["child_alpha"].loc[gen, "count"])}
                for p_ in PARAMS:
                    col = f"child_{p_}"
                    if col in pg.columns.get_level_values(0):
                        item[f"mean_{p_}"] = float(pg[col].loc[gen, "mean"])
                        sd = pg[col].loc[gen, "std"]
                        item[f"sd_{p_}"] = None if sd != sd else float(sd)
                rows.append(item)
        blk["per_generation"] = rows
        qd = {"n_agents": 0, "min_decisions": MIN_DECISIONS}
        if not ll.empty:
            g = ll[(ll["species"] == sp) & (ll["decisions"] >= MIN_DECISIONS)]
            if not g.empty:
                qd.update({"n_agents": int(len(g)), "mean_l1": float(g["q_drift_l1"].mean()),
                           "median_l1": float(g["q_drift_l1"].median()), "median_max_abs": float(g["q_drift_max"].median()),
                           "greedy_changed_frac": float(g["greedy_changed"].mean())})
        blk["q_drift"] = qd
        species[sp] = blk
    out["species"] = species
    out["inheritance_all_species"] = ar.inheritance(births) if not births.empty else {"n_births": 0}
    return out


def summarize_run(run_dir):
    run_dir = Path(run_dir)
    ar = analyze_module()
    if ar is not None:
        try:
            return jsonable(summarize_pandas(run_dir, ar))
        except Exception as ex:
            print(f"[service] pandas summary failed ({ex!r}); falling back to stdlib", flush=True)
    return jsonable(summarize_stdlib(run_dir))


# ---------------------------------------------------------------------------
# Welch t-test (scipy when available, else the same statistic in plain Python)
# ---------------------------------------------------------------------------
def _betacf(a, b, x, max_iter=300, eps=3e-14):
    qab, qap, qam = a + b, a + 1.0, a - 1.0
    c, d = 1.0, 1.0 - qab * x / qap
    d = 1.0 / (d if abs(d) > 1e-300 else 1e-300)
    h = d
    for m in range(1, max_iter + 1):
        m2 = 2 * m
        aa = m * (b - m) * x / ((qam + m2) * (a + m2))
        d = 1.0 + aa * d; d = 1.0 / (d if abs(d) > 1e-300 else 1e-300)
        c = 1.0 + aa / c; c = c if abs(c) > 1e-300 else 1e-300
        h *= d * c
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
        d = 1.0 + aa * d; d = 1.0 / (d if abs(d) > 1e-300 else 1e-300)
        c = 1.0 + aa / c; c = c if abs(c) > 1e-300 else 1e-300
        de = d * c
        h *= de
        if abs(de - 1.0) < eps:
            break
    return h


def betainc_reg(a, b, x):
    """Regularized incomplete beta I_x(a, b) (continued fraction, Numerical Recipes)."""
    if x <= 0:
        return 0.0
    if x >= 1:
        return 1.0
    lbeta = math.lgamma(a + b) - math.lgamma(a) - math.lgamma(b) + a * math.log(x) + b * math.log(1 - x)
    front = math.exp(lbeta)
    if x < (a + 1) / (a + b + 2):
        return front * _betacf(a, b, x) / a
    return 1.0 - front * _betacf(b, a, 1 - x) / b


def welch_ttest(xs, ys):
    """Two-sided Welch t-test. Returns dict(t, df, p, method)."""
    n1, n2 = len(xs), len(ys)
    if n1 < 2 or n2 < 2:
        return {"t": None, "df": None, "p": None, "method": "none", "note": "need >= 2 values per group"}
    try:
        from scipy import stats
        t, p = stats.ttest_ind(xs, ys, equal_var=False)
        v1, v2 = statistics.variance(xs), statistics.variance(ys)
        se1, se2 = v1 / n1, v2 / n2
        df = (se1 + se2) ** 2 / (se1 ** 2 / (n1 - 1) + se2 ** 2 / (n2 - 1)) if (se1 + se2) > 0 else None
        return jsonable({"t": float(t), "df": df, "p": float(p), "method": "scipy.stats.ttest_ind(equal_var=False)"})
    except ImportError:
        pass
    m1, m2 = sum(xs) / n1, sum(ys) / n2
    v1, v2 = statistics.variance(xs), statistics.variance(ys)
    se1, se2 = v1 / n1, v2 / n2
    if se1 + se2 <= 0:
        return {"t": None, "df": None, "p": None, "method": "builtin", "note": "zero variance in both groups"}
    t = (m1 - m2) / math.sqrt(se1 + se2)
    df = (se1 + se2) ** 2 / (se1 ** 2 / (n1 - 1) + se2 ** 2 / (n2 - 1))
    p = betainc_reg(df / 2.0, 0.5, df / (df + t * t))
    return {"t": t, "df": df, "p": p, "method": "builtin Welch (regularized incomplete beta)"}


def job_comparison(job):
    """End-of-run Lumen mean alpha, C vs N, across the finished runs of one job."""
    groups = {}
    for r in job["runs"]:
        s = r.get("summary") or {}
        lum = (s.get("species") or {}).get("Lumen") or {}
        val = (lum.get("final") or {}).get("mean_alpha")
        if r.get("status") == "done" and val is not None:
            groups.setdefault(r["mode"], []).append({"seed": r["seed"], "run_id": r["run_id"], "value": val})
    if "C" not in groups or "N" not in groups:
        return None
    c = [g["value"] for g in groups["C"]]
    n = [g["value"] for g in groups["N"]]
    res = {"species": "Lumen", "metric": "end-of-run mean_alpha (population.csv, last row)",
           "C": {"n": len(c), "mean": sum(c) / len(c), "sd": statistics.stdev(c) if len(c) > 1 else None, "runs": groups["C"]},
           "N": {"n": len(n), "mean": sum(n) / len(n), "sd": statistics.stdev(n) if len(n) > 1 else None, "runs": groups["N"]}}
    res.update(welch_ttest(c, n))
    if res.get("p") is not None:
        res["reading"] = ("mean alpha under C separates from neutral drift at p < 0.05" if res["p"] < 0.05
                          else "not distinguishable from neutral drift with these seeds")
    return res


# ---------------------------------------------------------------------------
# Job store + worker
# ---------------------------------------------------------------------------
class Store:
    def __init__(self):
        self.lock = threading.RLock()
        self.jobs = {}          # job_id -> job dict
        self.run_index = {}     # run_id -> job_id
        self.sim_index = {}     # sim_run_id (Saved dir name) -> run_id
        EXP_DIR.mkdir(parents=True, exist_ok=True)
        LOG_DIR.mkdir(parents=True, exist_ok=True)
        for p in sorted(EXP_DIR.glob("job-*.json")):
            try:
                job = json.loads(p.read_text(encoding="utf-8"))
            except Exception:
                continue
            for r in job.get("runs", []):
                if r.get("status") in ("queued", "running"):   # the service that owned it is gone
                    r["status"] = "failed"
                    r["error"] = "service restarted before the run finished"
            if job.get("status") in ("queued", "running"):
                job["status"] = "failed"
            self._register(job)

    def _register(self, job):
        self.jobs[job["job_id"]] = job
        for r in job["runs"]:
            self.run_index[r["run_id"]] = job["job_id"]
            if r.get("sim_run_id"):
                self.sim_index[r["sim_run_id"]] = r["run_id"]

    def save(self, job):
        with self.lock:
            self._register(job)
            path = EXP_DIR / f"{job['job_id']}.json"
            tmp = path.with_suffix(".json.tmp")
            tmp.write_text(json.dumps(jsonable(job), indent=1), encoding="utf-8")
            os.replace(tmp, path)

    def new_job(self, spec):
        job_id = f"job-{datetime.now().strftime('%Y%m%d-%H%M%S')}-{secrets.token_hex(2)}"
        runs = []
        k = 0
        for m in spec["modes"]:
            for s in spec["seeds"]:
                k += 1
                runs.append({"run_id": f"{job_id}-r{k}", "job_id": job_id, "index": k, "mode": m, "mode_name": MODE_NAMES[m],
                             "seed": s, "duration": spec["duration"], "speed": spec["speed"], "set": spec["set"],
                             "label": spec["label"], "policy_file": spec["policy_file"], "status": "queued",
                             "sim_run_id": None, "exit_code": None, "wall_s": None, "summary": None})
        job = {"job_id": job_id, "label": spec["label"], "created_utc": utc_now(), "status": "queued",
               "request": {k_: v for k_, v in spec.items()}, "runs": runs, "comparison": None}
        self.save(job)
        return job

    def find(self, ident):
        """Return (job, run) for a job_id, run_id or sim run dir name; run is None for a job_id."""
        with self.lock:
            if ident in self.jobs:
                return self.jobs[ident], None
            rid = self.sim_index.get(ident, ident)
            jid = self.run_index.get(rid)
            if jid is None:
                return None, None
            job = self.jobs[jid]
            for r in job["runs"]:
                if r["run_id"] == rid:
                    return job, r
        return None, None

    def listing(self):
        with self.lock:
            jobs = []
            for job in sorted(self.jobs.values(), key=lambda j: j["created_utc"]):
                jobs.append({"job_id": job["job_id"], "label": job["label"], "status": job["status"],
                             "created_utc": job["created_utc"], "has_comparison": job.get("comparison") is not None,
                             "runs": [{k: r.get(k) for k in ("run_id", "mode", "seed", "duration", "speed", "set", "status",
                                                             "sim_run_id", "exit_code", "wall_s")} for r in job["runs"]]})
            return jobs

    def running(self):
        with self.lock:
            return [r["run_id"] for j in self.jobs.values() for r in j["runs"] if r["status"] == "running"]


def run_dir_for(run):
    return RUNS_ROOT / run["sim_run_id"] if run.get("sim_run_id") else None


class Worker(threading.Thread):
    def __init__(self, store, q, python_exe, name):
        super().__init__(daemon=True, name=name)
        self.store, self.q, self.python = store, q, python_exe

    def run(self):
        while True:
            job_id, run_id = self.q.get()
            job, run = self.store.find(run_id)
            if run is None:
                self.q.task_done()
                continue
            try:
                self.execute(job, run)
            except Exception as ex:
                run["status"] = "failed"
                run["error"] = f"{type(ex).__name__}: {ex}"
            finally:
                with self.store.lock:
                    statuses = {r["status"] for r in job["runs"]}
                    if statuses <= {"done", "failed"}:
                        job["status"] = "done" if statuses == {"done"} else "failed"
                        try:
                            job["comparison"] = job_comparison(job)
                        except Exception as ex:
                            job["comparison"] = {"error": str(ex)}
                    else:
                        job["status"] = "running"
                    job["finished_utc"] = utc_now() if job["status"] in ("done", "failed") else None
                    self.store.save(job)
                self.q.task_done()

    def execute(self, job, run):
        with self.store.lock:
            run["status"] = "running"
            run["started_utc"] = utc_now()
            job["status"] = "running"
            self.store.save(job)
        cmd = [self.python, str(RUN_SIM), "--mode", run["mode"], "--seed", str(run["seed"]),
               "--duration", str(run["duration"]), "--speed", str(run["speed"])]
        if run["set"]:
            cmd += ["--set", run["set"]]
        if run["policy_file"]:
            cmd += ["--policy-file", run["policy_file"]]
        run["command"] = cmd
        timeout = run.get("wall_timeout") or job["request"].get("wall_timeout") or (300 + 4 * run["duration"] / max(run["speed"], 0.01))
        before = {p.name for p in RUNS_ROOT.iterdir()} if RUNS_ROOT.exists() else set()
        log_path = LOG_DIR / f"{run['run_id']}.log"
        t0 = time.time()
        print(f"[worker] start {run['run_id']}: {' '.join(cmd)}", flush=True)
        with open(log_path, "w", encoding="utf-8", errors="replace") as logf:
            try:
                proc = subprocess.run(cmd, cwd=str(ROOT), stdout=logf, stderr=subprocess.STDOUT, timeout=timeout)
                exit_code = proc.returncode
            except subprocess.TimeoutExpired:
                exit_code = -999
                run["error"] = f"wall timeout after {timeout:.0f} s"
        wall = time.time() - t0
        after = {p.name for p in RUNS_ROOT.iterdir()} if RUNS_ROOT.exists() else set()
        suffix = f"_seed{run['seed']}_{run['mode_name']}"
        new = sorted(n for n in (after - before) if n.endswith(suffix))
        run["exit_code"] = exit_code
        run["wall_s"] = round(wall, 1)
        run["finished_utc"] = utc_now()
        run["log_file"] = log_path.relative_to(ROOT).as_posix()
        try:
            lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
            run["stdout_tail"] = lines[-12:]
        except OSError:
            run["stdout_tail"] = []
        if not new:
            run["status"] = "failed"
            run["error"] = run.get("error") or f"no new run dir matching *{suffix} (exit {exit_code})"
            print(f"[worker] FAILED {run['run_id']}: {run['error']}", flush=True)
            return
        run["sim_run_id"] = new[-1]
        run["run_dir"] = (RUNS_ROOT / new[-1]).relative_to(ROOT).as_posix()
        run["summary"] = summarize_run(RUNS_ROOT / new[-1])
        run["status"] = "done" if exit_code == 0 else "failed"
        if exit_code != 0:
            run["error"] = run.get("error") or f"run_sim exit code {exit_code}"
        print(f"[worker] {run['status']} {run['run_id']} -> {new[-1]} exit {exit_code} wall {wall:.0f}s", flush=True)


# ---------------------------------------------------------------------------
# Live view, control, notes
# ---------------------------------------------------------------------------
def read_text_if(path, limit=20000):
    p = Path(path)
    if not p.exists():
        return None
    try:
        return p.read_text(encoding="utf-8", errors="replace")[-limit:]
    except OSError:
        return None


def live_view(store):
    dirs = [d for d in RUNS_ROOT.iterdir() if d.is_dir() and (d / "population.csv").exists()] if RUNS_ROOT.exists() else []
    now = time.time()
    scored = []
    for d in dirs:
        mt = max((d / f"{n}.csv").stat().st_mtime for n in CSV_FILES if (d / f"{n}.csv").exists())
        scored.append((mt, d))
    out = {"utc": utc_now(), "run_id": None, "live": False, "service_running_runs": store.running(),
           "policy_servers_txt": read_text_if(POLICY_FILE), "control_txt": read_text_if(CONTROL_FILE)}
    if not scored:
        out["note"] = "no run directories under Saved/SymbioticWorld"
        return out
    growing = [x for x in scored if now - x[0] < LIVE_WINDOW_S]
    mt, d = max(growing) if growing else max(scored)
    out["run_id"] = d.name
    out["live"] = bool(growing)
    out["log_age_s"] = round(now - mt, 1)
    out["service_run_id"] = store.sim_index.get(d.name)
    rows = tail_rows(d / "population.csv")
    latest = {}
    for r in rows:
        latest[r.get("species")] = r
    species = {sp: typed_row(latest[sp]) for sp in SPECIES if sp in latest}
    any_row = next(iter(species.values()), None)
    if any_row:
        out.update({"sim_time": any_row.get("sim_time"), "mode_name": any_row.get("mode"), "seed": any_row.get("seed"),
                    "drought_state": any_row.get("drought_state"), "births": any_row.get("births"), "deaths": any_row.get("deaths")})
    out["species"] = species
    cmds = d / "commands.csv"
    if cmds.exists() and cmds.stat().st_size > 0:
        try:
            out["commands_executed"] = read_rows(cmds)[-10:]     # last executed control lines (docs/CONTROL_FILE.md)
        except OSError:
            pass
    return out


def append_control(line):
    """Append one newline-terminated line (bare '\\n', like Tools/control.py); never rewrite the file."""
    SAVED.mkdir(parents=True, exist_ok=True)
    prefix = b""
    if CONTROL_FILE.exists() and CONTROL_FILE.stat().st_size > 0:
        with open(CONTROL_FILE, "rb") as f:
            f.seek(-1, os.SEEK_END)
            if f.read(1) != b"\n":
                prefix = b"\n"       # never glue onto a partial line: the sim executes newline-terminated lines only
    with open(CONTROL_FILE, "ab") as f:
        f.write(prefix + (line + "\n").encode("utf-8"))
    with open(CONTROL_FILE, "rb") as f:
        n = sum(1 for _ in f)
    return n


def append_note(text, author):
    SAVED.mkdir(parents=True, exist_ok=True)
    rec = {"utc": utc_now(), "author": author, "text": text}
    with open(NOTES_FILE, "ab") as f:
        f.write((json.dumps(rec, ensure_ascii=False) + "\n").encode("utf-8"))
    return rec


def rel_posix(path):
    return Path(path).relative_to(ROOT).as_posix()


def read_notes(limit=200):
    if not NOTES_FILE.exists():
        return []
    out = []
    for ln in NOTES_FILE.read_text(encoding="utf-8").splitlines():
        ln = ln.strip()
        if ln:
            try:
                out.append(json.loads(ln))
            except json.JSONDecodeError:
                out.append({"raw": ln})
    return out[-limit:]


INDEX_HTML = """<!doctype html><html><head><meta charset="utf-8"><title>Symbiotic World experiment service</title>
<style>body{font-family:system-ui,sans-serif;max-width:60em;margin:2em auto;line-height:1.4}code{background:#eee;padding:0 .3em}</style></head>
<body><h1>Symbiotic World: experiment service</h1>
<p>JSON API for scientist agents. No authentication (LAN hackathon). Full documentation: <a href="/docs">/docs</a> (docs/SCIENTIST_API.md).</p>
<table border="1" cellpadding="4" cellspacing="0">
<tr><th>Method</th><th>Path</th><th>What</th></tr>
<tr><td>POST</td><td><code>/runs</code></td><td>queue headless runs: {"mode":"C","seeds":[1,2],"duration":600,"speed":200,"set":"Settings.PatchRegenPerSec=8","label":"regen8","policy_file":null}</td></tr>
<tr><td>GET</td><td><code>/runs</code></td><td>all jobs and runs with status</td></tr>
<tr><td>GET</td><td><code>/runs/&lt;run_id|job_id|sim_run_id&gt;</code></td><td>summary JSON of a run (job JSON incl. Welch C vs N for a job_id)</td></tr>
<tr><td>GET</td><td><code>/runs/&lt;run_id&gt;/files/&lt;agents|births|deaths|population&gt;.csv</code></td><td>raw CSV of that run</td></tr>
<tr><td>GET</td><td><code>/live</code></td><td>latest growing run: last population rows per species, control.txt, policy_servers.txt</td></tr>
<tr><td>POST</td><td><code>/control</code></td><td>{"command":"drought=on"} appended to Saved/control.txt (grammar in /docs)</td></tr>
<tr><td>POST / GET</td><td><code>/notes</code></td><td>{"text":"...","author":"scientist-1"} notebook (Saved/scientist_notes.jsonl)</td></tr>
<tr><td>GET</td><td><code>/health</code></td><td>queue length, running runs, worker count</td></tr>
</table>
<p>Modes: A learning off, B learning on / genome fixed, C learning + evolution, N neutral-drift control.
Lifetime learning is a tabular contextual bandit (gamma = 0); evolution is asexual Gaussian mutation of alpha, epsilon, social.</p>
</body></html>"""


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------
class Handler(BaseHTTPRequestHandler):
    server_version = "SWExperimentService/1.0"
    store = None
    q = None
    workers = 1

    def log_message(self, fmt, *args):
        sys.stdout.write("[http] %s %s\n" % (self.address_string(), fmt % args))
        sys.stdout.flush()

    # -- helpers ------------------------------------------------------------
    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def send_json(self, obj, code=200):
        data = json.dumps(jsonable(obj), indent=1).encode("utf-8")
        self.send_response(code)
        self._cors()
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_text(self, text, ctype="text/plain; charset=utf-8", code=200):
        data = text.encode("utf-8")
        self.send_response(code)
        self._cors()
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_file(self, path, ctype="text/csv; charset=utf-8"):
        size = path.stat().st_size
        self.send_response(200)
        self._cors()
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(size))
        self.send_header("Content-Disposition", f'attachment; filename="{path.parent.name}_{path.name}"')
        self.end_headers()
        with open(path, "rb") as f:
            while True:
                chunk = f.read(1 << 16)
                if not chunk:
                    break
                self.wfile.write(chunk)

    def error(self, msg, code=400):
        self.send_json({"error": msg}, code)

    def read_json(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n > MAX_BODY:
            raise BadRequest("body too large")
        raw = self.rfile.read(n) if n else b""
        if not raw.strip():
            return {}
        try:
            return json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as ex:
            raise BadRequest(f"invalid JSON body: {ex}")

    def parts(self):
        u = urlparse(self.path)
        segs = [unquote(s) for s in u.path.split("/") if s]
        return segs, parse_qs(u.query)

    # -- routes -------------------------------------------------------------
    def do_OPTIONS(self):
        self.send_response(204)
        self._cors()
        self.end_headers()

    def do_GET(self):
        segs, qs = self.parts()
        try:
            if not segs:
                return self.send_text(INDEX_HTML, "text/html; charset=utf-8")
            if segs == ["health"]:
                return self.send_json({"ok": True, "utc": utc_now(), "queue_len": self.q.qsize(), "workers": self.workers,
                                       "running": self.store.running(), "jobs": len(self.store.jobs),
                                       "analysis_backend": "pandas+analyze_run" if analyze_module() else "stdlib"})
            if segs == ["docs"]:
                if not DOCS_FILE.exists():
                    return self.error("docs/SCIENTIST_API.md missing", 404)
                return self.send_text(DOCS_FILE.read_text(encoding="utf-8"), "text/markdown; charset=utf-8")
            if segs == ["runs"] or segs == ["jobs"]:
                return self.send_json({"jobs": self.store.listing(), "queue_len": self.q.qsize(), "running": self.store.running()})
            if segs[0] in ("runs", "jobs") and len(segs) >= 2:
                job, run = self.store.find(segs[1])
                if job is None:
                    return self.error(f"unknown run or job id '{segs[1]}'", 404)
                if len(segs) == 2:
                    return self.send_json(run if run is not None else job)
                if len(segs) == 4 and segs[2] == "files" and run is not None:
                    name = segs[3][:-4] if segs[3].endswith(".csv") else segs[3]
                    if name not in DOWNLOADABLE:
                        return self.error("file must be one of agents, births, deaths, population, commands (.csv)", 404)
                    rd = run_dir_for(run)
                    if rd is None or not (rd / f"{name}.csv").exists():
                        return self.error("run has no CSV yet (status %s)" % run["status"], 404)
                    return self.send_file(rd / f"{name}.csv")
                return self.error("not found", 404)
            if segs == ["live"]:
                return self.send_json(live_view(self.store))
            if segs == ["notes"]:
                try:
                    limit = max(1, min(int(qs.get("limit", ["200"])[0]), 5000))
                except ValueError:
                    raise BadRequest("limit: an integer")
                return self.send_json({"notes": read_notes(limit)})
            if segs == ["control"]:
                return self.send_json({"control_txt": read_text_if(CONTROL_FILE), "file": rel_posix(CONTROL_FILE)})
            return self.error("not found", 404)
        except BadRequest as ex:
            return self.error(str(ex))
        except Exception as ex:
            return self.error(f"{type(ex).__name__}: {ex}", 500)

    def do_POST(self):
        segs, _ = self.parts()
        try:
            body = self.read_json()
            if segs == ["runs"] or segs == ["jobs"]:
                spec = validate_job(body)
                job = self.store.new_job(spec)
                for r in job["runs"]:
                    self.q.put((job["job_id"], r["run_id"]))
                return self.send_json({"job_id": job["job_id"], "label": job["label"], "queue_len": self.q.qsize(),
                                       "runs": [{"run_id": r["run_id"], "mode": r["mode"], "seed": r["seed"], "status": r["status"]}
                                                for r in job["runs"]]}, 202)
            if segs == ["control"]:
                line, kind = validate_control(body.get("command"))
                n = append_control(line)
                print(f"[control] {line}", flush=True)
                return self.send_json({"ok": True, "command": line, "kind": kind, "file": rel_posix(CONTROL_FILE), "line_no": n})
            if segs == ["notes"]:
                text = body.get("text")
                if not isinstance(text, str) or not text.strip() or len(text) > 4000:
                    raise BadRequest("text: non-empty string, <= 4000 chars")
                author = str(body.get("author") or "anonymous")[:60]
                return self.send_json({"ok": True, "note": append_note(text.strip(), author)}, 201)
            return self.error("not found", 404)
        except BadRequest as ex:
            return self.error(str(ex))
        except Exception as ex:
            return self.error(f"{type(ex).__name__}: {ex}", 500)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8800)
    ap.add_argument("--workers", type=int, default=1, help="parallel headless runs (1..2; 1 recommended next to the live demo)")
    ap.add_argument("--python", default=sys.executable, help="interpreter used for Tools/run_sim.py (default: this one)")
    ap.add_argument("--no-pandas", action="store_true", help="force the stdlib statistics path (for testing the fallback)")
    ap.add_argument("--control-file", default=None, help="control file that POST /control appends to (default Saved/control.txt, the live sim's)")
    args = ap.parse_args()
    if args.control_file:
        globals()["CONTROL_FILE"] = Path(args.control_file)
    if not 1 <= args.workers <= 2:
        sys.exit("--workers must be 1 or 2")
    if args.no_pandas:
        os.environ["SW_EXPERIMENT_NO_PANDAS"] = "1"
    if not RUN_SIM.exists():
        sys.exit(f"missing {RUN_SIM}")
    store = Store()
    q = queue.Queue()
    for i in range(args.workers):
        Worker(store, q, args.python, f"worker-{i + 1}").start()
    Handler.store, Handler.q, Handler.workers = store, q, args.workers
    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    srv.daemon_threads = True
    print(f"[service] Symbiotic World experiment service on http://{args.host}:{args.port}/  workers={args.workers}  "
          f"python={args.python}  jobs loaded={len(store.jobs)}  analysis={'pandas+analyze_run' if analyze_module() else 'stdlib'}", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()
        print("[service] stopped", flush=True)


if __name__ == "__main__":
    main()
