#!/usr/bin/env python3
"""Turn a recorded take's commands.csv into an .srt caption track.

Every control command the sim executed is logged with its wall-clock time
(`wall_utc`), so a take recorded with an external recorder (Game Bar, OBS) can be
captioned without timing anything by hand: caption time = wall_utc - recording start.

    python Tools/demo_captions.py --run Saved/SymbioticWorld/<run_id> \
        --start "2026-09-17 13:45:02" --out captions.srt

    python Tools/demo_captions.py --run <run_id> --video "C:/Users/me/Videos/Captures/take.mp4"
        # start time read from a Game Bar filename ("... 2026-09-17 13-45-02.mp4")

Nudge the whole track with --offset (seconds, may be negative) when the recorder
started a moment before or after the sim. Captions default to text derived from the
command; override any of them with a TSV file (command<TAB>text) via --texts.

Burn the result into the video with ffmpeg:
    ffmpeg -i take.mp4 -vf "subtitles=captions.srt" -c:a copy captioned.mp4
or keep them switchable:
    ffmpeg -i take.mp4 -i captions.srt -c copy -c:s mov_text captioned.mp4

Standard library only. Times in commands.csv are UTC; --start is local time by
default (use --start-utc for a UTC start).
"""
import argparse
import csv
import re
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Command -> caption. The first pattern that matches wins; {0} is the first capture group.
DEFAULT_TEXTS = [
    (r"^follow=Leviathan$", "The Leviathan hunts the river channel"),
    (r"^follow=Lumen$", "One Lumen: its action values shift with every reward"),
    (r"^follow=Tecton$", "A Tecton grazing and reshaping the terrain"),
    (r"^follow=none$", ""),
    (r"^follow=(.+)$", "{0} of the field team, observing"),
    (r"^drought=on$", "Drought: the river drops and predation pauses"),
    (r"^drought=off$", "The drought lifts"),
    (r"^speed=1(?:\.0+)?$", "Back to real time"),
    (r"^speed=(\d+(?:\.\d+)?)$", "Time scale {0}x: generations turn over"),
    (r"^cam=.*$", ""),
    (r"^set Look\.bShowHUD=0$", ""),
    (r"^set Look\.bShowHUD=1$", ""),
    (r"^set .*$", ""),
    (r"^note=(.*)$", "{0}"),
    (r"^mode=([ABCN])$", "Mode {0}"),
    (r"^reset.*$", "Run restarted"),
]


def caption_for(command, overrides):
    if command in overrides:
        return overrides[command]
    for pattern, text in DEFAULT_TEXTS:
        m = re.match(pattern, command, re.I)
        if m:
            return text.format(*m.groups()) if m.groups() else text
    return ""


def parse_start(args):
    if args.start:
        stamp = datetime.strptime(args.start.strip(), "%Y-%m-%d %H:%M:%S")
        return stamp.replace(tzinfo=timezone.utc) if args.start_utc else stamp.astimezone()
    if args.video:
        name = Path(args.video).name
        m = re.search(r"(\d{4})-(\d{2})-(\d{2})[ _](\d{2})-(\d{2})-(\d{2})", name)
        if not m:
            sys.exit(f"cannot read a start time from {name!r}; pass --start \"YYYY-MM-DD HH:MM:SS\"")
        return datetime(*(int(g) for g in m.groups())).astimezone()
    sys.exit("need --start or --video")


def srt_time(seconds):
    if seconds < 0:
        seconds = 0.0
    whole = int(seconds)
    ms = int(round((seconds - whole) * 1000))
    return f"{whole // 3600:02d}:{whole % 3600 // 60:02d}:{whole % 60:02d},{ms:03d}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run", required=True, help="run directory holding commands.csv")
    ap.add_argument("--start", help='recording start, "YYYY-MM-DD HH:MM:SS" (local unless --start-utc)')
    ap.add_argument("--start-utc", action="store_true", help="--start is UTC")
    ap.add_argument("--video", help="video file whose name carries the start time (Game Bar naming)")
    ap.add_argument("--out", default="captions.srt", help="output .srt (default captions.srt)")
    ap.add_argument("--offset", type=float, default=0.0, help="shift every caption by N seconds")
    ap.add_argument("--hold", type=float, default=4.0, help="seconds each caption stays up (default 4)")
    ap.add_argument("--texts", help="TSV overrides: command<TAB>caption text")
    args = ap.parse_args()

    run = Path(args.run)
    if not run.is_absolute():
        run = ROOT / run
    commands = run / "commands.csv"
    if not commands.exists():
        sys.exit(f"no commands.csv in {run}")

    overrides = {}
    if args.texts:
        for line in Path(args.texts).read_text(encoding="utf-8").splitlines():
            if not line.strip() or line.startswith("#") or "\t" not in line:
                continue
            key, _, text = line.partition("\t")
            overrides[key.strip()] = text.strip()

    start = parse_start(args)
    rows = []
    with open(commands, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if not row.get("result", "").startswith("ok"):
                continue   # rejected commands never happened on screen
            text = caption_for(row["command"].strip(), overrides)
            if not text:
                continue   # camera moves and HUD toggles are seen, not narrated
            when = datetime.strptime(row["wall_utc"].strip()[:19], "%Y-%m-%dT%H:%M:%S").replace(tzinfo=timezone.utc)
            rows.append(((when - start).total_seconds() + args.offset, text, row["sim_time"]))

    rows.sort(key=lambda r: r[0])
    out = Path(args.out)
    with open(out, "w", encoding="utf-8") as f:
        for i, (at, text, sim_time) in enumerate(rows, 1):
            end = at + args.hold
            if i < len(rows):
                end = min(end, rows[i][0] - 0.1)   # never overlap the next caption
            f.write(f"{i}\n{srt_time(at)} --> {srt_time(max(end, at + 0.5))}\n{text}\n\n")

    print(f"{out}: {len(rows)} caption(s) from {commands}")
    for at, text, sim_time in rows:
        print(f"  {srt_time(at)}  (sim t={float(sim_time):.1f}s)  {text}")
    if rows and rows[0][0] < 0:
        print("\nfirst caption is before the video starts: check --start, or nudge with --offset")


if __name__ == "__main__":
    main()
