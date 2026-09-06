"""Read-only asset inventory of ANY Unreal project via the asset registry.

Nothing is loaded or saved: only registry metadata (class, path, tags such as
NaniteEnabled / Triangles / LODs / material count) plus the .uasset size on disk.

Run against a project (here: the Electric Dreams sample in the launcher vault):
  set SW_DUMP_OUT=<repo>/AssetSources/ed_registry.csv
  "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" ^
     "<that project>/<Name>.uproject" -run=pythonscript ^
     -script="<repo>/Tools/registry_dump.py" -unattended -nosplash -stdout -FullStdOutLogOutput

Columns: class, package, name, size_mb, nanite, triangles, lods, materials, tags(json)
"""
import csv
import json
import os
import unreal

OUT = os.environ.get("SW_DUMP_OUT", os.path.join(unreal.Paths.project_saved_dir(), "registry_dump.csv"))
ROOTS = [r for r in os.environ.get("SW_DUMP_ROOTS", "/Game").split(";") if r]
TAG_KEYS = ["NaniteEnabled", "Triangles", "LODs", "Materials", "Vertices", "CollisionPrims", "SourceFile",
            "ImportedSourceFile", "ShadingModel", "BlendMode", "bUsedWithNanite", "IsSubstrate", "PhysicalMaterial",
            "AssetImportData", "Dimensions", "Format", "Skeleton", "NumFrames", "Bones"]

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.wait_for_completion()
content_dir = unreal.Paths.project_content_dir()


def disk_size(package):
    rel = package.replace("/Game/", "", 1) if package.startswith("/Game/") else None
    if rel is None:
        return 0.0
    for ext in (".uasset", ".umap"):
        p = os.path.join(content_dir, rel + ext)
        if os.path.exists(p):
            return round(os.path.getsize(p) / 1e6, 3)
    return 0.0


rows = 0
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["class", "package", "name", "size_mb", "nanite", "triangles", "lods", "materials", "tags"])
    for root in ROOTS:
        for a in registry.get_assets_by_path(root, recursive=True):
            cls = str(a.asset_class_path.asset_name)
            pkg = str(a.package_name)
            tags = {}
            for k in TAG_KEYS:
                v = a.get_tag_value(k)
                if v:
                    tags[k] = str(v)
            w.writerow([cls, pkg, str(a.asset_name), disk_size(pkg), tags.get("NaniteEnabled", ""),
                        tags.get("Triangles", ""), tags.get("LODs", ""), tags.get("Materials", ""), json.dumps(tags)])
            rows += 1
unreal.log(f"[registry_dump] {rows} assets -> {OUT}")
