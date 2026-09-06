"""Symbiotic Lab CLI.

  python -m Lab.lab session --meetings 2 --llm ollama     # a research session
  python -m Lab.lab session --meetings 2 --llm mock       # no model needed
  python -m Lab.lab ingest <run_dir> [...]                # ingest existing runs
  python -m Lab.lab run-queued                            # execute experiments (sim machine)
  python -m Lab.lab report                                # regenerate report from the DB
  python -m Lab.lab textbook                              # print the inherited object

A session: seed textbook -> [meeting -> execute queued experiments] x N ->
consolidation -> lab report + transcript in Lab/reports/.
"""
import argparse
import json
import os
from pathlib import Path

from . import datasci, db, evidence, evolution, memory, report, runner, seed_content
from .llm import make_llm
from .meeting import MeetingRunner
from .profiles import load_profiles


def cmd_session(args):
    con = db.connect(args.db)
    profiles = load_profiles(con=con)   # overlay evolved parameters of this generation
    llm = make_llm(args.llm)
    seed_content.seed(con)
    for d in args.ingest or []:
        evidence.ingest_run(con, Path(d))
    mr = MeetingRunner(con, profiles, llm)
    for i in range(args.meetings):
        mr.run(kind="generation-boundary" if i == args.meetings - 1 else "regular")
        if runner.engine_available() or runner.service_client():
            runner.execute_queued(con)
        else:
            n = con.execute("SELECT COUNT(*) FROM experiments WHERE status='queued'").fetchone()[0]
            if n:
                print(f"  [runner] {n} experiment(s) queued; no UE engine or service -> awaiting-sim")
                con.execute("UPDATE experiments SET status='awaiting-sim' WHERE status='queued'")
                con.commit()
    print("\n=== CONSOLIDATION ===")
    memory.condense_notebooks(con)
    evolution.evolve(con, profiles)     # close the lab generation, mutate temperaments
    label = None
    annex = datasci.annex(con, label or "session")   # Vega: report-only, non-voting
    rp, tp = report.generate(con, annex_lines=annex)
    print(f"\nlab report: {rp}\ntranscript: {tp}")


def cmd_ingest(args):
    con = db.connect(args.db)
    for d in args.runs:
        eids = evidence.ingest_run(con, Path(d))
        print(f"{d}: {len(eids)} evidence objects")
    evidence.cross_run_stats(con)


def cmd_run_queued(args):
    con = db.connect(args.db)
    con.execute("UPDATE experiments SET status='queued' WHERE status='awaiting-sim'")
    con.commit()
    if not runner.engine_available() and runner.service_client() is None:
        raise SystemExit(
            "UE engine not found here and no experiment service reachable. Either run "
            "on the sim machine, or set LAB_SIM_SERVICE=<host[:port]> to a running "
            "Tools/experiment_service.py (docs/SCIENTIST_API.md).")
    n = runner.execute_queued(con)
    print(f"executed {n} experiment(s)")
    rp, tp = report.generate(con)
    print(f"lab report: {rp}")


def cmd_report(args):
    con = db.connect(args.db)
    rp, tp = report.generate(con)
    print(f"lab report: {rp}\ntranscript: {tp}")


def cmd_textbook(args):
    con = db.connect(args.db)
    print(json.dumps(memory.textbook(con), indent=2))


def cmd_trajectory(args):
    from . import trajectory
    con = db.connect(args.db)
    for line in trajectory.report_section(con):
        print(line)


def cmd_victory(args):
    from . import victory
    con = db.connect(args.db)
    victory.sync_questions(con)
    for row in victory.scorecard(con):
        cite = (" [" + ", ".join(row["evidence_ids"]) + "]") if row["evidence_ids"] else ""
        print(f"{row['id']} {row['status'].upper():8s} {row['text']}")
        print(f"       {row['why']}{cite}")


def cmd_ui(args):
    from . import ui_server
    from .config import DB_PATH
    ui_server.serve(db_path=args.db or DB_PATH, port=args.port, host=args.host)


def cmd_observe(args):
    from . import live_observer
    from .config import DB_PATH
    live_observer.serve(db_path=args.db or DB_PATH, port=args.port, host=args.host,
                        manage=args.manage, embody=args.embody)


def main():
    ap = argparse.ArgumentParser(prog="Lab", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", default=None, help="path to lab.sqlite (default Lab/lab.sqlite)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("session", help="run a research session")
    s.add_argument("--meetings", type=int, default=2)
    s.add_argument("--llm", choices=["ollama", "mock", "claude", "openrouter"],
                   default=os.environ.get("LAB_LLM", "ollama"))
    s.add_argument("--ingest", nargs="*", help="run directories to ingest first")
    s.set_defaults(fn=cmd_session)

    s = sub.add_parser("ingest", help="ingest run directories")
    s.add_argument("runs", nargs="+")
    s.set_defaults(fn=cmd_ingest)

    s = sub.add_parser("run-queued", help="execute queued/awaiting experiments (sim machine)")
    s.set_defaults(fn=cmd_run_queued)

    s = sub.add_parser("report", help="regenerate the lab report from the DB")
    s.set_defaults(fn=cmd_report)

    s = sub.add_parser("textbook", help="print the inherited object (registry + open questions)")
    s.set_defaults(fn=cmd_textbook)

    s = sub.add_parser("victory",
                       help="score the spec's six victory conditions against the evidence")
    s.set_defaults(fn=cmd_victory)

    s = sub.add_parser("trajectory",
                       help="print the discourse trajectory (is the conversation improving?)")
    s.set_defaults(fn=cmd_trajectory)

    s = sub.add_parser("ui", help="serve the read-only lab dashboard (LAN-visible)")
    s.add_argument("--port", type=int, default=8765)
    s.add_argument("--host", default="0.0.0.0")
    s.set_defaults(fn=cmd_ui)

    s = sub.add_parser("observe",
                       help="join the live sim through the policy bridge")
    s.add_argument("--port", type=int, default=9000)
    s.add_argument("--host", default="0.0.0.0")
    s.add_argument("--manage", action="store_true",
                   help="drive assigned organisms with the lab's population-management "
                        "doctrine (default: observe only)")
    s.add_argument("--embody", action="store_true",
                   help="mode toggle: the seven scientists walk the arena as virtual "
                        "bodies and mint witnessed-only evidence (default: plain "
                        "instrument observer)")
    s.set_defaults(fn=cmd_observe)

    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
