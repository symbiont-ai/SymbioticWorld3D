#!/usr/bin/env python
"""Zero-click asset fetch: CC0 rock / cliff / moss / fern / grass scans from
Poly Haven via its public API (no login, no clicks).

  python Tools/fetch_polyhaven.py                # default list, 2k, FBX + textures
  python Tools/fetch_polyhaven.py --res 1k       # smaller
  python Tools/fetch_polyhaven.py --ids rock_face_01 moss_01

Files land in AssetSources/polyhaven/<asset_id>/ (gitignored). Import later with
Tools/import_assets.py. License: CC0 (https://polyhaven.com/license).
"""
import argparse
import json
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "AssetSources" / "polyhaven"
API = "https://api.polyhaven.com"

# Models first (cliffs, boulders, groundcover), then a few tileable surfaces.
MODELS = [
    "rock_face_01", "rock_face_02", "mountainside", "coastal_cliff_01", "coastal_cliff_02",
    "namaqualand_cliff_01", "namaqualand_cliff_02", "namaqualand_boulder_02", "namaqualand_boulder_03",
    "namaqualand_boulder_04", "namaqualand_boulder_05", "boulder_01", "rock_07", "rock_09",
    "rock_moss_set_01", "rock_moss_set_02", "moss_01", "fern_02", "grass_medium_01", "grass_medium_02",
    "shrub_01", "shrub_02", "shrub_03",
]
TEXTURES = ["mossy_rock", "rock_pitted_mossy", "forest_floor", "cliff_side", "lichen_rock", "brown_mud_leaves_01"]


UA = {"User-Agent": "SymbioticWorld-fetch/1.0 (+https://polyhaven.com/license)"}


def _open(url, timeout):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=timeout)


def get_json(url):
    with _open(url, 60) as r:
        return json.loads(r.read().decode("utf-8"))


def download(url, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.stat().st_size > 0:
        return path.stat().st_size
    tmp = path.with_suffix(path.suffix + ".part")
    with _open(url, 120) as r, open(tmp, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
    tmp.rename(path)
    return path.stat().st_size


def fetch_model(aid, res):
    info = get_json(f"{API}/files/{aid}")
    fbx = info.get("fbx", {}).get(res, {}).get("fbx")
    if not fbx:
        return f"{aid}: no fbx/{res}"
    total = download(fbx["url"], DEST / aid / Path(fbx["url"]).name)
    n = 1
    for rel, meta in fbx.get("include", {}).items():
        total += download(meta["url"], DEST / aid / rel)
        n += 1
    return f"{aid}: {n} files, {total/1e6:.1f} MB"


def fetch_texture(aid, res):
    info = get_json(f"{API}/files/{aid}")
    total, n = 0, 0
    for m in ("Diffuse", "nor_gl", "Rough", "Displacement", "AO"):
        entry = info.get(m, {}).get(res, {})
        for fmt in ("jpg", "png"):
            if fmt in entry:
                total += download(entry[fmt]["url"], DEST / aid / Path(entry[fmt]["url"]).name)
                n += 1
                break
    return f"{aid} (texture): {n} files, {total/1e6:.1f} MB" if n else f"{aid}: no texture maps at {res}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--res", default="2k")
    ap.add_argument("--ids", nargs="*", help="override the default asset list")
    ap.add_argument("--no-textures", action="store_true")
    args = ap.parse_args()

    ids = args.ids or MODELS
    t0 = time.time()
    ok, bad = 0, 0
    for aid in ids:
        try:
            print(fetch_model(aid, args.res), flush=True); ok += 1
        except Exception as ex:
            print(f"{aid}: FAILED ({ex})", flush=True); bad += 1
    if not args.no_textures and not args.ids:
        for aid in TEXTURES:
            try:
                print(fetch_texture(aid, args.res), flush=True); ok += 1
            except Exception as ex:
                print(f"{aid}: FAILED ({ex})", flush=True); bad += 1
    size = sum(p.stat().st_size for p in DEST.rglob("*") if p.is_file())
    print(f"done: {ok} ok, {bad} failed, {size/1e6:.0f} MB in {DEST} ({time.time()-t0:.0f}s)")


if __name__ == "__main__":
    main()
