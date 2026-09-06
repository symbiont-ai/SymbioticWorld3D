#!/usr/bin/env python3
"""Append commands to the running sim's live control file, or show what it executed.

  python3 Tools/control.py "drought=on"                          # appends one validated line to Saved/control.txt
  python3 Tools/control.py "set Lumen.ReproThreshold=85" "note=repro sweep, step 2"
  python3 Tools/control.py --file /path/to/control.txt "speed=50"
  python3 Tools/control.py --check "mode=X"                      # validate only, write nothing (exit 2 when a line is bad)
  python3 Tools/control.py --tail 10                             # last 10 rows of the newest run's commands.csv

The sim polls the file every Settings.ControlFilePollSec (2 s) of wall time and executes every
newly appended line once, in order; lines that existed when it started are ignored. Grammar,
semantics and which settings are live are in docs/CONTROL_FILE.md. Standard library only.
"""
import argparse
import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FILE = ROOT / "Saved" / "control.txt"
DEFAULT_RUNS = ROOT / "Saved" / "SymbioticWorld"

ONOFF = r"(on|off|1|0|true|false)"
SCOPES = r"(settings|lumen|tecton|genome|founder|look)"
# One regex per keyword, mirroring ASWWorldManager::RunControlCommand (case-insensitive, trimmed).
RULES = [
    ("drought", re.compile(r"^drought\s*=\s*(on|off|toggle|1|0|true|false)$", re.I)),
    ("pause", re.compile(r"^pause\s*=\s*" + ONOFF + r"$", re.I)),
    ("speed", re.compile(r"^speed\s*=\s*(\d+(\.\d*)?|\.\d+)$", re.I)),
    ("reset", re.compile(r"^reset(\s+seed\s*=\s*-?\d+)?$", re.I)),
    ("mode", re.compile(r"^mode\s*=\s*[abcn]$", re.I)),
    ("note", re.compile(r"^note\s*=.*$", re.I | re.S)),
]
SET_ITEM = re.compile(r"^" + SCOPES + r"\.[A-Za-z_]\w*\s*=\s*\S.*$", re.I)
GRAMMAR = ("drought=on|off|toggle, speed=<float>, pause=on|off, set <Scope.Field>=<value>, "
           "reset, reset seed=<int>, mode=A|B|C|N, note=<text>")


def validate(line):
    """Return None when the line is a valid command (or a comment), else the reason it is not."""
    if "\n" in line or "\r" in line:
        return "a command is one line"
    s = line.strip()
    if not s:
        return "empty line"
    if s.startswith("#"):
        return None   # comment: the sim skips it
    low = s.lower()
    if low == "set" or low.startswith("set "):
        spec = s[3:].strip()
        items = [it.strip() for it in spec.split(";")]
        items = [it for it in items if it]
        if not items:
            return "set needs <Scope.Field>=<value> (Scope: Settings, Lumen, Tecton, Genome, Look)"
        for it in items:
            if not SET_ITEM.match(it):
                return "bad set item '%s': expected Scope.Field=value with Scope in Settings/Lumen/Tecton/Genome/Look" % it
        return None
    for _, rx in RULES:
        if rx.match(s):
            return None
    return "unknown or malformed command; grammar: " + GRAMMAR


def append_lines(path, lines):
    path.parent.mkdir(parents=True, exist_ok=True)
    prefix = b""
    if path.exists() and path.stat().st_size > 0:
        with open(path, "rb") as f:
            f.seek(-1, 2)
            if f.read(1) != b"\n":
                prefix = b"\n"   # never glue onto a partial last line: the sim only executes newline-terminated lines
    with open(path, "ab") as f:
        f.write(prefix + "".join(l.strip() + "\n" for l in lines).encode("utf-8"))


def newest_run(runs_root):
    if not runs_root.is_dir():
        return None
    dirs = [p for p in runs_root.iterdir() if p.is_dir()]
    if not dirs:
        return None
    return max(dirs, key=lambda p: p.name)   # run ids start with YYYYMMDD-HHMMSS


def tail(run_dir, n):
    path = run_dir / "commands.csv"
    if not path.exists():
        print("no commands.csv in %s (run written by a build without the control file, or logs off)" % run_dir)
        return 1
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        rows = list(csv.DictReader(f))
    print("%s: %d command(s) executed" % (path, len(rows)))
    for r in rows[-n:]:
        print("  t=%8s  %s  %s -> %s" % (r.get("sim_time", "?"), r.get("wall_utc", "?"), r.get("command", ""), r.get("result", "")))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("commands", nargs="*", help='commands to append, one per argument, e.g. "drought=on"')
    ap.add_argument("--file", default=str(DEFAULT_FILE), metavar="PATH", help="control file (default Saved/control.txt under the repo)")
    ap.add_argument("--check", action="store_true", help="validate the commands and exit; write nothing")
    ap.add_argument("--tail", type=int, default=None, metavar="N", help="print the last N rows of the newest run's commands.csv")
    ap.add_argument("--runs", default=str(DEFAULT_RUNS), metavar="DIR", help="run directories root for --tail (default Saved/SymbioticWorld)")
    ap.add_argument("--run", default=None, metavar="DIR", help="a specific run directory for --tail (default: newest)")
    args = ap.parse_args()

    if not args.commands and args.tail is None:
        ap.error("give at least one command, or --tail N")

    rc = 0
    if args.commands:
        bad = [(c, validate(c)) for c in args.commands]
        bad = [(c, why) for c, why in bad if why]
        for c, why in bad:
            print("rejected: %r: %s" % (c, why), file=sys.stderr)
        if bad:
            return 2
        if args.check:
            print("ok: %d command(s) valid" % len(args.commands))
        else:
            path = Path(args.file)
            append_lines(path, args.commands)
            for c in args.commands:
                print("appended: %s" % c.strip())
            print("-> %s (the sim executes new lines within ControlFilePollSec, default 2 s; see the UE log 'control:' lines or --tail)" % path)

    if args.tail is not None:
        run_dir = Path(args.run) if args.run else newest_run(Path(args.runs))
        if run_dir is None:
            print("no run directories under %s" % args.runs, file=sys.stderr)
            return 1
        rc = tail(run_dir, max(args.tail, 1))
    return rc


if __name__ == "__main__":
    sys.exit(main())
