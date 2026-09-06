#!/usr/bin/env python3
"""Thin client for the Symbiotic World experiment service (Tools/experiment_service.py).
Standard library only; runs on any machine that can reach the host over the LAN.

Python:
    from scientist_client import Client
    c = Client("10.0.0.1")                      # host running Tools/experiment_service.py, port 8800
    job = c.submit_runs(["C", "N"], [1, 2, 3], duration=300, set_spec="Settings.PatchRegenPerSec=8")
    job = c.wait(job["job_id"])                 # blocks until every run is done or failed
    print(job["comparison"])                    # Welch t-test of end-of-run mean alpha, C vs N
    c.live(); c.control("drought=on"); c.note("regen 8 looks viable", author="scientist-1")

CLI (same operations):
    python3 Tools/scientist_client.py --host 10.0.0.1 runs --mode C N --seeds 1 2 3 --duration 300 --set "Settings.PatchRegenPerSec=8" --wait
    python3 Tools/scientist_client.py --host 10.0.0.1 wait job-20260906-161500-ab12
    python3 Tools/scientist_client.py --host 10.0.0.1 run job-20260906-161500-ab12-r1
    python3 Tools/scientist_client.py --host 10.0.0.1 csv job-20260906-161500-ab12-r1 population --out pop.csv
    python3 Tools/scientist_client.py --host 10.0.0.1 live
    python3 Tools/scientist_client.py --host 10.0.0.1 control "drought=on"
    python3 Tools/scientist_client.py --host 10.0.0.1 note "regen 8 looks viable" --author scientist-1
    python3 Tools/scientist_client.py --host 10.0.0.1 notes
Every command prints the JSON the service returned. Exit code 1 on an HTTP error.
"""
import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8800


class ServiceError(Exception):
    def __init__(self, status, message):
        super().__init__(f"HTTP {status}: {message}")
        self.status, self.message = status, message


class Client:
    def __init__(self, host=DEFAULT_HOST, port=DEFAULT_PORT, timeout=30.0):
        self.base = f"http://{host}:{port}"
        self.timeout = timeout

    # -- transport ------------------------------------------------------------
    def _request(self, method, path, body=None, raw=False):
        data = None
        headers = {"Accept": "application/json"}
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(self.base + path, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                payload = resp.read()
        except urllib.error.HTTPError as ex:
            payload = ex.read()
            try:
                msg = json.loads(payload.decode("utf-8")).get("error", payload.decode("utf-8", "replace"))
            except (ValueError, AttributeError):
                msg = payload.decode("utf-8", "replace")
            raise ServiceError(ex.code, msg) from None
        except urllib.error.URLError as ex:
            raise ServiceError(0, f"cannot reach {self.base}: {ex.reason}") from None
        if raw:
            return payload
        return json.loads(payload.decode("utf-8"))

    def get(self, path, raw=False):
        return self._request("GET", path, raw=raw)

    def post(self, path, body):
        return self._request("POST", path, body)

    # -- API ------------------------------------------------------------------
    def submit_runs(self, modes, seeds, duration=600, speed=200, set_spec="", label="", policy_file=None, wall_timeout=None):
        """POST /runs. modes: 'C' or ['C', 'N']; seeds: list of ints (1..8). Returns {job_id, runs:[{run_id,status}]}."""
        body = {"mode": modes, "seeds": list(seeds), "duration": duration, "speed": speed,
                "set": set_spec or "", "label": label or "", "policy_file": policy_file}
        if wall_timeout is not None:
            body["wall_timeout"] = wall_timeout
        return self.post("/runs", body)

    def list_runs(self):
        return self.get("/runs")

    def get_job(self, job_id):
        return self.get(f"/runs/{urllib.parse.quote(job_id)}")

    def get_run(self, run_id):
        """GET /runs/<run_id>: the run record with its summary (None until the run is done)."""
        return self.get(f"/runs/{urllib.parse.quote(run_id)}")

    def wait(self, job_id, poll_s=5.0, timeout_s=None, verbose=True):
        """Poll until the job is done or failed. Returns the job JSON (with 'comparison')."""
        t0 = time.time()
        last = None
        while True:
            job = self.get_job(job_id)
            state = ",".join(f"{r['mode']}{r['seed']}:{r['status']}" for r in job["runs"])
            if verbose and state != last:
                print(f"[{time.time() - t0:5.0f}s] {job['status']:8s} {state}", file=sys.stderr, flush=True)
                last = state
            if job["status"] in ("done", "failed"):
                return job
            if timeout_s is not None and time.time() - t0 > timeout_s:
                raise TimeoutError(f"job {job_id} still {job['status']} after {timeout_s} s")
            time.sleep(poll_s)

    def download_csv(self, run_id, name, dest=None):
        """GET /runs/<run_id>/files/<name>.csv (name: agents, births, deaths, population). Returns bytes or writes dest."""
        data = self.get(f"/runs/{urllib.parse.quote(run_id)}/files/{name}.csv", raw=True)
        if dest:
            with open(dest, "wb") as f:
                f.write(data)
            return dest
        return data

    def live(self):
        return self.get("/live")

    def control(self, command):
        """POST /control: one command line (drought=on|off, speed=<float>, set <Scope.Field>=<value>, reset, reset seed=<int>, mode=A|B|C|N, note=<text>)."""
        return self.post("/control", {"command": command})

    def note(self, text, author="scientist"):
        return self.post("/notes", {"text": text, "author": author})

    def notes(self, limit=200):
        return self.get(f"/notes?limit={int(limit)}")

    def health(self):
        return self.get("/health")


# module-level convenience functions (one default client, configured by configure())
_default = Client()


def configure(host=DEFAULT_HOST, port=DEFAULT_PORT, timeout=30.0):
    global _default
    _default = Client(host, port, timeout)
    return _default


def submit_runs(*a, **k):
    return _default.submit_runs(*a, **k)


def wait(job_id, **k):
    return _default.wait(job_id, **k)


def get_run(run_id):
    return _default.get_run(run_id)


def live():
    return _default.live()


def control(cmd):
    return _default.control(cmd)


def note(text, author="scientist"):
    return _default.note(text, author)


# ---------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=DEFAULT_HOST, help="host running Tools/experiment_service.py")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--timeout", type=float, default=60.0, help="HTTP timeout per request (s)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("runs", help="submit runs (POST /runs)")
    p.add_argument("--mode", nargs="+", default=["C"], help="A B C N, one or more")
    p.add_argument("--seeds", nargs="+", type=int, default=[1])
    p.add_argument("--duration", type=float, default=600)
    p.add_argument("--speed", type=float, default=200)
    p.add_argument("--set", dest="set_spec", default="", help='"Settings.PatchRegenPerSec=8;Lumen.ReproThreshold=85" (no commas)')
    p.add_argument("--label", default="")
    p.add_argument("--policy-file", default=None)
    p.add_argument("--wall-timeout", type=float, default=None)
    p.add_argument("--wait", action="store_true", help="block until the job finishes and print the job JSON")
    p.add_argument("--poll", type=float, default=5.0)

    p = sub.add_parser("list", help="GET /runs")
    p = sub.add_parser("wait", help="poll a job until done"); p.add_argument("job_id"); p.add_argument("--poll", type=float, default=5.0)
    p = sub.add_parser("run", help="GET /runs/<id> (run summary, or the job for a job_id)"); p.add_argument("run_id")
    p = sub.add_parser("csv", help="download a CSV of a run"); p.add_argument("run_id")
    p.add_argument("name", choices=["agents", "births", "deaths", "population"]); p.add_argument("--out", default=None)
    p = sub.add_parser("live", help="GET /live")
    p = sub.add_parser("control", help="POST /control"); p.add_argument("command")
    p = sub.add_parser("note", help="POST /notes"); p.add_argument("text"); p.add_argument("--author", default="scientist")
    p = sub.add_parser("notes", help="GET /notes"); p.add_argument("--limit", type=int, default=200)
    p = sub.add_parser("health", help="GET /health")
    p = sub.add_parser("docs", help="GET /docs (prints the markdown)")
    args = ap.parse_args(argv)

    c = Client(args.host, args.port, args.timeout)
    try:
        if args.cmd == "runs":
            out = c.submit_runs(args.mode, args.seeds, args.duration, args.speed, args.set_spec, args.label, args.policy_file, args.wall_timeout)
            if args.wait:
                out = c.wait(out["job_id"], poll_s=args.poll)
        elif args.cmd == "list":
            out = c.list_runs()
        elif args.cmd == "wait":
            out = c.wait(args.job_id, poll_s=args.poll)
        elif args.cmd == "run":
            out = c.get_run(args.run_id)
        elif args.cmd == "csv":
            dest = args.out or f"{args.run_id}_{args.name}.csv"
            c.download_csv(args.run_id, args.name, dest)
            out = {"saved": dest}
        elif args.cmd == "live":
            out = c.live()
        elif args.cmd == "control":
            out = c.control(args.command)
        elif args.cmd == "note":
            out = c.note(args.text, args.author)
        elif args.cmd == "notes":
            out = c.notes(args.limit)
        elif args.cmd == "health":
            out = c.health()
        elif args.cmd == "docs":
            sys.stdout.write(c.get("/docs", raw=True).decode("utf-8", "replace"))
            return 0
        else:
            ap.error("unknown command")
    except ServiceError as ex:
        print(f"error: {ex}", file=sys.stderr)
        return 1
    print(json.dumps(out, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
