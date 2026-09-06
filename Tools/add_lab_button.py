#!/usr/bin/env python3
"""Put a "Symbiotic Lab" button on the streamed world's player page.

Epic's Pixel Streaming player (what LAN viewers see on port 80) is served from
the engine plugin folder, not from this repo, so this script patches the built
page in place: it finds the signalling server's www directory and appends a
small floating button to every served .html page. The button opens the lab
dashboard (`python -m Lab.lab ui`, port 8765) on the same host, where the
scientists' conversations, experiments, and Vega's data reports live.

    python Tools/add_lab_button.py            # patch (idempotent, re-run safe)
    python Tools/add_lab_button.py --remove   # restore
    python Tools/add_lab_button.py --www <dir>  # explicit frontend dir

Standard library only. Run it on the host; refresh the browser afterwards —
no server restart needed (static files are read per request).
"""
import argparse
import os
import sys
from pathlib import Path

MARKER = "<!-- symbiotic-lab-button -->"
LAB_PORT = 8765

SNIPPET = f"""{MARKER}
<a id="symbiotic-lab-btn" target="_blank" rel="noopener"
   style="position:fixed;right:18px;bottom:18px;z-index:9999;
          font:600 14px/1 -apple-system,Segoe UI,Roboto,sans-serif;
          color:#0b1016;background:#3fd1ff;border-radius:22px;
          padding:11px 18px;text-decoration:none;letter-spacing:.04em;
          box-shadow:0 2px 14px rgba(63,209,255,.55)">&#129514; Symbiotic Lab</a>
<script>document.getElementById('symbiotic-lab-btn').href =
  'http://' + location.hostname + ':{LAB_PORT}';</script>
{MARKER}
"""


def default_www():
    ue = Path(os.environ.get("UE_ROOT", r"C:\Program Files\Epic Games\UE_5.7"))
    base = ue / "Engine/Plugins/Media/PixelStreaming2/Resources/WebServers/SignallingWebServer"
    for sub in ("www", "Public", "public"):
        d = base / sub
        if d.is_dir():
            return d
    return None


def patch(www, remove=False):
    pages = sorted(www.glob("*.html")) + sorted(www.glob("**/index.html"))
    if not pages:
        sys.exit(f"no .html pages under {www}")
    done = 0
    for page in dict.fromkeys(pages):          # dedupe, keep order
        text = page.read_text(encoding="utf-8", errors="replace")
        has = MARKER in text
        if remove:
            if has:
                head, _, rest = text.partition(MARKER)
                _, _, tail = rest.partition(MARKER)
                page.write_text(head + tail.lstrip("\n"), encoding="utf-8")
                print(f"restored {page}")
                done += 1
            continue
        if has:
            print(f"already patched {page}")
            continue
        if "</body>" in text:
            text = text.replace("</body>", SNIPPET + "</body>", 1)
        else:
            text += SNIPPET
        page.write_text(text, encoding="utf-8")
        print(f"patched {page}")
        done += 1
    verb = "restored" if remove else "patched"
    print(f"{verb} {done} page(s). Viewers now reach the lab at http://<host>:{LAB_PORT} "
          f"from the button (keep `python -m Lab.lab ui` running there).")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--www", help="the signalling server's static frontend directory")
    ap.add_argument("--remove", action="store_true", help="remove the button again")
    args = ap.parse_args()
    www = Path(args.www) if args.www else default_www()
    if not www or not www.is_dir():
        sys.exit("frontend dir not found — pass --www <dir> (the SignallingWebServer's "
                 "www/ folder; run get_ps_servers.bat first, see start_stream_server.bat)")
    patch(www, remove=args.remove)


if __name__ == "__main__":
    main()
