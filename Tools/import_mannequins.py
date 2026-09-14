#!/usr/bin/env python3
"""Import Epic's Manny / Quinn mannequins for the scientist avatars (visual only).

The avatar layer (Source/SymbioticWorld/SWScientistAvatar.*) renders the Symbiotic Lab's field
team as Epic's mannequins at human scale. That content is not in this repository
(Content/Characters/Mannequins is git-ignored: ~130 MB of Epic template content that every UE 5.7
install already carries; the sibling Content/Characters/Symbiotic is the project's own authored
creatures, untracked on purpose, see docs/CREATURE_RENDERING.md), so run this once per machine:

    python Tools/import_mannequins.py            # copy from the engine's template resources
    python Tools/import_mannequins.py --check    # report what is present / missing, copy nothing

Source: <UE_ROOT>/Templates/TemplateResources/High/Characters/Content/Mannequins (UE_ROOT env var
overrides the default C:\\Program Files\\Epic Games\\UE_5.7). Destination: Content/Characters/Mannequins.
Only what the avatars load is copied: meshes, materials, textures, rigs (physics asset + the control
rigs the meshes reference), and the three unarmed clips (idle, walk forward, jog forward).
Standard library only; safe to run while a sim is running (the sim never opens these files).
"""
import argparse
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UE_ROOT = Path(os.environ.get("UE_ROOT", r"C:\Program Files\Epic Games\UE_5.7"))
SRC = UE_ROOT / "Templates" / "TemplateResources" / "High" / "Characters" / "Content" / "Mannequins"
DST = ROOT / "Content" / "Characters" / "Mannequins"

# Directories are copied whole; files individually. Paths are relative to the Mannequins folder.
PARTS = [
    "Meshes",
    "Materials",
    "Textures",
    "Rigs",
    "Anims/Unarmed/MM_Idle.uasset",
    "Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.uasset",
    "Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd.uasset",
]
# What the C++ actually loads (the run fails soft without them: cylinder body, no animation).
REQUIRED = [
    "Meshes/SKM_Manny_Simple.uasset",
    "Meshes/SKM_Quinn_Simple.uasset",
    "Meshes/SK_Mannequin.uasset",
    "Anims/Unarmed/MM_Idle.uasset",
    "Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.uasset",
    "Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd.uasset",
]


def check():
    missing = [p for p in REQUIRED if not (DST / p).exists()]
    for p in REQUIRED:
        print(("  ok      " if (DST / p).exists() else "  MISSING ") + p)
    return missing


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="report presence only")
    args = ap.parse_args()
    if args.check:
        print(f"{DST}:")
        missing = check()
        sys.exit(1 if missing else 0)
    if not SRC.is_dir():
        sys.exit(f"mannequin template resources not found: {SRC}\n"
                 f"(set UE_ROOT to the engine folder, or copy a Mannequins folder to {DST} by hand)")
    copied = 0
    total = 0
    for part in PARTS:
        s, d = SRC / part, DST / part
        if s.is_dir():
            for f in s.rglob("*"):
                if f.is_file():
                    rel = f.relative_to(SRC)
                    (DST / rel).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(f, DST / rel)
                    copied += 1
                    total += f.stat().st_size
        elif s.is_file():
            d.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(s, d)
            copied += 1
            total += s.stat().st_size
        else:
            print(f"  skipped (not in this engine's pack): {part}")
    print(f"copied {copied} files, {total / 1e6:.0f} MB -> {DST}")
    missing = check()
    if missing:
        sys.exit("still missing: " + ", ".join(missing))


if __name__ == "__main__":
    main()
