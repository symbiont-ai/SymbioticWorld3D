"""
import_assets.py  --  headless FBX / glTF / OBJ import into /Game/Assets (UE 5.7.3).

Run INSIDE THE FULL EDITOR PROCESS (it opens, imports, saves and exits by itself).
The -run=pythonscript commandlet CRASHES after the first asset in UE 5.7.3
(AssetTools syncs the Content Browser, which needs Slate: "Assertion failed:
CurrentApplication.IsValid()"), so pass the source/destination as environment
variables and use -ExecutePythonScript:

  set SW_IMPORT_SRC=<repo>/AssetSources/polyhaven
  set SW_IMPORT_DEST=/Game/Assets/PolyHaven
  "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" ^
     "<repo>/SymbioticWorld.uproject" ^
     -ExecutePythonScript="<repo>/Tools/import_assets.py" -unattended -nosplash -log

  Never while a C++ build is running (the module DLL is locked). ~2-5 min for 20-30 files.
  Optional: SW_IMPORT_LEGACY=1 forces the legacy FBX factory for a misbehaving file.

Verified against UE 5.7.3 source:
  * AssetToolsImpl::ImportAssetTasks -> ImportAssetsInternal uses Interchange when
    Interchange.Import.Enable is true AND task.factory is None (AssetTools.cpp ~3523).
  * task.options may be an InterchangePipelineStackOverride (AssetTools.cpp ~3909); a legacy
    FbxImportUI is converted with InterchangeManager.ConvertImportData (~3918).
  * glTF/glb has NO legacy importer in 5.7 (only GLTFExporter plugin exists); Interchange handles it.
  * Interchange.FeatureFlags.Import.FBX defaults to true (InterchangeFbxTranslator.cpp:36).
"""
import os
import sys
import unreal

EXTS = (".fbx", ".gltf", ".glb", ".obj")


def _args():
    src = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("SW_IMPORT_SRC", "")
    dest = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("SW_IMPORT_DEST", "/Game/Assets")
    legacy = (len(sys.argv) > 3 and sys.argv[3].lower() == "legacy") or os.environ.get("SW_IMPORT_LEGACY") == "1"
    if not src:
        raise SystemExit("usage: import_assets.py <src_dir_or_file> [/Game/Assets/Sub] [legacy]")
    return os.path.abspath(src), dest, legacy


def _files(root):
    if os.path.isfile(root):
        return [root]
    out = []
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.lower().endswith(EXTS):
                out.append(os.path.join(d, f))
    return sorted(out)


def _interchange_override(nanite=True):
    """Optional explicit pipeline (skip it to use the project's default 'Assets' stack)."""
    pipe = unreal.InterchangeGenericAssetsPipeline()
    mesh = pipe.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("build_nanite", nanite)          # rocks/cliffs: True; keep small props cheap: False
    mesh.set_editor_property("combine_static_meshes", True)   # one StaticMesh per file (Poly Haven / Kenney pieces)
    mesh.set_editor_property("import_skeletal_meshes", True)  # Quaternius / Fab creatures
    mesh.set_editor_property("create_physics_asset", False)   # kinematic actors: no ragdoll needed
    anim = pipe.get_editor_property("animation_pipeline")
    anim.set_editor_property("import_animations", True)
    override = unreal.InterchangePipelineStackOverride()
    override.add_pipeline(pipe)
    return override


def make_task(path, dest, legacy=False, skeletal=None, override=None):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", path)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("destination_name", os.path.splitext(os.path.basename(path))[0])
    t.set_editor_property("automated", True)        # no dialogs; Interchange gets bIsAutomated
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    if legacy and path.lower().endswith((".fbx", ".obj")):
        # Force the legacy FBX factory (SpecifiedFactory != null bypasses Interchange).
        unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
        t.set_editor_property("factory", unreal.FbxFactory())
        ui = unreal.FbxImportUI()
        is_skel = bool(skeletal)
        ui.set_editor_property("import_mesh", True)
        ui.set_editor_property("import_as_skeletal", is_skel)
        ui.set_editor_property("import_animations", is_skel)
        ui.set_editor_property("import_materials", True)
        ui.set_editor_property("import_textures", True)
        ui.set_editor_property("mesh_type_to_import",
                               unreal.FBXImportType.FBXIT_SKELETAL_MESH if is_skel else unreal.FBXImportType.FBXIT_STATIC_MESH)
        ui.static_mesh_import_data.set_editor_property("combine_meshes", True)
        ui.skeletal_mesh_import_data.set_editor_property("import_morph_targets", False)
        t.set_editor_property("options", ui)
    elif override is not None:
        t.set_editor_property("options", override)  # InterchangePipelineStackOverride
    return t


def main():
    src, dest, legacy = _args()
    files = _files(src)
    if not files:
        raise SystemExit(f"no {EXTS} files under {src}")
    unreal.log(f"[import_assets] {len(files)} file(s) -> {dest} (legacy={legacy})")
    override = None if legacy else _interchange_override(nanite=True)
    tasks = []
    for f in files:
        skel = any(k in f.lower() for k in ("anim", "rig", "skel", "character", "creature"))
        tasks.append(make_task(f, dest, legacy=legacy, skeletal=skel, override=override))
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for t in tasks:
        for p in t.get_editor_property("imported_object_paths"):
            unreal.log(f"[import_assets] imported {p}")
    unreal.EditorAssetLibrary.save_directory(dest, only_if_is_dirty=True, recursive=True)
    unreal.log("[import_assets] done")


# Alternative: direct Interchange call (synchronous), useful for glTF with an explicit pipeline.
def import_one_interchange(path, dest):
    mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
    sd = unreal.InterchangeManager.create_source_data(path)
    params = unreal.ImportAssetParameters()
    params.set_editor_property("is_automated", True)
    params.set_editor_property("replace_existing", True)
    params.set_editor_property("destination_name", os.path.splitext(os.path.basename(path))[0])
    # params.set_editor_property("override_pipelines", [unreal.SoftObjectPath("/Interchange/Pipelines/DefaultAssetsPipeline.DefaultAssetsPipeline")])
    ok, objects = mgr.import_asset(dest, sd, params)
    return ok, [o.get_path_name() for o in objects]


if __name__ == "__main__":
    main()
