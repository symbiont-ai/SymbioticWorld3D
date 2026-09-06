"""
migrate_ed.py -- migrate the Electric Dreams manifest packages (plus their /Game dependencies)
from the launcher-vault sample into Symbiotic World with IAssetTools::MigratePackages.

Run INSIDE THE SAMPLE PROJECT, with the full editor, on the empty engine map (the 4 km level never loads):

  export MSYS_NO_PATHCONV=1
  "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" \
      "C:/ProgramData/Epic/EpicGamesLauncher/VaultCache/ElectricDreamsSample_5.7/data/ElectricDreamsSample.uproject" \
      /Engine/Maps/Entry -ExecutePythonScript="<repo>/Tools/migrate_ed.py" \
      -unattended -nosplash -log -abslog="<repo>/AssetSources/ed_migrate_log.txt"

-ExecutePythonScript waits until the asset registry has finished scanning, runs this file with the
Unattended flag (no modal dialogs) and then issues QUIT_EDITOR by itself (EditorPythonExecuter.cpp).
Never use -run=pythonscript for this: the AssetTools notifications need Slate.
First run 2026-09-05: editor start + Substrate shader warm-up ~8 min, the migration itself 4.1 s
(274 packages, 3633.7 MB, classic file copy), 0 missing, 0 size mismatches.

Environment (all optional):
  SW_MIGRATE_MANIFEST     manifest json     (default <repo>/AssetSources/ed_manifest.json)
  SW_MIGRATE_DEST         destination Content DIRECTORY (default <repo>/Content);
                          MigratePackages takes the content folder, packages keep their /Game/... paths
  SW_MIGRATE_MODE         copy (default) -> AssetTools.UseNewPackageMigration=0: the classic migration, a
                                            byte-for-byte IFileManager::Copy of every package, nothing is loaded
                          new             -> 5.1+ migration: instanced load of the whole closure + resave
  SW_MIGRATE_EXCLUDE      ';'-separated packages that must stay out (default: manifest excluded_from_closure)
  SW_MIGRATE_MIN_FREE_GB  headroom required on top of manifest estimated_gb (default 8)
  SW_MIGRATE_DRYRUN=1     gather + report only, copy nothing

Verified against UE 5.7.3 source (Engine/Source/Developer/AssetTools):
  * IAssetTools.h: FMigrationOptions{bPrompt, bIgnoreDependencies, AssetConflict, OrphanFolder},
    EAssetMigrationConflict{Skip, Overwrite, Cancel} -> unreal.MigrationOptions(prompt, ignore_dependencies,
    asset_conflict, orphan_folder) / unreal.AssetMigrationConflict.SKIP.
  * AssetTools.cpp PerformMigratePackages: RecursiveGetDependencies follows hard AND soft package references
    (IAssetRegistry::GetDependencies default category), skips /Script and assets that no longer exist.
    bIgnoreDependencies keeps only the names passed in; that is how the manifest's excluded stubs are kept out.
  * Engine content (/Engine, engine plugins such as /Niagara) is never migrated ("Engine assets cannot be
    migrated"); it is identical on any 5.7.3 install, so it is only reported here as SHARED_ENGINE.
  * Legacy branch (UseNewPackageMigration=0): /Game packages map straight to <dest>/<sub path>, existing files
    are skipped under EAssetMigrationConflict::Skip, everything else is copied.
"""
import json
import os
import shutil
import time

import unreal

TAG = "[migrate_ed]"
_SW_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_MANIFEST = os.path.join(_SW_ROOT, "AssetSources", "ed_manifest.json")
DEFAULT_DEST = os.path.join(_SW_ROOT, "Content")


def log(msg):
    unreal.log(f"{TAG} {msg}")


def warn(msg):
    unreal.log_warning(f"{TAG} {msg}")


def err(msg):
    unreal.log_error(f"{TAG} {msg}")


def norm(p):
    return os.path.normpath(p).replace("\\", "/")


def rel_of(package):
    """'/Game/A/B' -> 'A/B' (None for anything that is not /Game)."""
    return package[len("/Game/"):] if package.startswith("/Game/") else None


def find_source_file(content_dir, package):
    rel = rel_of(package)
    if rel is None:
        return None
    for ext in (".uasset", ".umap"):
        p = os.path.join(content_dir, rel + ext)
        if os.path.exists(p):
            return norm(p)
    return None


def dest_file(dest_dir, package, src):
    rel = rel_of(package)
    if rel is None or src is None:
        return None
    return norm(os.path.join(dest_dir, rel + os.path.splitext(src)[1]))


def package_exists(registry, package):
    return len(registry.get_assets_by_package_name(unreal.Name(package), True)) > 0


def project_plugin_roots():
    """Mount points of the SOURCE project's own plugins (their content would need a matching root)."""
    roots = set()
    plugins_dir = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_plugins_dir())
    if os.path.isdir(plugins_dir):
        for dirpath, dirnames, filenames in os.walk(plugins_dir):
            for f in filenames:
                if f.lower().endswith(".uplugin") and os.path.isdir(os.path.join(dirpath, "Content")):
                    roots.add("/" + os.path.splitext(f)[0] + "/")
            if dirpath.count(os.sep) - plugins_dir.count(os.sep) >= 3:
                dirnames[:] = []
    return roots


def closure(registry, roots, exclude):
    """Same walk as UAssetToolsImpl::RecursiveGetDependencies: hard + soft package refs, no /Script,
    no missing packages; `exclude` stops the walk (like the CanMigratePackage delegate)."""
    opts = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True,
        include_hard_package_references=True,
        include_searchable_names=False,
        include_soft_management_references=False,
        include_hard_management_references=False,
    )
    seen = []
    seen_set = set()
    excluded_hits = set()
    stack = list(roots)
    for r in roots:
        if r not in seen_set:
            seen_set.add(r)
            seen.append(r)
    while stack:
        pkg = stack.pop()
        deps = registry.get_dependencies(unreal.Name(pkg), opts) or []
        for d in deps:
            d = str(d)
            if d.startswith("/Script"):
                continue
            if d in seen_set or d in excluded_hits:
                continue
            if d in exclude:
                excluded_hits.add(d)
                continue
            if not package_exists(registry, d):
                continue
            seen_set.add(d)
            seen.append(d)
            stack.append(d)
    return seen, excluded_hits


def main():
    t0 = time.time()
    manifest_path = os.environ.get("SW_MIGRATE_MANIFEST", DEFAULT_MANIFEST)
    dest_dir = norm(os.environ.get("SW_MIGRATE_DEST", DEFAULT_DEST))
    mode = os.environ.get("SW_MIGRATE_MODE", "copy").strip().lower()
    dry_run = os.environ.get("SW_MIGRATE_DRYRUN", "0") == "1"
    min_free_extra = float(os.environ.get("SW_MIGRATE_MIN_FREE_GB", "8"))

    log(f"project={unreal.Paths.get_project_file_path()} engine={unreal.SystemLibrary.get_engine_version()}")
    log(f"manifest={manifest_path} dest={dest_dir} mode={mode} dry_run={dry_run}")

    with open(manifest_path, "r", encoding="utf-8") as f:
        manifest = json.load(f)
    packages = list(dict.fromkeys(manifest["packages"]))
    est_gb = float(manifest.get("estimated_gb", 0.0))
    log(f"manifest packages={len(packages)} estimated_gb={est_gb} substrate_required={manifest.get('substrate_required')}")

    exclude_env = os.environ.get("SW_MIGRATE_EXCLUDE")
    if exclude_env is None:
        exclude = set(manifest.get("excluded_from_closure", {}).keys())
    else:
        exclude = {e.strip() for e in exclude_env.split(";") if e.strip()}
    for e in sorted(exclude):
        log(f"exclude {e}")

    # 1) destination sanity + disk headroom
    if not os.path.isdir(dest_dir):
        err(f"destination Content folder does not exist: {dest_dir}")
        return
    uprojects = [p for p in os.listdir(os.path.dirname(dest_dir)) if p.lower().endswith(".uproject")]
    if not uprojects:
        err(f"no .uproject next to {dest_dir}; MigratePackages needs a project Content folder")
        return
    usage = shutil.disk_usage(dest_dir)
    free_gb = usage.free / 1e9
    need_gb = est_gb + min_free_extra
    log(f"disk free={free_gb:.2f} GB need={need_gb:.2f} GB (estimated {est_gb} + {min_free_extra} headroom)")
    if free_gb < need_gb:
        err(f"ABORT: only {free_gb:.2f} GB free on the destination drive, need {need_gb:.2f} GB")
        return

    # 2) registry
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    content_dir = norm(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()))
    log(f"source content dir={content_dir}")

    missing_src = [p for p in packages if not package_exists(registry, p)]
    for p in missing_src:
        err(f"manifest package not in the sample registry: {p}")
    roots = [p for p in packages if p not in missing_src]

    # 3) dependency closure (what the engine would gather), minus exclusions
    full, excluded_hits = closure(registry, roots, exclude)
    plugin_roots = project_plugin_roots()
    game = [p for p in full if p.startswith("/Game/")]
    project_plugin = [p for p in full if any(p.startswith(r) for r in plugin_roots)]
    shared_engine = [p for p in full if p not in set(game) and p not in set(project_plugin)]
    manifest_set = set(packages)
    extras_game = [p for p in game if p not in manifest_set]
    log(f"closure={len(full)} packages: /Game {len(game)} (manifest {len(roots)} + {len(extras_game)} extra), "
        f"shared engine/engine-plugin content {len(shared_engine)} (not migrated, identical in every 5.7.3 install), "
        f"project-plugin content {len(project_plugin)}")
    for p in extras_game:
        log(f"extra-game-dependency {p}")
    for p in shared_engine:
        log(f"SHARED_ENGINE {p}")
    for p in sorted(excluded_hits):
        warn(f"excluded-but-referenced {p} (kept out on purpose)")
    for p in project_plugin:
        warn(f"project-plugin package in closure (needs its own content root in the destination): {p}")

    # size + pre-existing state
    src_files = {}
    total_mb = 0.0
    pre_existing = set()
    for p in game:
        s = find_source_file(content_dir, p)
        src_files[p] = s
        if s is None:
            warn(f"no package file on disk for {p}")
            continue
        total_mb += os.path.getsize(s) / 1e6
        d = dest_file(dest_dir, p, s)
        if d and os.path.exists(d):
            pre_existing.add(p)
    log(f"/Game closure size on disk={total_mb:.1f} MB ({total_mb / 1000:.2f} GB); already in destination={len(pre_existing)}")

    if dry_run:
        log("DRY RUN: nothing migrated")
        return

    # 4) migration
    if mode == "copy":
        world = None
        try:
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        except Exception:
            world = None
        unreal.SystemLibrary.execute_console_command(world, "AssetTools.UseNewPackageMigration 0")
        try:
            v = unreal.SystemLibrary.get_console_variable_int_value("AssetTools.UseNewPackageMigration")
            log(f"AssetTools.UseNewPackageMigration={v} (0 = classic file copy)")
        except Exception as e:  # noqa: BLE001
            warn(f"could not read back AssetTools.UseNewPackageMigration: {e}")
    else:
        log("mode=new: instanced load + resave (AssetTools.UseNewPackageMigration left at its default 1)")

    # With exclusions hit we must hand the engine the closure ourselves (bIgnoreDependencies keeps only the
    # names passed in); otherwise the engine gathers the dependencies exactly as we did.
    use_own_closure = len(excluded_hits) > 0
    names_py = (game + project_plugin) if use_own_closure else roots
    names = unreal.Array(unreal.Name)
    for p in names_py:
        names.append(unreal.Name(p))

    opts = unreal.MigrationOptions()
    opts.set_editor_property("prompt", False)
    opts.set_editor_property("ignore_dependencies", use_own_closure)
    opts.set_editor_property("asset_conflict", unreal.AssetMigrationConflict.SKIP)
    opts.set_editor_property("orphan_folder", "ED_Orphans")
    log(f"migrate_packages(names={len(names_py)}, dest='{dest_dir}', prompt=False, "
        f"ignore_dependencies={use_own_closure}, asset_conflict=SKIP, orphan_folder='ED_Orphans')"
        + (" [own closure: exclusions hit]" if use_own_closure else " [engine gathers dependencies]"))

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    t1 = time.time()
    tools.migrate_packages(names, dest_dir, opts)
    log(f"migrate_packages returned after {time.time() - t1:.1f} s")

    # 5) verification, one line per /Game package
    counts = {"COPIED": 0, "SKIPPED_EXISTING": 0, "MISSING": 0, "SIZE_MISMATCH": 0, "NO_SOURCE": 0}
    copied_mb = 0.0
    failed = []
    for p in game:
        s = src_files.get(p)
        if s is None:
            counts["NO_SOURCE"] += 1
            failed.append(p)
            log(f"NO_SOURCE {p}")
            continue
        d = dest_file(dest_dir, p, s)
        ssz = os.path.getsize(s)
        if not os.path.exists(d):
            counts["MISSING"] += 1
            failed.append(p)
            log(f"MISSING {p} -> {d}")
            continue
        dsz = os.path.getsize(d)
        if p in pre_existing:
            counts["SKIPPED_EXISTING"] += 1
            log(f"SKIPPED_EXISTING {p} src={ssz / 1e6:.2f}MB dst={dsz / 1e6:.2f}MB")
        elif dsz != ssz and mode == "copy":
            counts["SIZE_MISMATCH"] += 1
            failed.append(p)
            log(f"SIZE_MISMATCH {p} src={ssz} dst={dsz}")
        else:
            counts["COPIED"] += 1
            copied_mb += dsz / 1e6
            log(f"COPIED {p} {dsz / 1e6:.2f}MB")

    excluded_present = [e for e in exclude if find_source_file(dest_dir, e)]
    for e in excluded_present:
        warn(f"excluded package IS present in the destination: {e}")

    result = {
        "mode": mode,
        "ignore_dependencies_passed": use_own_closure,
        "manifest_packages": len(packages),
        "manifest_missing_in_source": missing_src,
        "closure_packages": len(full),
        "game_packages": len(game),
        "extra_game_dependencies": extras_game,
        "shared_engine_packages": len(shared_engine),
        "project_plugin_packages": project_plugin,
        "excluded_hits": sorted(excluded_hits),
        "excluded_present_in_dest": excluded_present,
        "counts": counts,
        "copied_mb": round(copied_mb, 1),
        "game_closure_mb": round(total_mb, 1),
        "failed": failed,
        "seconds": round(time.time() - t0, 1),
        "dest": dest_dir,
    }
    log("RESULT_JSON " + json.dumps(result))
    status = "OK" if not failed and not excluded_present else "WITH_FAILURES"
    log(f"DONE {status} copied={counts['COPIED']} skipped_existing={counts['SKIPPED_EXISTING']} "
        f"missing={counts['MISSING']} size_mismatch={counts['SIZE_MISMATCH']} shared_engine={len(shared_engine)} "
        f"copied_mb={copied_mb:.1f} elapsed={time.time() - t0:.1f}s")


main()
