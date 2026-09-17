"""The lab's duty cycle: the field team is either in the field or in a meeting, never both.

The phase lives in `lab_meta` (like the dashboard's `live_collect` switch), so the live observer,
whoever runs meetings (`loop` or a `session` on the same database) and the dashboard all read the
same state without talking to each other.

    field                the eight go out, witness organisms and mint "witnessed by <Name>" evidence
    meeting-requested    the field phase is over: the team is at camp, waiting for a meeting to open
    meeting              a meeting runner picked the request up and is running it; the team holds at camp

Transitions, and who makes them:

    field -> meeting-requested   the observer, after FIELD_WINDOWS evidence windows of fieldwork
    meeting-requested -> meeting the meeting runner, when it opens the meeting (begin_meeting)
    meeting -> field             the meeting runner, when the meeting ends (end_meeting)
    meeting-requested -> field   the observer, after STALL_WINDOWS more windows with nobody picking
                                 the request up, so a lone observer never stalls at camp

Instrument evidence (the god-view window statistics) is minted in both phases: those are
instruments, not people. Only the witnessed rows pause during a meeting.

Sim time is counted in evidence windows (`live_observer.WINDOW_S`, 60 sim-seconds), so the cycle
is the same length whatever the sim's time scale.
"""
import os
import time

from . import db

FIELD = "field"
MEETING_REQUESTED = "meeting-requested"
MEETING = "meeting"

# Sim-minutes of fieldwork before the team is called back to camp, and how long a request waits
# before the observer gives up and returns to the field. Both are overridable so a recorded take can
# show a whole cycle inside a few minutes (LAB_FIELD_WINDOWS=2) without changing the lab's own cadence.
FIELD_WINDOWS = max(1, int(os.environ.get("LAB_FIELD_WINDOWS", "5")))
STALL_WINDOWS = max(1, int(os.environ.get("LAB_STALL_WINDOWS", "2")))
OBSERVER_FRESH_S = 180.0   # an observer heartbeat older than this means nothing is observing

# lab_meta keys
K_PHASE = "field_phase"
K_SINCE = "field_phase_since_window"
K_MEETING = "field_phase_meeting"
K_WINDOW = "field_window"          # the observer's latest evidence window (for the dashboard)
K_SEEN = "live_observer_seen"      # observer heartbeat, unix seconds


def _int(con, key, default=0):
    try:
        return int(float(db.get_meta(con, key, default)))
    except (TypeError, ValueError):
        return default


def read(con):
    """{phase, since, meeting, window}: the shared state, with defaults for a fresh database."""
    return {
        "phase": db.get_meta(con, K_PHASE, FIELD) or FIELD,
        "since": _int(con, K_SINCE, 0),
        "meeting": _int(con, K_MEETING, 0),
        "window": _int(con, K_WINDOW, 0),
    }


def in_field(phase):
    """True while the team works the arena; false at camp (requested or in session)."""
    return phase == FIELD


def write(con, phase, since=None, meeting=None):
    db.set_meta(con, K_PHASE, phase)
    if since is not None:
        db.set_meta(con, K_SINCE, int(since))
    if meeting is not None:
        db.set_meta(con, K_MEETING, int(meeting))
    con.commit()


def ensure(con, window):
    """Called by the observer at hello. A new run restarts the sim clock at window 0, so a phase
    that claims to have started later is re-stamped; the phase itself survives the reset (a meeting
    in session is not interrupted by a sim restart)."""
    st = read(con)
    if db.get_meta(con, K_PHASE) is None:
        write(con, FIELD, since=window, meeting=0)
        return read(con)
    if st["since"] > window:
        write(con, st["phase"], since=window)
    db.set_meta(con, K_WINDOW, int(window))
    con.commit()
    return read(con)


def next_meeting_number(con):
    row = con.execute("SELECT COUNT(*) FROM meetings").fetchone()
    return int(row[0]) + 1 if row else 1


def advance(con, window):
    """Called by the observer at every window boundary. Moves the clock on and makes the two
    transitions the observer owns. Returns a one-line report for the bridge, or None."""
    db.set_meta(con, K_WINDOW, int(window))
    st = read(con)
    elapsed = window - st["since"]
    if st["phase"] == FIELD and elapsed >= FIELD_WINDOWS:
        n = next_meeting_number(con)
        write(con, MEETING_REQUESTED, since=window, meeting=n)
        return f"the team returns to camp; meeting {n} opens"
    if st["phase"] == MEETING_REQUESTED and elapsed >= STALL_WINDOWS:
        write(con, FIELD, since=window)
        return "no meeting opened in time; the team returns to the field"
    con.commit()
    return None


def heartbeat(con, now=None):
    """The observer says it is alive, so `loop` knows to wait for the cycle instead of the clock."""
    db.set_meta(con, K_SEEN, f"{now if now is not None else time.time():.0f}")
    con.commit()


def observer_live(con, now=None):
    seen = _int(con, K_SEEN, 0)
    return seen > 0 and (now if now is not None else time.time()) - seen < OBSERVER_FRESH_S


# -- the meeting runner's side ------------------------------------------------

def begin_meeting(con):
    """A meeting is opening: the team holds at camp until it ends, whether or not the field phase
    asked for it (a manual `session` mid-fieldwork calls the team in too)."""
    st = read(con)
    write(con, MEETING, since=st["window"], meeting=max(st["meeting"], next_meeting_number(con)))
    return read(con)


def end_meeting(con):
    """The meeting is over: back to the field, and the field clock starts now."""
    st = read(con)
    write(con, FIELD, since=st["window"])
    return read(con)


def wait_for_turn(con, fallback_s, poll_s=2.0, now=time.time, sleep=time.sleep):
    """Block until the duty cycle asks for a meeting, or until the fallback interval passes with
    nothing observing. Returns "requested" or "interval".

    With a live observer the lab follows the cycle: meetings happen when the team comes back to
    camp, not on a wall-clock timer. With no observer (no sim attached) the interval is the timer,
    exactly as before.
    """
    deadline = now() + fallback_s
    while True:
        if read(con)["phase"] == MEETING_REQUESTED:
            return "requested"
        if not observer_live(con, now()) and now() >= deadline:
            return "interval"
        sleep(poll_s)


def describe(con):
    """Read-only summary for the dashboard: phase, a short label and what is left of it."""
    st = read(con)
    left = max(FIELD_WINDOWS - (st["window"] - st["since"]), 0)
    if st["phase"] == FIELD:
        label = f"field — {left} of {FIELD_WINDOWS} windows left"
    elif st["phase"] == MEETING_REQUESTED:
        label = f"team at camp — meeting {st['meeting']} requested"
    else:
        label = f"meeting {st['meeting']} in session — team at camp"
    return {"phase": st["phase"], "label": label, "windows_left": left,
            "meeting": st["meeting"], "window": st["window"], "field_windows": FIELD_WINDOWS}
